/**
 * @file    hal_pulse_gate.h
 * @brief   脉冲计数器采样按需门控（按 board/pin 区分）
 * @author  HUWANGWEI
 * @date    2026-08-27
 *
 * @note    与 ADC 门控同形：消费者在需要新鲜计数的时间窗口内 acquire/release，
 *          worker 仅对 is_needed 的引脚做 SDO。本组件不感知机构语义。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_COMPONENTS_PULSE_GATE_HAL_PULSE_GATE_H
#define ADAPTERS_OUTBOUND_HAL_COMPONENTS_PULSE_GATE_HAL_PULSE_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief  声明需要某 DI 脉冲计数持续新鲜采样，计数 +1。
 * @param  board_id  子板号，从 1 开始。
 * @param  pin_id    DI 引脚号。
 */
void hal_pulse_gate_acquire(int board_id, int pin_id);

/**
 * @brief  声明不再需要某 DI 脉冲计数持续新鲜采样，计数 -1。
 * @param  board_id  子板号，从 1 开始。
 * @param  pin_id    DI 引脚号。
 * @note   必须与 hal_pulse_gate_acquire() 成对调用；多余的 release 不会使计数变为负数。
 */
void hal_pulse_gate_release(int board_id, int pin_id);

/**
 * @brief  查询某 DI 脉冲计数当前是否有消费者需要新鲜采样。
 * @param  board_id  子板号，从 1 开始。
 * @param  pin_id    DI 引脚号。
 * @retval true   至少一个消费者已声明需求。
 * @retval false  无人声明需求，或该引脚从未被 acquire 过。
 */
bool hal_pulse_gate_is_needed(int board_id, int pin_id);

/**
 * @brief  清空全部门控计数（仅测试）
 */
void hal_pulse_gate_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_COMPONENTS_PULSE_GATE_HAL_PULSE_GATE_H */
