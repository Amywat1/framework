#include "application/coordinators/control_tick.h"

#include "application/coordinators/device_runtime.h"
#include "application/use_cases/process_wash_trigger.h"
#include "domain/services/trigger_priority_service.h"
#include "shared/error_codes.h"
#include "shared/timeouts.h"

/**
 * @brief 判断待处理触发队列中是否已有超时触发。
 * @param device_runtime 系统上下文。
 * @return 存在超时触发时返回 `true`，否则返回 `false`。
 */
static bool has_pending_timeout_trigger(const device_runtime_t device_runtime)
{
    const wash_trigger_event_t *wash_trigger_event;
    unsigned int index;

    for (index = 0; index < device_runtime_pending_trigger_count(device_runtime); ++index)
    {
        wash_trigger_event = device_runtime_pending_trigger_at(device_runtime, index);
        if (wash_trigger_event != 0 && wash_trigger_event->trigger_type == TRIGGER_TYPE_TIMEOUT)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief 若等待条件已超时且条件允许，则向触发队列添加超时触发。
 * @param device_runtime 系统上下文。
 * @details 添加条件：等待条件存在、已超时、队列未满、队列内无重复超时触发。
 */
static void enqueue_timeout_if_needed(device_runtime_t device_runtime)
{
    const wait_condition_t *wait_condition;
    wash_trigger_event_t wash_trigger_event;
    unsigned long current_time_ms;

    wait_condition = device_runtime_wait_condition(device_runtime);
    current_time_ms = device_runtime_current_time_ms(device_runtime);
    if (wait_condition == 0 || !wait_condition_is_timed_out(wait_condition, current_time_ms))
    {
        return;
    }
    if (device_runtime_pending_trigger_count(device_runtime) >= MAX_PENDING_TRIGGER_COUNT ||
        has_pending_timeout_trigger(device_runtime))
    {
        return;
    }
    wash_trigger_event_init(&wash_trigger_event, TRIGGER_TYPE_TIMEOUT, 0, wait_condition->reason_code,
                            "control-tick-timeout", current_time_ms);
    (void)device_runtime_append_trigger(device_runtime, &wash_trigger_event);
}

operation_result_t control_tick_submit_trigger(device_runtime_t device_runtime,
                                            const wash_trigger_event_t *wash_trigger_event)
{
    operation_result_t result;

    result = device_runtime_require_active(device_runtime);
    if (!result.ok)
    {
        return result;
    }
    if (wash_trigger_event == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return device_runtime_append_trigger(device_runtime, wash_trigger_event);
}

void control_tick_advance_time(device_runtime_t device_runtime, unsigned long elapsed_ms)
{
    if (!device_runtime_require_active(device_runtime).ok)
    {
        return;
    }
    device_runtime_advance_time(device_runtime, elapsed_ms);
}

operation_result_t control_tick_run(device_runtime_t device_runtime)
{
    int best_index;
    unsigned int index;
    wash_trigger_event_t external_event;
    const wash_trigger_event_t *selected_event_ref;
    wash_trigger_event_t selected_event;
    const wash_trigger_event_t *candidate;
    const wash_trigger_event_t *best_candidate;
    operation_result_t result;

    result = device_runtime_require_active(device_runtime);
    if (!result.ok)
    {
        return result;
    }

    /*
     * 后台报警等跨线程事件已经在外部收件箱完成隔离，单拍内优先处理，
     * 避免内部待处理队列积压时延迟故障进入领域状态机。
     */
    if (device_runtime_try_pop_external_trigger(device_runtime, &external_event))
    {
        result = process_wash_trigger_execute(device_runtime, &external_event);
        if (!result.ok)
        {
            return result;
        }
        return process_wash_runtime_tick(device_runtime);
    }

    enqueue_timeout_if_needed(device_runtime);
    if (device_runtime_pending_trigger_count(device_runtime) > 0u)
    {
        best_index = 0;
        for (index = 1; index < device_runtime_pending_trigger_count(device_runtime); ++index)
        {
            candidate = device_runtime_pending_trigger_at(device_runtime, index);
            best_candidate = device_runtime_pending_trigger_at(device_runtime, (unsigned int)best_index);
            if (candidate != 0 && best_candidate != 0 &&
                trigger_priority_service_compare(candidate, best_candidate) > 0)
            {
                best_index = (int)index;
            }
        }

        selected_event_ref = device_runtime_pending_trigger_at(device_runtime, (unsigned int)best_index);
        if (selected_event_ref == 0)
        {
            return operation_result_fail(ERROR_CODE_INVALID_STATE);
        }
        selected_event = *selected_event_ref;
        device_runtime_remove_pending_trigger_at(device_runtime, (unsigned int)best_index);
        result = process_wash_trigger_execute(device_runtime, &selected_event);
        if (!result.ok)
        {
            return result;
        }
    }

    return process_wash_runtime_tick(device_runtime);
}
