/**
 * @file    hal_indicator_linux.c
 * @brief   入口指示灯与挡杆 HAL 端口 — Linux 真机实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "ports/hal/hal_indicator_port.h"
#include "config/machine/m8_machine_config.h"
#include "common/sw_config.h"
#include "adapters/machine/m8/m8_machine_map.h"
#include "adapters/hal/linux_hw/drv/drv_io.h"
#include <pthread.h>
#include <stdbool.h>

static pthread_mutex_t  s_light_mutex        = PTHREAD_MUTEX_INITIALIZER;
static hal_light_state_t s_light_state       = HAL_LIGHT_OFF;
static bool              s_blink_phase_on    = false;
static unsigned int      s_blink_elapsed_ms  = 0U;

static bool indicator_is_blink_state(hal_light_state_t state)
{
    return ((state == HAL_LIGHT_GREEN_BLINK) ||
            (state == HAL_LIGHT_RED_BLINK) ||
            (state == HAL_LIGHT_YELLOW_BLINK));
}

static void indicator_apply_outputs(hal_light_state_t state, bool blink_phase_on)
{
    /* 先全灭再按状态点亮，防止多路同时亮 */
    (void)drv_io_do_set(M8_DO_ENTRY_GREEN1, false);
    (void)drv_io_do_set(M8_DO_ENTRY_GREEN2, false);
    (void)drv_io_do_set(M8_DO_ENTRY_RED,    false);
    (void)drv_io_do_set(M8_DO_ENTRY_YELLOW, false);

    switch (state)
    {
        case HAL_LIGHT_GREEN:
            (void)drv_io_do_set(M8_DO_ENTRY_GREEN1, true);
            (void)drv_io_do_set(M8_DO_ENTRY_GREEN2, true);
            break;
        case HAL_LIGHT_RED:
            (void)drv_io_do_set(M8_DO_ENTRY_RED, true);
            break;
        case HAL_LIGHT_YELLOW:
            (void)drv_io_do_set(M8_DO_ENTRY_YELLOW, true);
            break;
        case HAL_LIGHT_GREEN_BLINK:
            if (blink_phase_on)
            {
                (void)drv_io_do_set(M8_DO_ENTRY_GREEN1, true);
                (void)drv_io_do_set(M8_DO_ENTRY_GREEN2, true);
            }
            break;
        case HAL_LIGHT_RED_BLINK:
            if (blink_phase_on)
            {
                (void)drv_io_do_set(M8_DO_ENTRY_RED, true);
            }
            break;
        case HAL_LIGHT_YELLOW_BLINK:
            if (blink_phase_on)
            {
                (void)drv_io_do_set(M8_DO_ENTRY_YELLOW, true);
            }
            break;
        case HAL_LIGHT_OFF:
        default:
            break;
    }
}

static sw_err_t m8_entry_light_set(hal_light_state_t state)
{
    pthread_mutex_lock(&s_light_mutex);
    s_light_state      = state;
    s_blink_phase_on   = false;
    s_blink_elapsed_ms = 0U;

    /* 常亮/常灭立即生效；闪烁灯态先进入灭相位，等待下一次 tick 开始闪烁。 */
    if (indicator_is_blink_state(state))
    {
        indicator_apply_outputs(state, false);
    }
    else
    {
        indicator_apply_outputs(state, true);
    }
    pthread_mutex_unlock(&s_light_mutex);

    return SW_OK;
}

static void m8_entry_light_tick(void)
{
    hal_light_state_t state;

    pthread_mutex_lock(&s_light_mutex);
    state = s_light_state;
    if (!indicator_is_blink_state(state))
    {
        pthread_mutex_unlock(&s_light_mutex);
        return;
    }

    s_blink_elapsed_ms += CFG_ALARM_POLL_PERIOD_MS;
    if (s_blink_elapsed_ms < CFG_ENTRY_LIGHT_BLINK_HALF_MS)
    {
        pthread_mutex_unlock(&s_light_mutex);
        return;
    }

    s_blink_elapsed_ms = 0U;
    s_blink_phase_on   = !s_blink_phase_on;
    indicator_apply_outputs(state, s_blink_phase_on);
    pthread_mutex_unlock(&s_light_mutex);
}

static sw_err_t m8_rod_open(void)
{
    (void)drv_io_do_set(M8_DO_ROD_EXTEND,  false);
    (void)drv_io_do_set(M8_DO_ROD_RETRACT, true);
    return SW_OK;
}

static sw_err_t m8_rod_close(void)
{
    (void)drv_io_do_set(M8_DO_ROD_RETRACT, false);
    (void)drv_io_do_set(M8_DO_ROD_EXTEND,  true);
    return SW_OK;
}

static const hal_indicator_ops_t s_ops = {
    .entry_light_set  = m8_entry_light_set,
    .entry_light_tick = m8_entry_light_tick,
    .rod_open         = m8_rod_open,
    .rod_close        = m8_rod_close,
};

void hal_indicator_linux_register(void)
{
    hal_indicator_register(&s_ops);
}
