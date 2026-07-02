/**
 * @file    m8_gantry_setup.c
 * @brief   M8 机型龙门行走适配层：将共享执行器注入到 domain gantry 层。
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    所有硬件回调（VFD、编码器、限位）已由 m8_motor_exec.c 统一实现。
 */

#include "machines/m8/adapters/setup/m8_gantry_setup.h"
#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "domain/device/mechanism/gantry.h"
#include "common/log.h"

sw_err_t m8_gantry_setup(void)
{
    sw_err_t ret;

    if (m8_motor_exec_get() == NULL) {
        LOG_ERROR("m8_gantry_setup: motor exec not initialized");
        return SW_ERR_NOT_INIT;
    }

    ret = gantry_init(m8_motor_exec_get(), M8_MOTOR_GANTRY);
    if (ret != SW_OK) {
        LOG_ERROR("m8_gantry_setup: gantry_init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_gantry_setup ok");
    return SW_OK;
}
