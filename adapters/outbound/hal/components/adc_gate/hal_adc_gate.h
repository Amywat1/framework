/**
 * @file    hal_adc_gate.h
 * @brief   ADC 采样按需门控（按 board/port 区分）
 * @author  HUWANGWEI
 * @date    2026-07-27
 *
 * @note    用于避免"无人需要时仍周期性触发阻塞 ADC 读"：消费者在需要持续新鲜采样的
 *          时间窗口内调用 acquire/release 声明需求，采样生产者调用 is_needed 判断
 *          是否要真正执行一次 ADC 读。本组件只按 (board_id, port) 区分，不感知具体
 *          机构语义，可供任意 ADC 通道复用。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_COMPONENTS_ADC_GATE_HAL_ADC_GATE_H
#define ADAPTERS_OUTBOUND_HAL_COMPONENTS_ADC_GATE_HAL_ADC_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief  声明需要某 ADC 端口持续新鲜采样，计数 +1。
 * @param  board_id  子板号，从 1 开始。
 * @param  port      ADC 通道号。
 */
void hal_adc_gate_acquire(int board_id, int port);

/**
 * @brief  声明不再需要某 ADC 端口持续新鲜采样，计数 -1。
 * @param  board_id  子板号，从 1 开始。
 * @param  port      ADC 通道号。
 * @note   必须与 hal_adc_gate_acquire() 成对调用；多余的 release 不会使计数变为负数。
 */
void hal_adc_gate_release(int board_id, int port);

/**
 * @brief  查询某 ADC 端口当前是否有消费者需要新鲜采样。
 * @param  board_id  子板号，从 1 开始。
 * @param  port      ADC 通道号。
 * @retval true   至少一个消费者已声明需求。
 * @retval false  无人声明需求，或该端口从未被 acquire 过。
 */
bool hal_adc_gate_is_needed(int board_id, int port);

/**
 * @brief  清空全部门控计数（仅测试）
 */
void hal_adc_gate_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_COMPONENTS_ADC_GATE_HAL_ADC_GATE_H */
