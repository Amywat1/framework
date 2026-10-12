/**
 * @file    safety_sim.h
 * @brief   安全端口仿真实现接口
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#ifndef ADAPTERS_OUTBOUND_SAFETY_SIM_SAFETY_SIM_H
#define ADAPTERS_OUTBOUND_SAFETY_SIM_SAFETY_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册安全端口的仿真实现
 * @retval SW_OK 注册成功
 * @note   须在 wiring 阶段调用，早于 scheduler 启动线程。
 */
sw_err_t safety_sim_register(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_SAFETY_SIM_SAFETY_SIM_H */
