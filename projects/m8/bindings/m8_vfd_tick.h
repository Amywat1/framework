/**
 * @file    m8_vfd_tick.h
 * @brief   M8 VFD periodic runtime task registration.
 */

#ifndef PROJECTS_M8_BINDINGS_M8_VFD_TICK_H
#define PROJECTS_M8_BINDINGS_M8_VFD_TICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

sw_err_t m8_vfd_tick_register_task(void);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_BINDINGS_M8_VFD_TICK_H */
