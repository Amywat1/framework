/**
 * @file    lifetime.h
 * @brief   保养通道表：按是否在转累秒，或按边沿加次数，经 param_kv 落盘
 *
 * @note    可选。通道名、谓词和落盘键由项目登记。未 bind 则 poll 不记账。
 *          首拍只记下状态，不加时间、不加次数。
 */

#ifndef APPLICATION_LIFETIME_H
#define APPLICATION_LIFETIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief 单机最多通道数 */
#define LIFETIME_CHANNEL_MAX 16U
/** @brief 采样周期（毫秒） */
#define LIFETIME_POLL_MS 1000U
/** @brief 秒累计脏了之后的落盘间隔（毫秒） */
#define LIFETIME_SAVE_PERIOD_MS 60000U

/**
 * @brief  通道当前是否为真
 * @return true 在转，或边沿通道处于有效态
 */
typedef bool (*lifetime_active_fn_t)(void);

/**
 * @brief  一条保养通道
 */
typedef struct {
    const char          *kv_key;    /**< param_kv 键；项目命名 */
    lifetime_active_fn_t is_active; /**< 不可为空 */
    bool                 on_edge;   /**< false 累秒；true 记 false→true 次数 */
} lifetime_channel_t;

/**
 * @brief  登记通道表（只存指针，表须静态或与模块同寿）
 * @param  channels 通道行
 * @param  count    行数，1..LIFETIME_CHANNEL_MAX
 * @retval SW_OK           已登记
 * @retval SW_ERR_PARAM    表空、键空、谓词空或键重复
 * @retval SW_ERR_OVERFLOW 超过 LIFETIME_CHANNEL_MAX
 */
sw_err_t lifetime_bind(const lifetime_channel_t *channels, unsigned count);

/**
 * @brief  从 param_kv 装入累计值（幂等）
 * @retval SW_OK 已就绪
 */
sw_err_t lifetime_init(void);

/**
 * @brief  按墙钟推进累计
 * @param  now_ms 单调时钟毫秒
 */
void lifetime_poll(uint64_t now_ms);

/**
 * @brief  登记 1 s 采样周期（须在 scheduler_start_all 前）
 * @retval SW_OK 已登记
 * @retval 其它  周期任务表满或参数非法
 */
sw_err_t lifetime_register_task(void);

/**
 * @brief  读取一条通道的当前整数（秒或次数）
 * @param  kv_key 登记时的键
 * @param  out    输出，不可为空
 * @retval SW_OK           已写入
 * @retval SW_ERR_PARAM    kv_key 或 out 为空
 * @retval SW_ERR_NOT_INIT 尚未 init
 * @retval SW_ERR_NOT_FOUND 无此键
 */
sw_err_t lifetime_get_int(const char *kv_key, int32_t *out);

/**
 * @brief  清空模块状态，仅供测试隔离
 */
void lifetime_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_LIFETIME_H */
