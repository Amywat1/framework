/**
 * @file    blackbox_recorder.h
 * @brief   通用故障黑匣子循环采样接口
 */

#ifndef OBSERVABILITY_RECORDER_BLACKBOX_RECORDER_H
#define OBSERVABILITY_RECORDER_BLACKBOX_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLACKBOX_SAMPLE_PAYLOAD_MAX 128U
#define BLACKBOX_SAMPLE_CAPACITY    2048U

/** @brief  黑匣子运行状态。 */
typedef enum {
    BLACKBOX_STATE_ARMED = 0,
    BLACKBOX_STATE_POST_TRIGGER,
    BLACKBOX_STATE_FROZEN,
} blackbox_state_t;

/** @brief  黑匣子配置。 */
typedef struct {
    uint16_t sample_rate_hz;
    uint16_t pre_trigger_seconds;
    uint16_t post_trigger_seconds;
} blackbox_config_t;

/** @brief  单条黑匣子采样。 */
typedef struct {
    uint64_t monotonic_ms;
    uint32_t sample_group;
    uint16_t payload_size;
    uint8_t  payload[BLACKBOX_SAMPLE_PAYLOAD_MAX];
} blackbox_sample_t;

/** @brief  已冻结故障窗口元数据。 */
typedef struct {
    uint64_t         incident_id;
    uint32_t         trigger_code;
    uint64_t         trigger_monotonic_ms;
    uint32_t         sample_count;
    blackbox_state_t state;
    uint64_t         dropped_busy_count;
    uint64_t         dropped_frozen_count;
} blackbox_snapshot_info_t;

/**
 * @brief  初始化并布防黑匣子。
 * @param  config 采样频率和前后窗口配置。
 * @retval SW_OK 初始化成功。
 * @retval SW_ERR_PARAM 配置非法或窗口超过固定容量。
 */
sw_err_t blackbox_recorder_init(const blackbox_config_t *config);

/**
 * @brief  非阻塞记录一条采样。
 * @param  sample_group 项目定义的采样组稳定编号。
 * @param  payload 采样载荷。
 * @param  payload_size 载荷字节数。
 * @retval SW_OK 写入成功。
 * @retval SW_ERR_OVERFLOW 锁繁忙、已冻结或容量配置不足。
 * @retval SW_ERR_PARAM 载荷非法。
 */
sw_err_t blackbox_recorder_record(uint32_t sample_group, const void *payload, size_t payload_size);

/**
 * @brief  触发故障窗口，开始记录后置采样。
 * @param  incident_id 故障唯一编号，不能为 0。
 * @param  trigger_code 触发事件或报警稳定编号。
 * @retval SW_OK 触发成功。
 * @retval SW_ERR_BUSY 已有故障窗口等待导出。
 * @retval SW_ERR_NOT_INIT 尚未初始化。
 */
sw_err_t blackbox_recorder_trigger(uint64_t incident_id, uint32_t trigger_code);

/**
 * @brief  获取当前故障窗口元数据。
 * @param  out 输出元数据。
 * @retval SW_OK 获取成功。
 * @retval SW_ERR_PARAM 输出指针为空。
 */
sw_err_t blackbox_recorder_get_info(blackbox_snapshot_info_t *out);

/**
 * @brief  按冻结窗口中的时间顺序复制一条采样。
 * @param  index 从 0 开始的采样索引。
 * @param  out 输出采样。
 * @retval SW_OK 获取成功。
 * @retval SW_ERR_BUSY 窗口尚未冻结。
 * @retval SW_ERR_NOT_FOUND 索引越界。
 */
sw_err_t blackbox_recorder_copy_sample(uint32_t index, blackbox_sample_t *out);

/**
 * @brief  确认故障窗口已导出并重新布防。
 * @param  incident_id 必须与当前冻结窗口一致。
 * @retval SW_OK 重新布防成功。
 * @retval SW_ERR_BUSY 窗口尚未冻结。
 * @retval SW_ERR_PARAM 故障编号不匹配。
 */
sw_err_t blackbox_recorder_release(uint64_t incident_id);

#ifdef __cplusplus
}
#endif

#endif /* OBSERVABILITY_RECORDER_BLACKBOX_RECORDER_H */
