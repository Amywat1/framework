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
#include "common/time_util.h"
#include "domain/op_mode/operational_mode.h"
#include "ports/inbound/command/command_port.h"
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
static uint64_t           s_next_request_id;

/* drain 所在线程（即 event dispatch 线程）标识。
 * 用于拦截"从 dispatch 线程自身调用 submit"这种必然自锁的误用：
 * 此时 wake 事件排在队列里，drain 要等当前 handler 返回才可能执行，
 * 而 handler 正阻塞在 sem_timedwait 上，只能白等到超时。 */
static pthread_t s_drain_thread;
static bool      s_drain_thread_known;

static void publish_cmd_handled(dev_cmd_kind_t kind, dev_cmd_status_t status, op_reject_reason_t reason)
{
    (void)event_publish(EVT_OP_MODE_CMD_HANDLED, cmd_handled_evt_param((uint8_t)kind, status, reason));
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
    uint64_t            request_id;

    if (cmd == NULL) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_q_mutex);
    if (s_drain_thread_known && pthread_equal(pthread_self(), s_drain_thread)) {
        pthread_mutex_unlock(&s_q_mutex);
        LOG_ERROR("command_gateway: submit 不可在 event dispatch 线程内调用（会自锁至超时）");
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_FAILED;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_STATE;
            receipt->request_id    = cmd->meta.request_id;
        }
        return SW_ERR_STATE;
    }

    request_id = (cmd->meta.request_id != 0U) ? cmd->meta.request_id : ++s_next_request_id;
    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        if (!s_queue[i].used) {
            slot                        = &s_queue[i];
            slot->used                  = true;
            my_generation               = ++s_generation;
            slot->generation            = my_generation;
            slot->cmd                   = *cmd;
            slot->cmd.meta.request_id   = request_id;
            slot->receipt.status        = DEV_CMD_STATUS_BUSY;
            slot->receipt.reject_reason = OP_REJECT_NONE;
            slot->receipt.effect_error  = SW_ERR_BUSY;
            slot->receipt.request_id    = request_id;
            /* 排空上一任槽主可能残留的信号量计数。
             * 残留来源：前一次 submit 超时释放槽位后，drain 才执行 sem_post。
             * 若不排空，本次 sem_timedwait 会立即返回并读到占位 receipt，
             * 导致命令被静默丢弃且调用方收到 SW_ERR_BUSY。 */
            while (sem_trywait(&slot->done) == 0) {
                /* 循环直至计数归零 */
            }
            break;
        }
    }
    pthread_mutex_unlock(&s_q_mutex);

    if (slot == NULL) {
        if (receipt != NULL) {
            receipt->status        = DEV_CMD_STATUS_BUSY;
            receipt->reject_reason = OP_REJECT_NONE;
            receipt->effect_error  = SW_ERR_BUSY;
            receipt->request_id    = request_id;
        }
        return SW_ERR_BUSY;
    }

    if (event_publish(EVT_CMD_GATEWAY_WAKE, 0U) != SW_OK) {
        release_slot_if_current(slot, my_generation);
        return SW_ERR_BUSY;
    }

    time_util_fill_deadline(wait_ms, &ts);
    /* EINTR 重试：信号打断不应被误判为命令超时。
     * 截止时间是绝对值，重试不会延长总等待时长。 */
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
    pthread_mutex_unlock(&s_q_mutex);

    if (receipt != NULL) {
        *receipt = local_receipt;
    }

    ret = receipt_to_sw_err(&local_receipt);
    return ret;
}

void command_gateway_drain(void)
{
    /* 记录 drain 线程身份，供 submit 侧拦截同线程重入 */
    pthread_mutex_lock(&s_q_mutex);
    s_drain_thread       = pthread_self();
    s_drain_thread_known = true;
    pthread_mutex_unlock(&s_q_mutex);

    for (unsigned i = 0; i < CMD_GATEWAY_QUEUE_SIZE; i++) {
        cmd_gateway_slot_t *slot = &s_queue[i];
        dev_cmd_t           cmd;
        uint32_t            generation;
        dev_cmd_decision_t  decision;
        sw_err_t            effect_err = SW_OK;
        dev_cmd_receipt_t   receipt;
        trace_context_t     previous;
        trace_context_t     current;

        pthread_mutex_lock(&s_q_mutex);
        if (!slot->used) {
            pthread_mutex_unlock(&s_q_mutex);
            continue;
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
            effect_err = side_effect_router_run(decision.pending_effect, &cmd);
        }

        receipt.request_id    = cmd.meta.request_id;
        receipt.reject_reason = decision.reason;
        receipt.effect_error  = effect_err;
        receipt.status        = status_from_effect(effect_err, decision.verdict);

        publish_cmd_handled(cmd.body.kind, receipt.status, receipt.reject_reason);

        trace_context_set(&previous);
        /* sem_post 必须在持锁内完成：
         * 若先解锁再 post，submit 侧可能正好在这个窗口里超时释放槽位，
         * 使信号量计数残留给下一任槽主。sem_post 不阻塞，持锁开销可忽略。 */
        pthread_mutex_lock(&s_q_mutex);
        if (slot->used && (slot->generation == generation)) {
            slot->receipt = receipt;
            (void)sem_post(&slot->done);
        }
        pthread_mutex_unlock(&s_q_mutex);
    }
}

sw_err_t command_gateway_init(void)
{
    static const event_subscription_t s_subs[] = {
        {EVT_CMD_GATEWAY_WAKE, on_gateway_wake},
    };

    s_next_request_id    = 0U;
    s_drain_thread_known = false;
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

    ret = device_command_port_register(&s_ops);
    if (ret != SW_OK) {
        LOG_ERROR("command_gateway: device_command_port_register ret=%d", (int)ret);
        return ret;
    }
    LOG_INFO("command_gateway: init ok");
    return SW_OK;
}
