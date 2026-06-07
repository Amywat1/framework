/**
 * @file    water.c
 * @brief   水路设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/water.h"
#include "config/machine/m8_machine_config.h"
#include "common/log.h"
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t       s_mutex = PTHREAD_MUTEX_INITIALIZER;
static water_actuator_ops_t  s_actuator;
static bool                  s_actuator_ready = false;

static bool s_pump_on     = false;
static bool s_curtain_on  = false;
static bool s_foam_on     = false;
static bool s_brush_on    = false;
static bool s_highpres_on = false;

static void water_delay_ms(unsigned int delay_ms)
{
    if (delay_ms > 0U)
    {
        usleep(delay_ms * 1000U);
    }
}

static bool any_valve_open_locked(void)
{
    return (s_curtain_on || s_foam_on || s_brush_on || s_highpres_on);
}

static bool any_valve_open(void)
{
    bool result;

    pthread_mutex_lock(&s_mutex);
    result = any_valve_open_locked();
    pthread_mutex_unlock(&s_mutex);

    return result;
}

static bool is_pump_on(void)
{
    bool result;

    pthread_mutex_lock(&s_mutex);
    result = s_pump_on;
    pthread_mutex_unlock(&s_mutex);

    return result;
}

static void set_pump_state(bool on)
{
    pthread_mutex_lock(&s_mutex);
    s_pump_on = on;
    pthread_mutex_unlock(&s_mutex);
}

static void set_curtain_state(bool on)
{
    pthread_mutex_lock(&s_mutex);
    s_curtain_on = on;
    pthread_mutex_unlock(&s_mutex);
}

static void set_foam_state(bool on)
{
    pthread_mutex_lock(&s_mutex);
    s_foam_on = on;
    pthread_mutex_unlock(&s_mutex);
}

static void set_brush_state(bool on)
{
    pthread_mutex_lock(&s_mutex);
    s_brush_on = on;
    pthread_mutex_unlock(&s_mutex);
}

static void set_highpres_state(bool on)
{
    pthread_mutex_lock(&s_mutex);
    s_highpres_on = on;
    pthread_mutex_unlock(&s_mutex);
}

static sw_err_t check_actuator(void)
{
    if (!s_actuator_ready || (s_actuator.slot_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}

static sw_err_t slot_output(water_channel_t ch, water_slot_t slot, bool on)
{
    sw_err_t ret = s_actuator.slot_set(ch, slot, on);

    if (ret != SW_OK)
    {
        LOG_ERROR("water: ch %d slot %d %s failed ret=%d",
                  (int)ch, (int)slot, on ? "ON" : "OFF", (int)ret);
    }
    return ret;
}

static sw_err_t set_pump_output(bool on)
{
    sw_err_t ret = slot_output(WCH_SHARED, WATER_SLOT_PUMP, on);

    if (ret == SW_OK)
    {
        set_pump_state(on);
    }
    return ret;
}

static sw_err_t set_curtain_output(bool on)
{
    sw_err_t ret = slot_output(WCH_CURTAIN, WATER_SLOT_WATER_VALVE, on);

    if (ret == SW_OK)
    {
        set_curtain_state(on);
    }
    return ret;
}

static sw_err_t set_foam_output(bool on)
{
    sw_err_t ret = slot_output(WCH_FOAM, WATER_SLOT_WATER_VALVE, on);

    if (ret == SW_OK)
    {
        set_foam_state(on);
    }
    return ret;
}

static sw_err_t set_brush_output(bool on)
{
    sw_err_t ret = slot_output(WCH_BRUSH, WATER_SLOT_WATER_VALVE, on);

    if (ret == SW_OK)
    {
        set_brush_state(on);
    }
    return ret;
}

static sw_err_t set_highpres_output(bool on)
{
    sw_err_t ret = slot_output(WCH_HIGHPRES, WATER_SLOT_WATER_VALVE, on);

    if (ret == SW_OK)
    {
        set_highpres_state(on);
    }
    return ret;
}

static sw_err_t ensure_pump_on(void)
{
    if (is_pump_on())
    {
        return SW_OK;
    }

    if (!any_valve_open())
    {
        LOG_WARN("water: pump ON skipped because no valve is open");
        return SW_ERR_STATE;
    }

    return set_pump_output(true);
}

static sw_err_t safe_pump_off(void)
{
    sw_err_t ret;

    if (!is_pump_on())
    {
        return SW_OK;
    }

    ret = set_pump_output(false);
    if (ret == SW_OK)
    {
        water_delay_ms(CFG_WATER_PUMP_STOP_DELAY_MS);
    }
    return ret;
}

static sw_err_t stop_pump_if_no_valve_open(const char *reason)
{
    if (!any_valve_open() && is_pump_on())
    {
        LOG_INFO("water: no valve open after %s, auto stop pump", reason);
        return safe_pump_off();
    }
    return SW_OK;
}

static void try_stop_pump_if_no_valve_open(const char *reason)
{
    sw_err_t ret = stop_pump_if_no_valve_open(reason);

    if (ret != SW_OK)
    {
        LOG_WARN("water: auto stop pump after %s failed ret=%d", reason, (int)ret);
    }
}

static void reset_states(void)
{
    pthread_mutex_lock(&s_mutex);
    s_pump_on     = false;
    s_curtain_on  = false;
    s_foam_on     = false;
    s_brush_on    = false;
    s_highpres_on = false;
    pthread_mutex_unlock(&s_mutex);
}

sw_err_t water_init(const water_actuator_ops_t *ops)
{
    sw_err_t ret;

    if ((ops == NULL) || (ops->slot_set == NULL))
    {
        return SW_ERR_PARAM;
    }

    s_actuator       = *ops;
    s_actuator_ready = true;  /* 先置 true，使内部调 water_all_off 时通过 check_actuator */
    reset_states();
    ret = water_all_off();
    if (ret != SW_OK)
    {
        s_actuator_ready = false;  /* all_off 失败则撤销就绪标志，避免遗留半初始化状态 */
    }
    return ret;
}

sw_err_t water_prewash_on(void)
{
    sw_err_t ret;

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("water: prewash ON failed, actuator not ready");
        return ret;
    }

    ret = set_foam_output(true);
    if (ret != SW_OK)
    {
        return ret;
    }
    ret = set_curtain_output(true);
    if (ret != SW_OK)
    {
        (void)set_foam_output(false);
        return ret;
    }

    water_delay_ms(CFG_WATER_VALVE_OPEN_DELAY_MS);
    ret = ensure_pump_on();
    if (ret != SW_OK)
    {
        (void)set_curtain_output(false);
        (void)set_foam_output(false);
        return ret;
    }

    LOG_INFO("water: prewash ON");
    return SW_OK;
}

sw_err_t water_prewash_off(void)
{
    sw_err_t ret;
    sw_err_t first_err = SW_OK;

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("water: prewash OFF failed, actuator not ready");
        return ret;
    }

    ret = set_foam_output(false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_curtain_output(false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }

    if (first_err == SW_OK)
    {
        try_stop_pump_if_no_valve_open("prewash_off");
        LOG_INFO("water: prewash OFF");
    }
    return first_err;
}

sw_err_t water_brush_on(void)
{
    sw_err_t ret;

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("water: brush ON failed, actuator not ready");
        return ret;
    }

    ret = set_brush_output(true);
    if (ret != SW_OK)
    {
        return ret;
    }

    water_delay_ms(CFG_WATER_VALVE_OPEN_DELAY_MS);
    ret = ensure_pump_on();
    if (ret != SW_OK)
    {
        (void)set_brush_output(false);
        return ret;
    }

    LOG_INFO("water: brush ON");
    return SW_OK;
}

sw_err_t water_brush_off(void)
{
    sw_err_t ret;

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("water: brush OFF failed, actuator not ready");
        return ret;
    }

    ret = set_brush_output(false);
    if (ret != SW_OK)
    {
        return ret;
    }

    try_stop_pump_if_no_valve_open("brush_off");
    LOG_INFO("water: brush OFF");
    return SW_OK;
}

sw_err_t water_highpres_on(void)
{
    sw_err_t ret;

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("water: high pressure ON failed, actuator not ready");
        return ret;
    }

    ret = set_highpres_output(true);
    if (ret != SW_OK)
    {
        return ret;
    }

    water_delay_ms(CFG_WATER_VALVE_OPEN_DELAY_MS);
    ret = ensure_pump_on();
    if (ret != SW_OK)
    {
        (void)set_highpres_output(false);
        return ret;
    }

    LOG_INFO("water: high pressure ON");
    return SW_OK;
}

sw_err_t water_highpres_off(void)
{
    sw_err_t ret;

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("water: high pressure OFF failed, actuator not ready");
        return ret;
    }

    ret = set_highpres_output(false);
    if (ret != SW_OK)
    {
        return ret;
    }

    try_stop_pump_if_no_valve_open("highpres_off");
    LOG_INFO("water: high pressure OFF");
    return SW_OK;
}

sw_err_t water_all_off(void)
{
    sw_err_t ret;
    sw_err_t first_err = SW_OK;
    bool     was_pump_on;

    ret = check_actuator();
    if (ret != SW_OK)
    {
        LOG_ERROR("water: all OFF failed, actuator not ready");
        return ret;
    }

    was_pump_on = is_pump_on();
    ret = set_pump_output(false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }

    if (was_pump_on)
    {
        water_delay_ms(CFG_WATER_PUMP_STOP_DELAY_MS);
    }

    ret = set_curtain_output(false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_foam_output(false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_brush_output(false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_highpres_output(false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }

    if (first_err == SW_OK)
    {
        LOG_INFO("water: all OFF (pump first, then valves)");
    }
    return first_err;
}

bool water_is_pump_on(void)
{
    return is_pump_on();
}

bool water_is_any_valve_open(void)
{
    return any_valve_open();
}
