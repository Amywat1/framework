/**
 * @file    command_gateway.c
 * @brief   命令网关：独立 control 线程串行裁决；STOP_ALL 优先/合并/抢占
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "application/command_gateway.h"

#include "application/ports/inbound/command/command_port.h"
#include "application/side_effect_router.h"
#include "common/event_types.h"
#include "common/log.h"
#include "common/time_util.h"
#include "common/trace_context.h"
#include "domain/op_mode/operational_mode.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/thread_registry.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <string.h>

#define CMD_GATEWAY_QUEUE_SIZE 4U
#define CMD_GATEWAY_WAIT_MS    5000U

typedef struct {
    bool              used;
    bool              waiting; /**< true：sync 等待者；false：async，drain 后直接释槽 */
    uint32_t          generation;
    dev_cmd_t         cmd;
    dev_cmd_receipt_t receipt;
    sem_t             done;
} cmd_gateway_slot_t;

static cmd_gateway_slot_t s_queue[CMD_GATEWAY_QUEUE_SIZE];
static pthread_mutex_t    s_q_mutex    = PTHREAD_MUTEX_INITIALIZER;
static uint32_t           s_generation = 0U;
static uint64_t           s_next_request_id;
static sem_t              s_wake;
static bool               s_wake_ready;

/* drain 所在线程（cmd_control）标识，用于拦截 submit_sync 自锁 */
static pthread_t s_drain_thread;
static bool      s_drain_thread_known;

#ifdef WDF_UNIT_TEST
static pthread_t    s_test_ctrl_tid;
static volatile int s_test_ctrl_stop;
static bool         s_test_ctrl_started;
#endif

static bool is_stop_priority(dev_cmd_kind_t kind)
{
    return kind == DEV_CMD_STOP_ALL_OUTPUTS;
}

static void publish_cmd_handled(dev_cmd_kind_t kind, dev_cmd_status_t status, op_reject_reason_t reason)
{
    (void)event_publish(EVT_OP_MODE_CMD_HANDLED, cmd_handled_evt_param((uint8_t)kind, status, reason));
}

static void wake_control(void)
{
    if (s_wake_ready) {
        (void)sem_post(&s_wake);
    }
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
        slot->used    = false;
        slot->waiting = false;
    }
    pthread_mutex_unlock(&s_q_mutex);
}

/** @brief 持锁：写入槽位 */
static void fill_slot_locked(cmd_gateway_slot_t *slot,
                             const dev_cmd_t    *cmd,
                             bool                waiting,
                             uint64_t            request_id,
                             uint32_t           *out_generation)
{
    slot->used                  = true;
    slot->waiting               = waiting;
    *out_generation             = ++s_generation;
    slot->generation            = *out_generation;
    slot->cmd                   = *cmd;
    slot->cmd.meta.request_id   = request_id;
    slot->receipt.status        = DEV_CMD_STATUS_BUSY;
    slot->receipt.reject_reason = OP_REJECT_NONE;
    slot->receipt.effect_error  = SW_ERR_BUSY;
    slot->receipt.request_id    = request_id;
    while (sem_trywait(&slot->done) == 0) {
        /* 排空残留 */
    }
}

/**
 * @brief  入队；STOP_ALL 可合并 async 同类，并可抢占非优先槽
 * @note   抢占直接覆写槽位；旧 sync 等待者因 generation 不匹配而 TIMEOUT（不保证及时 BUSY）
 */
static sw_err_t enqueue_command(const dev_cmd_t     *cmd,
                                bool                 waiting,
                                uint64_t            *out_request_id,
                                cmd_gateway_slot_t **out_slot,
                                uint32_t            *out_generation)
{
    cmd_gateway_slot_t *slot       = NULL;
    uint32_t            generation = 0U;
    uint64_t            request_id;
    bool                priority;
    dev_cmd_kind_t      preempted_kind           = DEV_CMD_NONE;
    uint64_t            preempted_request_id     = 0U;
    uint64_t            preempted_correlation_id = 0U;
    bool                preempted_publish        = false;

    if ((cmd == NULL) || (out_slot == NULL) || (out_generation == NULL)) {
        return SW_ERR_PARAM;
    }

    priority = is_stop_priority(cmd->body.kind);

    pthread_mutex_lock(&s_q_mutex);
    request_id = (cmd->meta.request_id != 0U) ? cmd->meta.request_id : ++s_next_request_id;

    if (priority) {
        for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
            if (!s_queue[i].used || (s_queue[i].cmd.body.kind != cmd->body.kind)) {
                continue;
            }
            if (!s_queue[i].waiting) {
                if (!waiting) {
                    /* async 合并进已有 async STOP */
                    request_id = s_queue[i].cmd.meta.request_id;
                    pthread_mutex_unlock(&s_q_mutex);
                    if (out_request_id != NULL) {
                        *out_request_id = request_id;
                    }
                    *out_slot       = NULL;
                    *out_generation = 0U;
                    return SW_OK;
                }
                /* sync 挂到已有 async STOP 槽上等待同一结果 */
                s_queue[i].waiting = true;
                request_id         = s_queue[i].cmd.meta.request_id;
                slot               = &s_queue[i];
                generation         = s_queue[i].generation;
                break;
            }
        }
    }

    if (slot == NULL) {
        for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
            if (!s_queue[i].used) {
                slot = &s_queue[i];
                fill_slot_locked(slot, cmd, waiting, request_id, &generation);
                break;
            }
        }
    }

    if ((slot == NULL) && priority) {
        for (unsigned pass = 0; pass < 2U; pass++) {
            for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
                cmd_gateway_slot_t *victim = &s_queue[i];

                if (!victim->used || is_stop_priority(victim->cmd.body.kind)) {
                    continue;
                }
                if ((pass == 0U) && victim->waiting) {
                    continue;
                }

                preempted_kind           = victim->cmd.body.kind;
                preempted_request_id     = victim->cmd.meta.request_id;
                preempted_correlation_id = victim->cmd.meta.correlation_id;
                preempted_publish        = true;
                /* 直接覆写；旧 sync 等待者 generation 不匹配 → TIMEOUT */
                slot = victim;
                fill_slot_locked(slot, cmd, waiting, request_id, &generation);
                break;
            }
            if (slot != NULL) {
                break;
            }
        }
    }

    pthread_mutex_unlock(&s_q_mutex);

    if (preempted_publish) {
        trace_context_t previous = trace_context_get();
        trace_context_t current  = previous;

        current.command_id     = preempted_request_id;
        current.correlation_id = (preempted_correlation_id != 0U) ? preempted_correlation_id : preempted_request_id;
        trace_context_set(&current);
        publish_cmd_handled(preempted_kind, DEV_CMD_STATUS_BUSY, OP_REJECT_NONE);
        trace_context_set(&previous);
    }

    if (out_request_id != NULL) {
        *out_request_id = request_id;
    }

    if (slot == NULL) {
        return SW_ERR_BUSY;
    }

    *out_slot       = slot;
    *out_generation = generation;
    return SW_OK;
}

static sw_err_t gateway_submit_async(const dev_cmd_t *cmd, uint64_t *request_id)
{
    cmd_gateway_slot_t *slot       = NULL;
    uint32_t            generation = 0U;
    uint64_t            rid        = 0U;
    sw_err_t            ret;

    ret = enqueue_command(cmd, false, &rid, &slot, &generation);
    if (ret != SW_OK) {
        if (request_id != NULL) {
            *request_id = rid;
        }
        return ret;
    }

    if (slot != NULL) {
        wake_control();
    }

    if (request_id != NULL) {
        *request_id = rid;
    }
    return SW_OK;
}

static sw_err_t gateway_submit_sync(const dev_cmd_t *cmd, dev_cmd_receipt_t *receipt, uint32_t timeout_ms)
{
    cmd_gateway_slot_t *slot          = NULL;
    uint32_t            my_generation = 0U;
    struct timespec     ts;
    int                 sem_ret;
    sw_err_t            ret;
    dev_cmd_receipt_t   local_receipt;
    uint32_t            wait_ms    = (timeout_ms > 0U) ? timeout_ms : CMD_GATEWAY_WAIT_MS;
    uint64_t            request_id = 0U;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_q_mutex);
    if (s_drain_thread_known && pthread_equal(pthread_self(), s_drain_thread)) {
        pthread_mutex_unlock(&s_q_mutex);
        LOG_ERROR("command_gateway: submit_sync 不可在 cmd_control 线程内调用（会自锁至超时）");
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_FAILED;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_STATE;
            receipt->request_id    = cmd->meta.request_id;
        }
        return SW_ERR_STATE;
    }
    pthread_mutex_unlock(&s_q_mutex);

    ret = enqueue_command(cmd, true, &request_id, &slot, &my_generation);
    if (ret != SW_OK) {
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_BUSY;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_BUSY;
            receipt->request_id    = request_id;
        }
        return ret;
    }

    if (slot == NULL) {
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_BUSY;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_BUSY;
            receipt->request_id    = request_id;
        }
        return SW_ERR_BUSY;
    }

    wake_control();

    time_util_fill_deadline(wait_ms, &ts);
    do {
        sem_ret = sem_timedwait(&slot->done, &ts);
    } while ((sem_ret != 0) && (errno == EINTR));

    if (sem_ret != 0) {
        release_slot_if_current(slot, my_generation);
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_TIMEOUT;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_TIMEOUT;
            receipt->request_id    = request_id;
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
            receipt->request_id    = request_id;
        }
        return SW_ERR_TIMEOUT;
    }

    local_receipt = slot->receipt;
    slot->used    = false;
    slot->waiting = false;
    pthread_mutex_unlock(&s_q_mutex);

    if (receipt != NULL) {
        *receipt = local_receipt;
    }

    return receipt_to_sw_err(&local_receipt);
}

static void process_one_slot(cmd_gateway_slot_t *slot)
{
    dev_cmd_t          cmd;
    uint32_t           generation;
    dev_cmd_decision_t decision;
    sw_err_t           effect_err = SW_OK;
    dev_cmd_receipt_t  receipt;
    trace_context_t    previous;
    trace_context_t    current;

    pthread_mutex_lock(&s_q_mutex);
    if (!slot->used) {
        pthread_mutex_unlock(&s_q_mutex);
        return;
    }
    generation = slot->generation;
    cmd        = slot->cmd;
    pthread_mutex_unlock(&s_q_mutex);

    previous                = trace_context_get();
    current.wash_session_id = cmd.meta.wash_session_id;
    current.command_id      = cmd.meta.request_id;
    current.correlation_id  = (cmd.meta.correlation_id != 0U) ? cmd.meta.correlation_id : cmd.meta.request_id;
    current.causation_id    = cmd.meta.causation_id;
    trace_context_set(&current);

    decision = op_mode_handle_command(&cmd);
    if (decision.verdict == OP_CMD_ALLOWED) {
        effect_err = side_effect_router_run(&cmd, decision.mode_before);
    }

    receipt.request_id    = cmd.meta.request_id;
    receipt.reject_reason = decision.reason;
    receipt.effect_error  = effect_err;
    receipt.status        = status_from_effect(effect_err, decision.verdict);

    publish_cmd_handled(cmd.body.kind, receipt.status, receipt.reject_reason);
    trace_context_set(&previous);

    /* 须持锁重读 waiting：裁决期间 sync 可能挂接到本槽（false→true）*/
    pthread_mutex_lock(&s_q_mutex);
    if (slot->used && (slot->generation == generation)) {
        slot->receipt = receipt;
        if (slot->waiting) {
            (void)sem_post(&slot->done);
        } else {
            slot->used    = false;
            slot->waiting = false;
        }
    }
    pthread_mutex_unlock(&s_q_mutex);
}

void command_gateway_drain(void)
{
    unsigned order[CMD_GATEWAY_QUEUE_SIZE];
    unsigned n = 0U;

    pthread_mutex_lock(&s_q_mutex);
    s_drain_thread       = pthread_self();
    s_drain_thread_known = true;
    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        if (s_queue[i].used && is_stop_priority(s_queue[i].cmd.body.kind)) {
            order[n++] = i;
        }
    }
    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        if (s_queue[i].used && !is_stop_priority(s_queue[i].cmd.body.kind)) {
            order[n++] = i;
        }
    }
    pthread_mutex_unlock(&s_q_mutex);

    for (unsigned j = 0; j < n; j++) {
        process_one_slot(&s_queue[order[j]]);
    }
}

static void *cmd_control_thread_fn(void *arg)
{
    (void)arg;

    for (;;) {
#ifdef WDF_UNIT_TEST
        if (s_test_ctrl_stop != 0) {
            break;
        }
#endif
        while (sem_wait(&s_wake) != 0) {
            if (errno != EINTR) {
                break;
            }
        }
#ifdef WDF_UNIT_TEST
        if (s_test_ctrl_stop != 0) {
            break;
        }
#endif
        command_gateway_drain();
    }

    return NULL;
}

sw_err_t command_gateway_init(void)
{
    s_next_request_id    = 0U;
    s_drain_thread_known = false;

    if (!s_wake_ready) {
        if (sem_init(&s_wake, 0, 0) != 0) {
            return SW_ERR_HW;
        }
        s_wake_ready = true;
    }

    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        if (sem_init(&s_queue[i].done, 0, 0) != 0) {
            return SW_ERR_HW;
        }
        s_queue[i].used       = false;
        s_queue[i].waiting    = false;
        s_queue[i].generation = 0U;
    }

#ifndef WDF_UNIT_TEST
    {
        sw_err_t reg = thread_register("cmd_control", cmd_control_thread_fn, SCHED_OTHER, 0, THD_CMD_CONTROL_STACK);
        if (reg != SW_OK) {
            LOG_ERROR("command_gateway: thread_register ret=%d", (int)reg);
            return reg;
        }
    }
#endif

    {
        static const device_command_port_ops_t s_ops = {
            .submit_async = gateway_submit_async,
            .submit_sync  = gateway_submit_sync,
        };
        sw_err_t ret = device_command_port_register(&s_ops);
        if (ret != SW_OK) {
            LOG_ERROR("command_gateway: device_command_port_register ret=%d", (int)ret);
            return ret;
        }
    }

    LOG_INFO("command_gateway: init ok (cmd_control)");
    return SW_OK;
}

#ifdef WDF_UNIT_TEST
sw_err_t command_gateway_start_control_for_test(void)
{
    if (s_test_ctrl_started) {
        return SW_OK;
    }
    if (!s_wake_ready) {
        return SW_ERR_NOT_INIT;
    }
    s_test_ctrl_stop = 0;
    if (pthread_create(&s_test_ctrl_tid, NULL, cmd_control_thread_fn, NULL) != 0) {
        return SW_ERR_HW;
    }
    s_test_ctrl_started = true;
    return SW_OK;
}

void command_gateway_stop_control_for_test(void)
{
    if (!s_test_ctrl_started) {
        return;
    }
    s_test_ctrl_stop = 1;
    wake_control();
    (void)pthread_join(s_test_ctrl_tid, NULL);
    s_test_ctrl_started = false;
    pthread_mutex_lock(&s_q_mutex);
    s_drain_thread_known = false;
    pthread_mutex_unlock(&s_q_mutex);
}
#endif
