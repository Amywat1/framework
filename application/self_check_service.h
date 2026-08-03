/**
 * @file    self_check_service.h
 * @brief   完整自检流程协调（阶段一最小实现）
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#ifndef APPLICATION_SELF_CHECK_SERVICE_H
#define APPLICATION_SELF_CHECK_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

sw_err_t self_check_service_init(void);
void     self_check_service_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SELF_CHECK_SERVICE_H */
