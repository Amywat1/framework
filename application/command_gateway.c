/**
 * @file    command_gateway.c
 * @brief   命令网关实现（submit → 裁决 → side_effect_router → receipt）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/command_gateway.h"

#include "application/side_effect_router.h"
#include "common/event_types.h"
#include "common/log.h"
#include "domain/command_gateway/operational_mode.h"
#include "ports/inbound/command/command_port.h"
#include "common/time_util.h"
#include "runtime/event_bus/event_bus.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

#define CMD_GATEWAY_QUEUE_SIZE 4U
#define CMD_GATEWAY_WAIT_MS    5000U

typedef struct {
    bool              used;
    uint32_t          generation;
    dev_cmd_t         cmd;
    dev_cmd_receipt_t receipt;
    sem_t             done;
} cmd_gateway_slot_t;

static cmd_gateway_slot_t s_queue[CMD_GATEWAY_QUEUE_SIZE];
static pthread_mutex_t    s_q_mutex    = PTHREAD_MUTEX_INITIALIZER;
static uint32_t           s_generation = 0U;

static void publish_cmd_handled(dev_cmd_kind_t kind, dev_cmd_status_t status, op_reject_reason_t reason)
{
    uint32_t param = ((uint32_t)kind << 16) | ((uint32_t)status << 8) | (uint32_t)reason;

    (void)event_publish(EVT_OP_MODE_CMD_HANDLED, param);
}

static dev_cmd_status_t status_from_effect(sw_err_t effect_err, op_cmd_result_t verdict)
{
    if (verdict != OP_CMD_ALLOWED) {
        return DEV_CMD_STATUS_REJECTED;
    }
    if (effect_err != SW_OK) {
        return DEV_CMD_STATUS_FAILED;
    }
    return DEV_CMD_STATUS_ACCEPTED;
}

static sw_err_t receipt_to_sw_err(const dev_cmd_receipt_t *receipt)
{
    if (receipt == NULL) {
        return SW_ERR_PARAM;
    }

    switch (receipt->status) {
    case DEV_CMD_STATUS_ACCEPTED:
        return SW_OK;
    case DEV_CMD_STATUS_REJECTED:
        return SW_ERR_STATE;
    case DEV_CMD_STATUS_FAILED:
        return (receipt->effect_error != SW_OK) ? receipt->effect_error : SW_ERR_HW;
    case DEV_CMD_STATUS_TIMEOUT:
        return SW_ERR_TIMEOUT;
    case DEV_CMD_STATUS_BUSY:
        return SW_ERR_BUSY;
    default:
        return SW_ERR_HW;
    }
}

static void release_slot_if_current(cmd_gateway_slot_t *slot, uint32_t generation)
{
    pthread_mutex_lock(&s_q_mutex);
    if (slot->used && (slot->generation == generation)) {
        slot->used = false;
    }
    pthread_mutex_unlock(&s_q_mutex);
}

static void on_gateway_wake(const event_t *evt)
{
    (void)evt;
    command_gateway_drain();
}

static sw_err_t gateway_submit(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt, uint32_t timeout_ms)
{
    cmd_gateway_slot_t *slot          = NULL;
    uint32_t            my_generation = 0U;
    struct timespec     ts;
    int                 sem_ret;
    sw_err_t            ret;
    dev_cmd_receipt_t   local_receipt;
    uint32_t            wait_ms = (timeout_ms > 0U) ? timeout_ms : CMD_GATEWAY_WAIT_MS;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_q_mutex);
    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        if (!s_queue[i].used) {
            slot                        = &s_queue[i];
            slot->used                  = true;
            my_generation               = ++s_generation;
            slot->generation            = my_generation;
            slot->cmd                   = *cmd;
            slot->receipt.status        = DEV_CMD_STATUS_BUSY;
            slot->receipt.reject_reason = OP_REJECT_NONE;
            slot->receipt.effect_error  = SW_ERR_BUSY;
            slot->receipt.request_id    = cmd->meta.request_id;
            break;
        }
    }
    pthread_mutex_unlock(&s_q_mutex);

    if (slot == NULL) {
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_BUSY;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_BUSY;
            receipt->request_id    = cmd->meta.request_id;
        }
        return SW_ERR_BUSY;
    }

    if (event_publish(EVT_CMD_GATEWAY_WAKE, 0U) != SW_OK) {
        release_slot_if_current(slot, my_generation);
        return SW_ERR_BUSY;
    }

    time_util_fill_deadline(wait_ms, &ts);
    sem_ret = sem_timedwait(&slot->done, &ts);
    if (sem_ret != 0) {
        release_slot_if_current(slot, my_generation);
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_TIMEOUT;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_TIMEOUT;
            receipt->request_id    = cmd->meta.request_id;
        }
        return SW_ERR_TIMEOUT;
    }

    pthread_mutex_lock(&s_q_mutex);
    if (!slot->used || (slot->generation != my_generation)) {
        pthread_mutex_unlock(&s_q_mutex);
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_TIMEOUT;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_TIMEOUT;
            receipt->request_id    = cmd->meta.request_id;
        }
        return SW_ERR_TIMEOUT;
    }

    local_receipt = slot->receipt;
    slot->used    = false;
    pthread_mutex_unlock(&s_q_mutex);

    if (receipt != NULL) {
        *receipt = local_receipt;
    }

    ret = receipt_to_sw_err(&local_receipt);
    return ret;
}

void command_gateway_drain(void)
{
    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        cmd_gateway_slot_t *slot = &s_queue[i];
        dev_cmd_t           cmd;
        uint32_t            generation;
        dev_cmd_decision_t  decision;
        sw_err_t            effect_err = SW_OK;
        dev_cmd_receipt_t   receipt;

        pthread_mutex_lock(&s_q_mutex);
        if (!slot->used) {
            pthread_mutex_unlock(&s_q_mutex);
            continue;
        }

        generation = slot->generation;
        cmd        = slot->cmd;
        pthread_mutex_unlock(&s_q_mutex);

        decision = op_mode_handle_command(&cmd);
        if (decision.verdict == OP_CMD_ALLOWED) {
            effect_err = side_effect_router_run(decision.pending_effect, &cmd);
        }

        receipt.request_id    = cmd.meta.request_id;
        receipt.reject_reason = decision.reason;
        receipt.effect_error  = effect_err;
        receipt.status        = status_from_effect(effect_err, decision.verdict);

        publish_cmd_handled(cmd.body.kind, receipt.status, receipt.reject_reason);

        pthread_mutex_lock(&s_q_mutex);
        if (slot->used && (slot->generation == generation)) {
            slot->receipt = receipt;
            pthread_mutex_unlock(&s_q_mutex);
            (void)sem_post(&slot->done);
        } else {
            pthread_mutex_unlock(&s_q_mutex);
        }
    }
}

sw_err_t command_gateway_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_CMD_GATEWAY_WAKE, on_gateway_wake},
    };

    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        if (sem_init(&s_queue[i].done, 0, 0) != 0) {
            return SW_ERR_HW;
        }
        s_queue[i].used       = false;
        s_queue[i].generation = 0U;
    }

    sw_err_t ret = event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
    if (ret != SW_OK) {
        return ret;
    }

    static const device_command_port_ops_t s_ops = {.submit = gateway_submit};
    device_command_port_register(&s_ops);
    LOG_INFO("command_gateway: init ok");
    return SW_OK;
}
