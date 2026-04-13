/**
 * @file    water.c
 * @brief   水路设备实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "domain/device/water.h"
#include "config/machine/m8_machine_config.h"
#include "ports/hal/hal_water_port.h"
#include "common/log.h"
#include <pthread.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * 线程安全模型
 * -------------------------------------------------------------------------
 * 1. water_*on/off 系列控制接口由 step_engine 单线程串行调用，不支持并发写控制。
 * 2. water_is_pump_on()/water_is_any_valve_open() 为只读查询接口，允许报警轮询线程并发调用。
 * 3. s_mutex 仅用于保护内部状态读写，避免读写竞争；不提供跨多个状态组合操作的全局原子性。
 * 4. 若后续引入多个控制线程，需要重新设计水路命令串行化机制，不能仅依赖当前状态锁。
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static pthread_mutex_t s_mutex         = PTHREAD_MUTEX_INITIALIZER;
static bool            s_pump_on       = false;
static bool            s_curtain_on    = false;
static bool            s_foam_on       = false;
static bool            s_brush_on      = false;
static bool            s_highpres_on   = false;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */
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

static sw_err_t check_ops(const hal_water_ops_t *ops)
{
    if ((ops == NULL) ||
        (ops->pump_set == NULL) ||
        (ops->curtain_set == NULL) ||
        (ops->foam_set == NULL) ||
        (ops->brush_water_set == NULL) ||
        (ops->highpres_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}

static sw_err_t set_pump_output(const hal_water_ops_t *ops, bool on)
{
    sw_err_t ret = ops->pump_set(on);
    if (ret == SW_OK)
    {
        set_pump_state(on);
    }
    else
    {
        LOG_ERROR("water: pump_set %s failed ret=%d", on ? "ON" : "OFF", (int)ret);
    }
    return ret;
}

static sw_err_t set_curtain_output(const hal_water_ops_t *ops, bool on)
{
    sw_err_t ret = ops->curtain_set(on);
    if (ret == SW_OK)
    {
        set_curtain_state(on);
    }
    else
    {
        LOG_ERROR("water: curtain_set %s failed ret=%d", on ? "ON" : "OFF", (int)ret);
    }
    return ret;
}

static sw_err_t set_foam_output(const hal_water_ops_t *ops, bool on)
{
    sw_err_t ret = ops->foam_set(on);
    if (ret == SW_OK)
    {
        set_foam_state(on);
    }
    else
    {
        LOG_ERROR("water: foam_set %s failed ret=%d", on ? "ON" : "OFF", (int)ret);
    }
    return ret;
}

static sw_err_t set_brush_output(const hal_water_ops_t *ops, bool on)
{
    sw_err_t ret = ops->brush_water_set(on);
    if (ret == SW_OK)
    {
        set_brush_state(on);
    }
    else
    {
        LOG_ERROR("water: brush_water_set %s failed ret=%d", on ? "ON" : "OFF", (int)ret);
    }
    return ret;
}

static sw_err_t set_highpres_output(const hal_water_ops_t *ops, bool on)
{
    sw_err_t ret = ops->highpres_set(on);
    if (ret == SW_OK)
    {
        set_highpres_state(on);
    }
    else
    {
        LOG_ERROR("water: highpres_set %s failed ret=%d", on ? "ON" : "OFF", (int)ret);
    }
    return ret;
}

/**
 * @brief  安全开泵：确认至少有一个水阀打开后再启动水泵
 */
static sw_err_t ensure_pump_on(const hal_water_ops_t *ops)
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

    return set_pump_output(ops, true);
}

/**
 * @brief  安全关泵：先关泵，再等待管路泄压
 */
static sw_err_t safe_pump_off(const hal_water_ops_t *ops)
{
    sw_err_t ret;

    if (!is_pump_on())
    {
        return SW_OK;
    }

    ret = set_pump_output(ops, false);
    if (ret == SW_OK)
    {
        water_delay_ms(CFG_WATER_PUMP_STOP_DELAY_MS);
    }
    return ret;
}

static sw_err_t stop_pump_if_no_valve_open(const hal_water_ops_t *ops, const char *reason)
{
    if (!any_valve_open() && is_pump_on())
    {
        LOG_INFO("water: no valve open after %s, auto stop pump", reason);
        return safe_pump_off(ops);
    }
    return SW_OK;
}

static void try_stop_pump_if_no_valve_open(const hal_water_ops_t *ops, const char *reason)
{
    sw_err_t ret = stop_pump_if_no_valve_open(ops, reason);

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

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t water_init(void)
{
    reset_states();
    return water_all_off();
}

sw_err_t water_prewash_on(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    sw_err_t               ret;

    ret = check_ops(ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: prewash ON failed, HAL not ready");
        return ret;
    }

    /* 1. 先开泡沫阀和水帘阀。*/
    ret = set_foam_output(ops, true);
    if (ret != SW_OK)
    {
        return ret;
    }
    ret = set_curtain_output(ops, true);
    if (ret != SW_OK)
    {
        (void)set_foam_output(ops, false);
        return ret;
    }

    /* 2. 等待阀体到位，再启动水泵。*/
    water_delay_ms(CFG_WATER_VALVE_OPEN_DELAY_MS);
    ret = ensure_pump_on(ops);
    if (ret != SW_OK)
    {
        (void)set_curtain_output(ops, false);
        (void)set_foam_output(ops, false);
        return ret;
    }

    LOG_INFO("water: prewash ON");
    return SW_OK;
}

sw_err_t water_prewash_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    sw_err_t               ret;
    sw_err_t               first_err = SW_OK;

    ret = check_ops(ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: prewash OFF failed, HAL not ready");
        return ret;
    }

    /* 只关预洗相关阀，水泵可能仍为其他水路服务。*/
    ret = set_foam_output(ops, false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_curtain_output(ops, false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }

    if (first_err == SW_OK)
    {
        try_stop_pump_if_no_valve_open(ops, "prewash_off");
        LOG_INFO("water: prewash OFF");
    }
    return first_err;
}

sw_err_t water_brush_on(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    sw_err_t               ret;

    ret = check_ops(ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: brush ON failed, HAL not ready");
        return ret;
    }

    /* 先开刷子冲水阀，再延时开泵。*/
    ret = set_brush_output(ops, true);
    if (ret != SW_OK)
    {
        return ret;
    }

    water_delay_ms(CFG_WATER_VALVE_OPEN_DELAY_MS);
    ret = ensure_pump_on(ops);
    if (ret != SW_OK)
    {
        (void)set_brush_output(ops, false);
        return ret;
    }

    LOG_INFO("water: brush ON");
    return SW_OK;
}

sw_err_t water_brush_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    sw_err_t               ret;

    ret = check_ops(ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: brush OFF failed, HAL not ready");
        return ret;
    }

    ret = set_brush_output(ops, false);
    if (ret != SW_OK)
    {
        return ret;
    }

    try_stop_pump_if_no_valve_open(ops, "brush_off");
    LOG_INFO("water: brush OFF");
    return SW_OK;
}

sw_err_t water_highpres_on(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    sw_err_t               ret;

    ret = check_ops(ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: high pressure ON failed, HAL not ready");
        return ret;
    }

    /* 先开高压阀，再延时开泵。*/
    ret = set_highpres_output(ops, true);
    if (ret != SW_OK)
    {
        return ret;
    }

    water_delay_ms(CFG_WATER_VALVE_OPEN_DELAY_MS);
    ret = ensure_pump_on(ops);
    if (ret != SW_OK)
    {
        (void)set_highpres_output(ops, false);
        return ret;
    }

    LOG_INFO("water: high pressure ON");
    return SW_OK;
}

sw_err_t water_highpres_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    sw_err_t               ret;

    ret = check_ops(ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: high pressure OFF failed, HAL not ready");
        return ret;
    }

    ret = set_highpres_output(ops, false);
    if (ret != SW_OK)
    {
        return ret;
    }

    try_stop_pump_if_no_valve_open(ops, "highpres_off");
    LOG_INFO("water: high pressure OFF");
    return SW_OK;
}

sw_err_t water_all_off(void)
{
    const hal_water_ops_t *ops = hal_water_get_ops();
    sw_err_t               ret;
    sw_err_t               first_err = SW_OK;
    bool                   was_pump_on;

    ret = check_ops(ops);
    if (ret != SW_OK)
    {
        LOG_ERROR("water: all OFF failed, HAL not ready");
        return ret;
    }

    /* 1. 先关泵，避免阀门先关导致管路冲击。*/
    was_pump_on = is_pump_on();
    ret = set_pump_output(ops, false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }

    /* 2. 仅在水泵原本处于运行状态时等待管路泄压。*/
    if (was_pump_on)
    {
        water_delay_ms(CFG_WATER_PUMP_STOP_DELAY_MS);
    }

    /* 3. 继续尝试关闭所有阀，返回首次错误。*/
    ret = set_curtain_output(ops, false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_foam_output(ops, false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_brush_output(ops, false);
    if ((first_err == SW_OK) && (ret != SW_OK))
    {
        first_err = ret;
    }
    ret = set_highpres_output(ops, false);
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
