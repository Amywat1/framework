/**
 * @file    stop_wash.h
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef APPLICATION_USECASE_STOP_WASH_H
#define APPLICATION_USECASE_STOP_WASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/** @brief 中止当前洗车（发布 EVT_CMD_STOP_WASH）*/
sw_err_t stop_wash(void);

/** @brief 停止运营（发布 EVT_CMD_STOP_OPERATION）*/
sw_err_t stop_operation(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_USECASE_STOP_WASH_H */
