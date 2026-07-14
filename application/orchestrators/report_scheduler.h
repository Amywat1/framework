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

/**
 * @brief  注册点位索引解析器。
 *
 * @param  resolver 点位索引到物模型 id 的解析函数，可传 NULL 清除。
 * @note   用于 EVT_CLOUD_POINT_DIRTY 事件按索引生成增量上报 id。
 */
void report_scheduler_register_point_resolver(report_point_id_resolver_fn_t resolver);

/**
 * @brief  注册云端上报调度策略。
 *
 * @param  policies 上报策略数组。
 * @param  count 上报策略数量，不能超过内部容量。
 * @retval SW_OK 注册成功。
 * @retval SW_ERR_PARAM 策略为空、数量非法或周期策略冲突。
 * @retval 其他 周期任务或事件订阅注册失败。
 * @note   本函数只注册周期任务和事件订阅，后台线程由 scheduler 统一启动。
 */
sw_err_t report_scheduler_register(const report_policy_entry_t *policies, size_t count);

/**
 * @brief  请求立即执行一次全量重同步。
 *
 * @note   云端离线或 report port 未注册时静默跳过。
 */
void report_scheduler_request_resync(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ORCHESTRATORS_REPORT_SCHEDULER_H */
