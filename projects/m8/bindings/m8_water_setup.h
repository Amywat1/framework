/**
 * @file    m8_water_setup.h
 * @brief   M8 机型水路槽位绑定与 domain 执行器注入
 * @author  HUWANGWEI
 * @date    2026-06-07
 */

#ifndef ADAPTERS_MACHINE_M8_WATER_SETUP_H
#define ADAPTERS_MACHINE_M8_WATER_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  绑定 M8 水路槽位并初始化 domain/water 执行器
 * @note   须在 hal_io 已 register 且（真机）hal_io.init 之后调用
 */
sw_err_t m8_water_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_WATER_SETUP_H */
