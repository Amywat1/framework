/**
 * @file    m8_fan_setup.h
 * @brief   M8 机型风机适配层初始化接口。
 *
 * @note    须在 m8_motor_exec_init() 完成后调用。
 */
#ifndef MACHINES_M8_ADAPTERS_SETUP_M8_FAN_SETUP_H
#define MACHINES_M8_ADAPTERS_SETUP_M8_FAN_SETUP_H

#include "common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将共享执行器注入到 domain fan 层。
 *
 * @return SW_OK 成功；SW_ERR_NOT_INIT motor exec 未初始化。
 */
sw_err_t m8_fan_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* MACHINES_M8_ADAPTERS_SETUP_M8_FAN_SETUP_H */
