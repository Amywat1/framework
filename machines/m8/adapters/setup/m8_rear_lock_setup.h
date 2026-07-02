/**
 * @file    m8_rear_lock_setup.h
 * @brief   M8 机型后轮锁止适配层初始化接口。
 *
 * @note    须在 hal_io 注册完成后调用。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_REAR_LOCK_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_REAR_LOCK_SETUP_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化后轮锁止继电器驱动、限位回调，并将 rear_lock_tick 注册到电机 tick 管理器。
 *
 * @return SW_OK 成功；SW_ERR_NOT_INIT hal_io 未注册；SW_ERR_HW MCC 初始化失败。
 */
sw_err_t m8_rear_lock_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_REAR_LOCK_SETUP_H */
