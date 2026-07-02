/**
 * @file    m8_lift_setup.h
 * @brief   M8 机型顶刷升降适配层初始化接口。
 *
 * @note    须在 hal_io 注册完成后调用。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_LIFT_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_LIFT_SETUP_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化升降继电器驱动、限位回调，并将 lift_tick 注册到电机 tick 管理器。
 *
 * @return SW_OK 成功；SW_ERR_NOT_INIT hal_io 未注册；SW_ERR_HW MCC 初始化失败。
 */
sw_err_t m8_lift_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_LIFT_SETUP_H */
