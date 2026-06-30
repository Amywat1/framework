/**
 * @file    m8_gantry_setup.h
 * @brief   M8 机型龙门执行器绑定入口
 */

#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_GANTRY_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_GANTRY_SETUP_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  绑定龙门执行器（motor 层回调）并初始化 gantry domain
 */
sw_err_t m8_gantry_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_GANTRY_SETUP_H */
