/**
 * @file    demo_defs.h
 * @brief   Demo 共享常量（报警码等）
 *
 * @note    产品项目用自己的目录生成宏，不要复制这些码。
 */

#ifndef DEMO_WIRING_DEMO_DEFS_H
#define DEMO_WIRING_DEMO_DEFS_H

#include "domain/safety/model/alarm_types.h"

/** @brief Demo MAJOR：检测对象过载，锁存，用于 smoke 主动 trigger */
#define DEMO_ALARM_MAJOR ALARM_CODE_MAKE(ALM_C_DETECT, 11U, ALM_N_OVERLOAD)

/** @brief Demo 急停：检测对象安全触发，跟随信号，由 estop_alarm_bridge 投影 */
#define DEMO_ALARM_ESTOP ALARM_CODE_MAKE(ALM_C_DETECT, 99U, ALM_N_SAFETY_TRIGGER)

#endif /* DEMO_WIRING_DEMO_DEFS_H */
