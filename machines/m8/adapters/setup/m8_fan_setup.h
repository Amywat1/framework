/**
 * @file    m8_fan_setup.h
 * @brief   M8 机型风机适配层初始化接口。
 *
 * @note    须在 hal_io 注册完成后调用。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_FAN_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_FAN_SETUP_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注入风机 IO 回调并初始化风机模块，同时将 fan_tick 注册到电机 tick 管理器。
 *
 * @return SW_OK 成功；SW_ERR_NOT_INIT hal_io 未注册；SW_ERR_HW tick 注册失败。
 */
sw_err_t m8_fan_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_FAN_SETUP_H */
