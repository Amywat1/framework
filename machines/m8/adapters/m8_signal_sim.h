/**
 * @file    m8_signal_sim.h
 * @brief   M8 信号仿真注入接口（供 tools/simulator/ 与场景测试使用）
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#ifndef ADAPTERS_MACHINE_M8_SIGNAL_SIM_H
#define ADAPTERS_MACHINE_M8_SIGNAL_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "machines/m8/config/m8_signal_table.h"
#include <stdbool.h>

/**
 * @brief  设置指定 M8 信号的仿真 DI 逻辑态
 * @param  sig_id  信号标识
 * @param  active  true=逻辑触发态，false=逻辑释放态
 * @note   写入 hal_io_sim 原始电平，极性由 m8_signal_table 决定
 */
void m8_signal_sim_set_active(m8_signal_id_t sig_id, bool active);

void m8_signal_sim_set_fwd_limit(bool active);
void m8_signal_sim_set_rev_limit(bool active);
void m8_signal_sim_set_lift_top(bool active);
void m8_signal_sim_set_lift_bottom(bool active);
void m8_signal_sim_set_estop(bool active);
void m8_signal_sim_set_rear_lock_home(bool active);

/** @brief  将全部信号置为逻辑释放态 */
void m8_signal_sim_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_SIGNAL_SIM_H */
