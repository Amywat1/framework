/**
 * @file    wiring.h
 * @brief   依赖注入接口（将 adapter 实现注册到 port 接口表）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    真机接线：wiring.c
 *          仿真接线：wiring_sim.c（CMake BUILD_SIM=ON 时替换）
 */

#ifndef CORE_BOOTSTRAP_WIRING_H
#define CORE_BOOTSTRAP_WIRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  完成所有 port → adapter 的注册（依赖注入）
 *         在 event_bus_init() 之后、任何业务模块 init() 之前调用。
 * @retval SW_OK / SW_ERR_HW（硬件初始化失败）
 */
sw_err_t wiring(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_BOOTSTRAP_WIRING_H */
