/**
 * @file    estop_poll_thread.h
 * @brief   急停轮询采集线程（可选入站适配器）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    定位：这是"如何采集急停"的一种实现，属于入站适配器而非运行时核心。
 *          它以 SCHED_FIFO 高优先级轮询安全端口的 estop_is_active()，经确认滤波
 *          后在边沿处调用 cutout 并发布 EVT_HW_ESTOP_ON/OFF，不直接访问 OperationalMode。
 *
 * @note    推荐由本采集器独占急停热路径：项目只提供引脚、极性和切断实现。
 *          若项目另有采集通路并发布同样的 EVT_HW_ESTOP_*，则不要再链接本文件。
 */

#ifndef ADAPTERS_INBOUND_SAFETY_ESTOP_POLL_THREAD_H
#define ADAPTERS_INBOUND_SAFETY_ESTOP_POLL_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 急停确认滤波配置
 *
 * @note   确认时间按墙钟计算，不按轮询次数。采集周期短于 IO 刷新时，
 *         同一帧会被多次读到，按次数确认会把单帧毛刺当成有效边沿。
 * @note   触发与释放分开：按下尽快确认，松开更稳，避免抖动误恢复。
 */
typedef struct {
    uint32_t confirm_on_ms;  /**< 触发确认时间（ms）；0 表示当前采样立即确认 */
    uint32_t confirm_off_ms; /**< 释放确认时间（ms）；0 表示当前采样立即确认 */
} estop_poll_cfg_t;

/**
 * @brief 确认滤波输出事件
 */
typedef enum {
    ESTOP_FILTER_HOLD = 0,     /**< 尚未确认，或确认态未变化 */
    ESTOP_FILTER_CONFIRMED_ON, /**< 确认急停有效 */
    ESTOP_FILTER_CONFIRMED_OFF /**< 确认急停解除 */
} estop_filter_event_t;

/**
 * @brief 急停确认滤波器运行态
 */
typedef struct {
    estop_poll_cfg_t cfg;                /**< 确认时间 */
    bool             started;            /**< 是否已收到首个采样 */
    bool             candidate_active;   /**< 当前候选电平 */
    bool             output_valid;       /**< 是否已产生过确认输出 */
    bool             output_active;      /**< 已确认的急停状态 */
    uint64_t         candidate_since_ms; /**< 候选电平起始时间 */
} estop_filter_t;

/**
 * @brief  复位确认滤波器
 * @param  filter  滤波器；NULL 则忽略
 * @param  cfg     确认时间；NULL 视为 0/0（立即确认）
 */
void estop_filter_reset(estop_filter_t *filter, const estop_poll_cfg_t *cfg);

/**
 * @brief  喂入一次原始采样并推进确认
 * @param  filter      滤波器
 * @param  raw_active  本次原始采样是否有效
 * @param  now_ms      当前毫秒时间戳
 * @return 确认事件；参数非法时返回 HOLD
 *
 * @note   上电首个确认若为无效，不产生 OFF 事件。
 */
estop_filter_event_t estop_filter_feed(estop_filter_t *filter, bool raw_active, uint64_t now_ms);

/**
 * @brief  注册急停轮询线程到线程表（须在 scheduler_start_all 之前调用）
 * @param  cfg  确认滤波配置；NULL 视为立即确认（与历史行为兼容）
 * @retval SW_OK           已登记
 * @retval SW_ERR_OVERFLOW 线程表已满
 * @note   依赖安全端口已注册；未注册时轮询读到的急停状态恒为 false。
 */
sw_err_t estop_poll_thread_init(const estop_poll_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_INBOUND_SAFETY_ESTOP_POLL_THREAD_H */
