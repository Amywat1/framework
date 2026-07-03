/**
 * @file    m8_brush_setup.c
 * @brief   M8 机型刷子适配层：将共享执行器注入到 domain brush 层。
 * @author  HUWANGWEI
 * @date    2026-07-03
 *
 * @note    VFD 驱动回调与接触器切换时序已由 m8_motor_exec.c 统一实现。
 */

#include "machines/m8/adapters/setup/m8_brush_setup.h"
#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "domain/device/mechanism/brush.h"
#include "common/log.h"

sw_err_t m8_brush_setup(void)
{
    sw_err_t ret;

    if (m8_motor_exec_get() == NULL) {
        LOG_ERROR("m8_brush_setup: motor exec not initialized");
        return SW_ERR_NOT_INIT;
    }

    ret = brush_init(m8_motor_exec_get(), M8_MOTOR_BRUSH_SIDE, M8_MOTOR_BRUSH_TOP);
    if (ret != SW_OK) {
        LOG_ERROR("m8_brush_setup: brush_init failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_brush_setup ok");
    return SW_OK;
}
