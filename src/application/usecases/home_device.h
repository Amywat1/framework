/**
 * @file    home_device.h
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef APPLICATION_USECASE_HOME_DEVICE_H
#define APPLICATION_USECASE_HOME_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/** @brief 发布 EVT_CMD_HOME_DEVICE，device_fsm 在 IDLE 时执行归位 */
sw_err_t home_device(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_USECASE_HOME_DEVICE_H */
