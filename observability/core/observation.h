/**
 * @file    observation.h
 * @brief   通用可观测记录非阻塞发布接口
 */

#ifndef OBSERVABILITY_CORE_OBSERVATION_H
#define OBSERVABILITY_CORE_OBSERVATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "common/trace_context.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OBSERVATION_SCHEMA_VERSION          1U
#define OBSERVATION_SOURCE_MAX              32U
#define OBSERVATION_PAYLOAD_MAX             256U
#define OBSERVATION_NORMAL_QUEUE_CAPACITY   256U
#define OBSERVATION_CRITICAL_QUEUE_CAPACITY 32U

/** @brief  观测记录种类。 */
typedef enum {
    OBSERVATION_RECORD_EVENT = 0,
    OBSERVATION_RECORD_LOG,
    OBSERVATION_RECORD_STATUS,
    OBSERVATION_RECORD_TREND,
    OBSERVATION_RECORD_INCIDENT,
} observation_record_kind_t;

/** @brief  观测记录严重度。 */
typedef enum {
    OBSERVATION_SEVERITY_DEBUG = 0,
    OBSERVATION_SEVERITY_INFO,
    OBSERVATION_SEVERITY_WARN,
    OBSERVATION_SEVERITY_ERROR,
    OBSERVATION_SEVERITY_CRITICAL,
} observation_severity_t;

/** @brief  载荷编码格式。 */
typedef enum {
    OBSERVATION_PAYLOAD_NONE = 0,
    OBSERVATION_PAYLOAD_TEXT,
    OBSERVATION_PAYLOAD_JSON,
    OBSERVATION_PAYLOAD_PROTOBUF,
    OBSERVATION_PAYLOAD_BINARY,
} observation_payload_format_t;

/**
 * @brief  当前业务关联上下文。
 * @note   与 common/trace_context.h 的 trace_context_t 是同一模型（同名同序四字段），
 *         此处以别名复用而非重复定义，避免两套关联标识各自演进后失同步。
 *         两者的差别只在存储方式：trace_context 是线程局部的“当前链路”，
 *         本模块的 s_context 是可被显式设置的“记录默认上下文”。
 */
typedef trace_context_t observation_context_t;

/** @brief  发布方提供的记录字段。 */
typedef struct {
    observation_record_kind_t    kind;
    observation_severity_t       severity;
    observation_payload_format_t payload_format;
    uint32_t                     event_code;
    const char                  *source;
    const void                  *payload;
    size_t                       payload_size;
    const observation_context_t *context;
} observation_record_spec_t;

/** @brief  队列中保存的完整记录。 */
typedef struct {
    uint16_t                     schema_version;
    observation_record_kind_t    kind;
    observation_severity_t       severity;
    observation_payload_format_t payload_format;
    uint32_t                     event_code;
    uint64_t                     boot_id;
    uint64_t                     sequence;
    uint64_t                     wall_time_ms;
    uint64_t                     monotonic_ms;
    observation_context_t        context;
    char                         source[OBSERVATION_SOURCE_MAX];
    uint16_t                     payload_size;
    uint8_t                      payload[OBSERVATION_PAYLOAD_MAX];
} observation_record_t;

/** @brief  可观测核心运行统计。 */
typedef struct {
    uint64_t published_count;
    uint64_t popped_count;
    uint64_t dropped_busy_count;
    uint64_t dropped_normal_full_count;
    uint64_t dropped_critical_full_count;
    uint32_t normal_queue_depth;
    uint32_t normal_queue_peak;
    uint32_t critical_queue_depth;
    uint32_t critical_queue_peak;
} observation_stats_t;

/**
 * @brief  初始化可观测核心。
 * @param  boot_id 本次进程启动的唯一编号，不能为 0。
 * @retval SW_OK 初始化成功。
 * @retval SW_ERR_PARAM 启动编号非法。
 */
sw_err_t observation_init(uint64_t boot_id);

/**
 * @brief  查询可观测核心是否已初始化。
 * @return true 表示已初始化，发布的记录会真正入队。
 * @note   可观测是旁路设施，项目可以选择不启用。装配层用本接口判断是否
 *         需要挂接事件桥接，避免在未启用观测的项目里做无用订阅。
 */
bool observation_is_ready(void);

/**
 * @brief  设置后续记录使用的默认业务上下文。
 * @param  context 上下文；传 NULL 清空。
 */
void observation_context_set(const observation_context_t *context);

/**
 * @brief  获取当前默认业务上下文副本。
 * @return 当前上下文。
 */
observation_context_t observation_context_get(void);

/**
 * @brief  非阻塞发布一条观测记录。
 * @param  spec 记录字段和载荷。
 * @retval SW_OK 发布成功。
 * @retval SW_ERR_PARAM 字段或载荷非法。
 * @retval SW_ERR_NOT_INIT 尚未初始化。
 * @retval SW_ERR_OVERFLOW 队列繁忙或容量已满，记录被丢弃。
 * @note   本接口不执行动态内存、文件、网络或数据库操作。
 */
sw_err_t observation_publish(const observation_record_spec_t *spec);

/**
 * @brief  非阻塞获取下一条记录，关键队列优先。
 * @param  out 输出记录。
 * @retval SW_OK 成功获取。
 * @retval SW_ERR_NOT_FOUND 当前没有记录。
 * @retval SW_ERR_PARAM 输出指针为空。
 */
sw_err_t observation_try_pop(observation_record_t *out);

/**
 * @brief  获取运行统计副本。
 * @param  out 输出统计。
 * @retval SW_OK 获取成功。
 * @retval SW_ERR_PARAM 输出指针为空。
 */
sw_err_t observation_get_stats(observation_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OBSERVABILITY_CORE_OBSERVATION_H */
