/**
 * @file    hal_voice_sim.c
 * @brief   语音模块 HAL 仿真实现（无硬件，指令静默丢弃）
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#include "common/log.h"
#include "ports/outbound/hal/hal_voice_port.h"

static sw_err_t sim_voice_init(void)
{
    LOG_INFO("hal_voice_sim: init ok");
    return SW_OK;
}

static sw_err_t sim_play(uint16_t track)
{
    LOG_INFO("hal_voice_sim: play track=%u", (unsigned)track);
    return SW_OK;
}

static sw_err_t sim_stop(void)
{
    return SW_OK;
}
static sw_err_t sim_pause(void)
{
    return SW_OK;
}

static sw_err_t sim_set_volume(uint16_t vol)
{
    (void)vol;
    return SW_OK;
}

static sw_err_t sim_volume_up(void)
{
    return SW_OK;
}
static sw_err_t sim_volume_down(void)
{
    return SW_OK;
}

static void sim_register_event_cb(void (*cb)(int event_code))
{
    (void)cb;
}

static const hal_voice_ops_t s_ops = {
    .init              = sim_voice_init,
    .play              = sim_play,
    .stop              = sim_stop,
    .pause             = sim_pause,
    .set_volume        = sim_set_volume,
    .volume_up         = sim_volume_up,
    .volume_down       = sim_volume_down,
    .register_event_cb = sim_register_event_cb,
};

void hal_voice_sim_register(void)
{
    hal_voice_register(&s_ops);
    LOG_INFO("hal_voice_sim: registered");
}
