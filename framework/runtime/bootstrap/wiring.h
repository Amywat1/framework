/**
 * @file    wiring.h
 * @brief   依赖接线接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    真机构建使用 wiring.c。
 *          仿真构建使用 wiring_sim.c。
 */

#ifndef CORE_BOOTSTRAP_WIRING_H
#define CORE_BOOTSTRAP_WIRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

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
