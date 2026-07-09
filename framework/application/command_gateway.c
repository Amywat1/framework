/**
 * @file    command_gateway.c
 * @brief   命令网关实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/application/command_gateway.h"
#include "framework/application/self_check_service.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/safety/alarm/alarm_core.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/domain/command_gateway/op_mode_types.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"
#include "framework/services/dev_ctx/dev_ctx.h"

#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define CMD_GATEWAY_QUEUE_SIZE   4U
#define CMD_GATEWAY_WAIT_MS      5000U

typedef struct
{
    bool      used;
    uint32_t  generation;
    cmd_t     cmd;
    sw_err_t  result;
    sem_t     done;
} cmd_gateway_slot_t;

static cmd_gateway_slot_t s_queue[CMD_GATEWAY_QUEUE_SIZE];
static pthread_mutex_t    s_q_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t           s_generation = 0U;

static sw_err_t map_result(op_command_result_t r)
{
    if (r.result == OP_CMD_ALLOWED)
    {
        return SW_OK;
    }
    return SW_ERR_STATE;
}

static void release_slot_if_current(cmd_gateway_slot_t *slot, uint32_t generation)
{
    pthread_mutex_lock(&s_q_mutex);
    if (slot->used && (slot->generation == generation))
    {
        slot->used = false;
    }
    pthread_mutex_unlock(&s_q_mutex);
}

static sw_err_t dispatch_side_effects(const cmd_t *cmd)
{
    switch (cmd->type)
    {
    case CMD_START_WASH:
        return wash_orchestrator_start(cmd->payload.start_wash.mode);

    case CMD_STOP_WASH:
        wash_orchestrator_abort(WASH_ABORT_MANUAL);
        return SW_OK;

    case CMD_START_SELF_CHECK:
        return self_check_service_start();

    case CMD_RESET_FAULT:
    {
        device_context_t ctx = dev_ctx_snapshot();

        if ((ctx.operational_mode == OP_MODE_IDLE) &&
            (ctx.safety_state != SAFETY_STATE_WARNING))
        {
            return SW_ERR_STATE;
        }
        (void)alarm_core_reset_alarms();
        op_mode_on_legacy_reset_fault();
        return SW_OK;
    }

    case CMD_HOME_DEVICE:
    {
        sw_err_t ret = gantry_home();

        if (ret != SW_OK)
        {
            LOG_ERROR("command_gateway: gantry_home failed ret=%d", (int)ret);
        }
        return ret;
    }

    case CMD_ENTER_MANUAL:
    case CMD_MANUAL_ACTUATOR:
    case CMD_RECOVER:
    case CMD_STOP_OPERATION:
    case CMD_RESUME_OPERATION:
        return SW_OK;

    default:
        return SW_ERR_PARAM;
    }
}

static void on_gateway_wake(const event_t *evt)
{
    (void)evt;
    command_gateway_drain();
}

static sw_err_t gateway_inject(const cmd_t *cmd)
{
    cmd_gateway_slot_t *slot = NULL;
    uint32_t            my_generation = 0U;
    struct timespec     ts;
    int                 sem_ret;
    sw_err_t            ret;

    if (cmd == NULL)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_q_mutex);
    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++)
    {
        if (!s_queue[i].used)
        {
            slot = &s_queue[i];
            slot->used = true;
            my_generation = ++s_generation;
            slot->generation = my_generation;
            slot->cmd = *cmd;
            slot->result = SW_ERR_BUSY;
            break;
        }
    }
    pthread_mutex_unlock(&s_q_mutex);

    if (slot == NULL)
    {
        return SW_ERR_BUSY;
    }

    if (event_publish(EVT_CMD_GATEWAY_WAKE, 0U) != SW_OK)
    {
        release_slot_if_current(slot, my_generation);
        return SW_ERR_BUSY;
    }

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
        release_slot_if_current(slot, my_generation);
        return SW_ERR_HW;
    }
    ts.tv_sec += (time_t)(CMD_GATEWAY_WAIT_MS / 1000U);
    ts.tv_nsec += (long)(CMD_GATEWAY_WAIT_MS % 1000U) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    sem_ret = sem_timedwait(&slot->done, &ts);
    if (sem_ret != 0)
    {
        release_slot_if_current(slot, my_generation);
        return SW_ERR_TIMEOUT;
    }

    pthread_mutex_lock(&s_q_mutex);
    if (!slot->used || (slot->generation != my_generation))
    {
        pthread_mutex_unlock(&s_q_mutex);
        return SW_ERR_TIMEOUT;
    }

    ret = slot->result;
    slot->used = false;
    pthread_mutex_unlock(&s_q_mutex);
    return ret;
}

void command_gateway_drain(void)
{
    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++)
    {
        cmd_gateway_slot_t *slot = &s_queue[i];
        cmd_t               cmd;
        uint32_t            generation;
        op_command_result_t op_r;
        sw_err_t            side_ret = SW_OK;
        sw_err_t            final_ret;

        pthread_mutex_lock(&s_q_mutex);
        if (!slot->used)
        {
            pthread_mutex_unlock(&s_q_mutex);
            continue;
        }

        generation = slot->generation;
        cmd = slot->cmd;
        pthread_mutex_unlock(&s_q_mutex);

        op_r = op_mode_handle_command(&cmd);
        if (op_r.result == OP_CMD_ALLOWED)
        {
            side_ret = dispatch_side_effects(&cmd);
            final_ret = (side_ret != SW_OK) ? side_ret : SW_OK;
        }
        else
        {
            final_ret = map_result(op_r);
        }

        pthread_mutex_lock(&s_q_mutex);
        if (slot->used && (slot->generation == generation))
        {
            slot->result = final_ret;
            pthread_mutex_unlock(&s_q_mutex);
            (void)sem_post(&slot->done);
        }
        else
        {
            pthread_mutex_unlock(&s_q_mutex);
        }
    }
}

sw_err_t command_gateway_init(void)
{
    static const event_subscription_t s_subs[] = {
        { EVT_CMD_GATEWAY_WAKE, on_gateway_wake },
    };

    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++)
    {
        if (sem_init(&s_queue[i].done, 0, 0) != 0)
        {
            return SW_ERR_HW;
        }
        s_queue[i].used = false;
        s_queue[i].generation = 0U;
    }

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK)
    {
        return ret;
    }

    static const command_port_ops_t s_ops = { .inject = gateway_inject };
    command_port_register(&s_ops);
    LOG_INFO("command_gateway: init ok");
    return SW_OK;
}
