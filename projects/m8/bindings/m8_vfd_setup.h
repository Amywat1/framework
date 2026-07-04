/**
 * @file    m8_vfd_setup.h
 * @brief   M8 机型 VFD 绑定初始化
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#ifndef ADAPTERS_MACHINE_M8_VFD_SETUP_H
#define ADAPTERS_MACHINE_M8_VFD_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  按 m8_vfd_table 绑定 VFD 实例并注册通信事件回调
 * @note   须在 hal_io.init() 与 hal_vfd.init() 之后调用
 */
sw_err_t m8_vfd_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_VFD_SETUP_H */
