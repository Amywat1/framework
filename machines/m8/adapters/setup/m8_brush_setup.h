/**
 * @file    m8_brush_setup.h
 * @brief   M8 机型刷子接触器绑定与 domain 执行器注入
 * @author  HUWANGWEI
 * @date    2026-06-30
 */

#ifndef ADAPTERS_MACHINE_M8_BRUSH_SETUP_H
#define ADAPTERS_MACHINE_M8_BRUSH_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注入 M8 刷子接触器回调并初始化 domain/brush
 * @note   须在 hal_io 已 register 且 hal_io.init 之后调用
 */
sw_err_t m8_brush_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_BRUSH_SETUP_H */
