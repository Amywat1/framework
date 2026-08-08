/**
 * @file    hal_voice_sim.c
 * @brief   语音模块 HAL 仿真实现（无硬件，指令静默丢弃）
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#include "adapters/outbound/hal/sim/hal_voice_sim.h"

#include "common/log.h"
#include "domain/ports/outbound/hal/hal_voice_port.h"

#include <stdbool.h>

static bool s_inited = false;

static sw_err_t sim_voice_init(void)
{
    s_inited = true;
    LOG_INFO("hal_voice_sim: init ok");
    return SW_OK;
}

static sw_err_t sim_play(uint16_t track)
{
    if (!s_inited) {
        return SW_ERR_NOT_INIT;
    }
    LOG_INFO("hal_voice_sim: play track=%u", (unsigned)track);
    return SW_OK;
}

static sw_err_t sim_stop(void)
{
    if (!s_inited) {
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}
static sw_err_t sim_pause(void)
{
    if (!s_inited) {
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}

static sw_err_t sim_set_volume(uint16_t vol)
{
    (void)vol;
    if (!s_inited) {
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}

static sw_err_t sim_volume_up(void)
{
    if (!s_inited) {
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}
static sw_err_t sim_volume_down(void)
{
    if (!s_inited) {
        return SW_ERR_NOT_INIT;
    }
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

#ifdef HAL_VOICE_SIM_UNIT_TEST
void hal_voice_sim_test_reset(void)
{
    s_inited = false;
}
#endif
