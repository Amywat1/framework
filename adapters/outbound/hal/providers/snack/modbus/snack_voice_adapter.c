/**
 * @file    snack_voice_adapter.c
 * @brief   语音模块 HAL 端口 Linux 真机实现（drv_voice 转发）
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#include "adapters/outbound/hal/providers/snack/modbus/snack_voice_adapter.h"

#include "adapters/outbound/hal/providers/snack/modbus/drv_voice.h"
#include "ports/outbound/hal/hal_voice_port.h"

#include <string.h>

static drv_voice_t s_voice;
static bool        s_voice_inited = false;
static bool        s_configured   = false;
static const char *s_serial_port  = NULL;
static int         s_baud         = 0;
static int         s_modbus_addr  = 0;
static void (*s_event_cb)(int event_code) = NULL;

static bool voice_ready(void)
{
    return s_voice_inited;
}

sw_err_t snack_voice_adapter_configure(const char *serial_port, int baud, int modbus_addr)
{
    if ((serial_port == NULL) || (modbus_addr <= 0) || (modbus_addr > 247)) {
        return SW_ERR_PARAM;
    }
    if (s_configured) {
        return SW_ERR_BUSY;
    }
    s_serial_port = serial_port;
    s_baud        = baud;
    s_modbus_addr = modbus_addr;
    s_configured  = true;
    s_voice_inited = false;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * hal_voice_ops_t 实现
 * ------------------------------------------------------------------------- */
static sw_err_t voice_init(void)
{
    sw_err_t ret;

    if (!s_configured) {
        return SW_ERR_NOT_INIT;
    }

    memset(&s_voice, 0, sizeof(s_voice));
    ret = drv_voice_init(&s_voice, s_serial_port, s_baud, s_modbus_addr);
    if (ret != SW_OK) {
        s_voice_inited = false;
        return ret;
    }

    s_voice_inited = true;
    drv_voice_register_event_cb(&s_voice, s_event_cb);
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
    s_event_cb = cb;
    if (voice_ready()) {
        drv_voice_register_event_cb(&s_voice, cb);
    }
}

static const hal_voice_ops_t s_ops = {
    .init              = voice_init,
    .play              = voice_play,
    .stop              = voice_stop,
    .pause             = voice_pause,
    .set_volume        = voice_set_volume,
    .volume_up         = voice_volume_up,
    .volume_down       = voice_volume_down,
    .register_event_cb = voice_register_event_cb,
};

void snack_voice_adapter_register(void)
{
    hal_voice_register(&s_ops);
}

#ifdef SNACK_VOICE_ADAPTER_UNIT_TEST
void snack_voice_adapter_test_reset(void)
{
    memset(&s_voice, 0, sizeof(s_voice));
    s_voice_inited = false;
    s_configured   = false;
    s_serial_port  = NULL;
    s_baud         = 0;
    s_modbus_addr  = 0;
    s_event_cb     = NULL;
}
#endif
