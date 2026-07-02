/**
 * @file    m8_fan_setup.c
 * @brief   M8 机型风机适配层：注入 HAL IO 回调并初始化风机模块。
 * @author  HUWANGWEI
 * @date    2026-07-02
 *
 * @note    风机通过 FAN_START / FAN_RESET DO 控制变频器，FAN_ALARM DI 读取故障反馈。
 *          fan_tick 注册到电机 tick 管理器以复用 20ms 周期线程。
 */

#include "machines/m8/adapters/setup/m8_fan_setup.h"
#include "machines/m8/adapters/setup/m8_motor_tick.h"
#include "domain/device/mechanism/fan.h"
#include "machines/m8/config/m8_io_pins.h"
#include "ports/hal/hal_io_port.h"
#include "common/log.h"
#include <stddef.h>

/* -------------------- IO 回调实现 -------------------- */

static sw_err_t fan_set_start(void *ctx, bool on)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    (void)ctx;
    if ((io == NULL) || (io->do_set == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return io->do_set(M8_IO_DO_FAN_START, on);
}

static sw_err_t fan_set_reset(void *ctx, bool on)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    (void)ctx;
    if ((io == NULL) || (io->do_set == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return io->do_set(M8_IO_DO_FAN_RESET, on);
}

static bool fan_read_alarm(void *ctx)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    (void)ctx;
    if ((io == NULL) || (io->di_read == NULL)) {
        return false;
    }
    return io->di_read(M8_IO_DI_FAN_ALARM);
}

/* -------------------- 公共函数 -------------------- */

sw_err_t m8_fan_setup(void)
{
    sw_err_t ret;
    static const fan_io_ops_t ops = {
        .set_start  = fan_set_start,
        .set_reset  = fan_set_reset,
        .read_alarm = fan_read_alarm,
        .ctx        = NULL,
    };
    static const fan_cfg_t cfg = {
        .reset_pulse_ms = 500U,  /* 复位脉冲宽度 500ms */
    };

    if (hal_io_get_ops() == NULL) {
        LOG_ERROR("m8_fan_setup: hal_io not registered");
        return SW_ERR_NOT_INIT;
    }

    ret = fan_init(&ops, &cfg);
    if (ret != SW_OK) {
        LOG_ERROR("m8_fan_setup: fan_init failed ret=%d", (int)ret);
        return ret;
    }

    /* 复用电机 tick 线程（20ms）驱动风机报警检测与复位计时 */
    ret = m8_motor_tick_register(fan_tick);
    if (ret != SW_OK) {
        LOG_ERROR("m8_fan_setup: m8_motor_tick_register 失败 ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_fan_setup ok");
    return SW_OK;
}
