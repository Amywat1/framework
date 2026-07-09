/**
 * @file    gantry_alarm_codes.h
 * @brief   龙门流程类报警码（与 doc/报警编码规范 §6.3 一致）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    部件编号与 M8 配置表 ALM_SENSE_GANTRY_* 对齐；
 *          机型侧须在 m8_alarm_init 做 _Static_assert 校验。
 */

#ifndef DOMAIN_MECHANISM_GANTRY_ALARM_CODES_H
#define DOMAIN_MECHANISM_GANTRY_ALARM_CODES_H

#include "framework/domain/safety/model/alarm_types.h"

#define GANTRY_ALM_ENC_ERR \
    ALARM_CODE_MAKE(ALM_C_SENSE, 1U, ALM_N_SIG_ERR)
#define GANTRY_ALM_FWD_TMO \
    ALARM_CODE_MAKE(ALM_C_SENSE, 2U, ALM_N_TIMEOUT)
#define GANTRY_ALM_REV_TMO \
    ALARM_CODE_MAKE(ALM_C_SENSE, 3U, ALM_N_TIMEOUT)

#endif /* DOMAIN_MECHANISM_GANTRY_ALARM_CODES_H */
