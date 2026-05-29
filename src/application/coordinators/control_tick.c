#include "application/coordinators/control_tick.h"

#include "application/coordinators/control_context.h"
#include "application/use_cases/process_wash_trigger.h"
#include "domain/services/trigger_priority_service.h"
#include "shared/error_codes.h"
#include "shared/timeouts.h"

/**
 * @brief 判断待处理触发队列中是否已有超时触发。
 * @return 存在超时触发时返回 `true`，否则返回 `false`。
 */
static bool has_pending_timeout_trigger(void)
{
    const wash_trigger_event_t *wash_trigger_event;
    unsigned int index;

    for (index = 0; index < control_context_pending_trigger_count(); ++index)
    {
        wash_trigger_event = control_context_pending_trigger_at(index);
        if (wash_trigger_event != 0 && wash_trigger_event->trigger_type == TRIGGER_TYPE_TIMEOUT)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief 若等待条件已超时且条件允许，则向触发队列添加超时触发。
 * @details 添加条件：等待条件存在、已超时、队列未满、队列内无重复超时触发。
 */
static void enqueue_timeout_if_needed(void)
{
    const wait_condition_t *wait_condition;
    wash_trigger_event_t wash_trigger_event;
    unsigned long current_time_ms;

    wait_condition = control_context_wait_condition();
    current_time_ms = control_context_current_time_ms();
    if (wait_condition == 0 || !wait_condition_is_timed_out(wait_condition, current_time_ms))
    {
        return;
    }
    if (control_context_pending_trigger_count() >= MAX_PENDING_TRIGGER_COUNT ||
        has_pending_timeout_trigger())
    {
        return;
    }
    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_TIMEOUT, 0, wait_condition->reason_code,
                            "control-tick-timeout", current_time_ms);
    (void)control_context_append_trigger(&wash_trigger_event);
}

operation_result_t control_tick_submit_trigger(const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    result = control_context_require_active();
    if (!result.ok)
    {
        return result;
    }
    if (wash_trigger_event == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return control_context_append_trigger(wash_trigger_event);
}

void control_tick_advance_time(unsigned long elapsed_ms)
{
    if (!control_context_require_active().ok)
    {
        return;
    }
    control_context_advance_time(elapsed_ms);
}

operation_result_t control_tick_run(void)
{
    int best_index;
    unsigned int index;
    wash_trigger_event_t external_event;
    const wash_trigger_event_t *selected_event_ref;
    wash_trigger_event_t selected_event;
    const wash_trigger_event_t *candidate;
    const wash_trigger_event_t *best_candidate;
    operation_result_t result;

    result = control_context_require_active();
    if (!result.ok)
    {
        return result;
    }

    /*
     * 后台报警等跨线程事件已经在外部收件箱完成隔离，单拍内优先处理，
     * 避免内部待处理队列积压时延迟故障进入领域状态机。
     */
    if (control_context_try_pop_external_trigger(&external_event))
    {
        result = process_wash_trigger_execute(&external_event);
        if (!result.ok)
        {
            return result;
        }
        return process_wash_runtime_tick();
    }

    enqueue_timeout_if_needed();
    if (control_context_pending_trigger_count() > 0u)
    {
        best_index = 0;
        for (index = 1; index < control_context_pending_trigger_count(); ++index)
        {
            candidate = control_context_pending_trigger_at(index);
            best_candidate = control_context_pending_trigger_at((unsigned int)best_index);
            if (candidate != 0 && best_candidate != 0 &&
                trigger_priority_service_compare(candidate, best_candidate) > 0)
            {
                best_index = (int)index;
            }
        }

        selected_event_ref = control_context_pending_trigger_at((unsigned int)best_index);
        if (selected_event_ref == 0)
        {
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        selected_event = *selected_event_ref;
        control_context_remove_pending_trigger_at((unsigned int)best_index);
        result = process_wash_trigger_execute(&selected_event);
        if (!result.ok)
        {
            return result;
        }
    }

    return process_wash_runtime_tick();
}
