#ifndef PLATFORM_LINUX_MAIN_LOOP_H
#define PLATFORM_LINUX_MAIN_LOOP_H

#include "application/coordinators/system_context.h"
#include "domain/model/wash_trigger_event.h"
#include "shared/result_types.h"

/**
 * @file main_loop.h
 * @brief Defines the single-tick controller runtime entry points.
 */

/**
 * @brief Executes one bounded controller tick.
 *
 * @param system_context Controller runtime composition root.
 * @return `operation_result_ok()` on success, otherwise a failure result.
 *
 * @note This function never waits on OS events and never owns an outer event
 * loop. The platform scheduler decides when to call it.
 */
operation_result_t main_loop_run(system_context_t *system_context);

/**
 * @brief Submits one trigger into the pending runtime queue.
 *
 * @param system_context Controller runtime composition root.
 * @param wash_trigger_event Trigger to enqueue.
 * @return `operation_result_ok()` on success, otherwise a failure result.
 */
operation_result_t main_loop_submit_trigger(system_context_t *system_context, const wash_trigger_event_t *wash_trigger_event);

/**
 * @brief Advances controller time by a bounded elapsed duration.
 *
 * @param system_context Controller runtime composition root.
 * @param elapsed_ms Elapsed time in milliseconds.
 *
 * @note The platform scheduler owns the time base and is the only caller that
 * should advance time during steady-state runtime.
 */
void main_loop_advance_time(system_context_t *system_context, unsigned long elapsed_ms);

#endif
