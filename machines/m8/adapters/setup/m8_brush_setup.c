/**
 * @file    m8_brush_setup.c
 * @brief   M8 机型刷子适配层：注入接触器回调并初始化 domain brush 层。
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    VFD 硬件回调已由 m8_motor_exec.c 统一实现。
 *          本文件仅负责接触器 DO 操作的注入与 brush_tick（接触器状态机）的注册。
 */

#include "machines/m8/adapters/setup/m8_brush_setup.h"
#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "machines/m8/adapters/setup/m8_motor_tick.h"
#include "domain/device/mechanism/brush.h"
#include "machines/m8/config/m8_brush_config.h"
#include "machines/m8/config/m8_io_pins.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"
#include <stddef.h>

/* -------------------- 接触器 DO 操作回调 -------------------- */

static sw_err_t io_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

static sw_err_t m8_contactor_on(void *ctx, brush_id_t id)
{
    (void)ctx;
    switch (id) {
    case BRUSH_SIDE: return io_do_set(M8_IO_DO_SIDE_BRUSH_ACT, true);
    case BRUSH_TOP:  return io_do_set(M8_IO_DO_TOP_BRUSH_ACT,  true);
    default:         return SW_ERR_PARAM;
    }
}

static sw_err_t m8_contactor_off(void *ctx, brush_id_t id)
{
    (void)ctx;
    switch (id) {
    case BRUSH_SIDE: return io_do_set(M8_IO_DO_SIDE_BRUSH_ACT, false);
    case BRUSH_TOP:  return io_do_set(M8_IO_DO_TOP_BRUSH_ACT,  false);
    default:         return SW_ERR_PARAM;
    }
}

/* -------------------- 公共函数 -------------------- */

sw_err_t m8_brush_setup(void)
{
    sw_err_t ret;

    if (m8_motor_exec_get() == NULL) {
        LOG_ERROR("m8_brush_setup: motor exec not initialized");
        return SW_ERR_NOT_INIT;
    }
    if (hal_io_get_ops() == NULL) {
        LOG_ERROR("m8_brush_setup: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = brush_init(
        m8_motor_exec_get(),
        M8_MOTOR_BRUSH,
        &(brush_contactor_ops_t){
            .set_on  = m8_contactor_on,
            .set_off = m8_contactor_off,
            .ctx     = NULL,
        },
        &(brush_contactor_cfg_t){
            .release_ms = CFG_BRUSH_CONTACTOR_RELEASE_MS,
            .close_ms   = CFG_BRUSH_CONTACTOR_CLOSE_MS,
        });
    if (ret != SW_OK) {
        LOG_ERROR("m8_brush_setup: brush_init failed ret=%d", (int)ret);
        return ret;
    }

    /* brush_tick 仅驱动接触器状态机，motor_tick 由 m8_motor_exec_tick 统一负责 */
    ret = m8_motor_tick_register(brush_tick);
    if (ret != SW_OK) {
        LOG_ERROR("m8_brush_setup: m8_motor_tick_register 失败 ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_brush_setup ok");
    return SW_OK;
}
