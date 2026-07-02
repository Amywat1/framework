/**
 * @file    m8_lift_setup.c
 * @brief   M8 机型顶刷升降适配层：将共享执行器注入到 domain lift 层。
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    继电器驱动回调（TOP_BRUSH_UP/DOWN）已由 m8_motor_exec.c 统一实现。
 */

#include "machines/m8/adapters/setup/m8_lift_setup.h"
#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "domain/device/mechanism/lift.h"
#include "common/log.h"

sw_err_t m8_lift_setup(void)
{
    sw_err_t ret;

    if (m8_motor_exec_get() == NULL) {
        LOG_ERROR("m8_lift_setup: motor exec not initialized");
        return SW_ERR_NOT_INIT;
    }

    ret = lift_init(m8_motor_exec_get(), M8_MOTOR_LIFT);
    if (ret != SW_OK) {
        LOG_ERROR("m8_lift_setup: lift_init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_lift_setup ok");
    return SW_OK;
}
