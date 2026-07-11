/**
 * @file    safety_projection.h
 * @brief   报警/安全 → safety_snapshot 投影
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef APPLICATION_SAFETY_PROJECTION_H
#define APPLICATION_SAFETY_PROJECTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

sw_err_t safety_projection_init(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SAFETY_PROJECTION_H */
