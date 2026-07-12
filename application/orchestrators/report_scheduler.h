/**
 * @file    report_scheduler.h
 * @brief   云端状态上报调度器接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef APPLICATION_ORCHESTRATORS_REPORT_SCHEDULER_H
#define APPLICATION_ORCHESTRATORS_REPORT_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    REPORT_TRIGGER_PERIODIC = 0,
    REPORT_TRIGGER_EVENT,
} report_trigger_kind_t;

typedef struct {
    report_trigger_kind_t kind;
    uint32_t              period_ms;
    uint32_t              event_id;
    bool                  full;
    bool                  use_event_param_as_point_index;
    const char *const    *delta_ids;
    size_t                delta_id_count;
} report_policy_entry_t;

/**
 * @brief  点位表索引 -> 物模型 id（EVT_CLOUD_POINT_DIRTY 时使用）
 */
typedef const char *(*report_point_id_resolver_fn_t)(uint32_t index);

void     report_scheduler_register_point_resolver(report_point_id_resolver_fn_t resolver);
sw_err_t report_scheduler_init(const report_policy_entry_t *policies, size_t count);
void     report_scheduler_request_resync(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_REPORT_SCHEDULER_H */
