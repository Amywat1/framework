/**
 * @file    m8_fan_setup.c
 * @brief   M8 机型风机适配层：将共享执行器注入到 domain fan 层。
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    风机 VFD 硬件回调已由 m8_motor_exec.c 统一实现。
 */

#include "machines/m8/adapters/setup/m8_fan_setup.h"
#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "domain/device/mechanism/fan.h"
#include "common/log.h"

sw_err_t m8_fan_setup(void)
{
    sw_err_t ret;

    if (m8_motor_exec_get() == NULL) {
        LOG_ERROR("m8_fan_setup: motor exec not initialized");
        return SW_ERR_NOT_INIT;
    }

    ret = fan_init(m8_motor_exec_get(), M8_MOTOR_FAN);
    if (ret != SW_OK) {
        LOG_ERROR("m8_fan_setup: fan_init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_fan_setup ok");
    return SW_OK;
}
