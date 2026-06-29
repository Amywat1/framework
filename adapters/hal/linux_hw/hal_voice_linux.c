/**
 * @file    hal_voice_linux.c
 * @brief   语音模块 HAL 端口 Linux 真机实现（drv_voice 转发）
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#include "adapters/hal/linux_hw/hal_voice_linux.h"
#include "ports/hal/hal_voice_port.h"
#include "adapters/hal/linux_hw/drv/drv_voice.h"

#include <string.h>

static drv_voice_t s_voice;
static bool        s_voice_inited = false;

static bool voice_ready(void)
{
    return s_voice_inited;
}

/* -------------------------------------------------------------------------
 * 供机型适配层调用的初始化接口
 * ------------------------------------------------------------------------- */
sw_err_t hal_voice_linux_init(const char *serial_port, int baud, int modbus_addr)
{
    sw_err_t ret;

    memset(&s_voice, 0, sizeof(s_voice));
    ret = drv_voice_init(&s_voice, serial_port, baud, modbus_addr);
    if (ret != SW_OK) {
        s_voice_inited = false;
        return ret;
    }

    s_voice_inited = true;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * hal_voice_ops_t 实现
 * ------------------------------------------------------------------------- */
static sw_err_t voice_init(void)
{
    /* 实例初始化由 hal_voice_linux_init 在 bootstrap 阶段完成，此处仅占位 */
    return SW_OK;
}

static sw_err_t voice_play(uint16_t track)
{
    if (!voice_ready()) {
        return SW_ERR_NOT_INIT;
    }
    return drv_voice_play(&s_voice, track);
}

static sw_err_t voice_stop(void)
{
    if (!voice_ready()) {
        return SW_ERR_NOT_INIT;
    }
    return drv_voice_stop(&s_voice);
}

static sw_err_t voice_pause(void)
{
    if (!voice_ready()) {
        return SW_ERR_NOT_INIT;
    }
    return drv_voice_pause(&s_voice);
}

static sw_err_t voice_set_volume(uint16_t vol)
{
    if (!voice_ready()) {
        return SW_ERR_NOT_INIT;
    }
    return drv_voice_set_volume(&s_voice, vol);
}

static sw_err_t voice_volume_up(void)
{
    if (!voice_ready()) {
        return SW_ERR_NOT_INIT;
    }
    return drv_voice_volume_up(&s_voice);
}

static sw_err_t voice_volume_down(void)
{
    if (!voice_ready()) {
        return SW_ERR_NOT_INIT;
    }
    return drv_voice_volume_down(&s_voice);
}

static void voice_register_event_cb(void (*cb)(int event_code))
{
    if (voice_ready()) {
        drv_voice_register_event_cb(&s_voice, cb);
    }
}

static const hal_voice_ops_t s_ops = {
    .init               = voice_init,
    .play               = voice_play,
    .stop               = voice_stop,
    .pause              = voice_pause,
    .set_volume         = voice_set_volume,
    .volume_up          = voice_volume_up,
    .volume_down        = voice_volume_down,
    .register_event_cb  = voice_register_event_cb,
};

void hal_voice_linux_register(void)
{
    hal_voice_register(&s_ops);
}
