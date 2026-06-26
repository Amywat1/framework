/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警绑定适配实现
 * @author  HUWANGWEI
 * @date    2026-06-26
 */

#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "adapters/machine/m8/m8_sensor.h"
#include "ports/safety/alarm_binding_port.h"
#include "domain/model/alarm_code.h"
#include "config/machine/m8_signal_table.h"
#include "common/log.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 本机型会上报的报警码（标识值）
 *   报警「定义」（等级/清除/描述）在 doc/m8_alarm_catalog.json，与此处靠 code
 *   数值对齐；此处只声明「M8 检测到什么时报哪个码」，属机型检测层职责。
 * ------------------------------------------------------------------------- */
#define M8_ALM_ESTOP               ALARM_CODE_MAKE(2, 17, 9)  /* 201709 急停 */
#define M8_ALM_SIDE_BRUSH_OVERLOAD ALARM_CODE_MAKE(2, 11, 1)  /* 201101 侧刷过载 */
#define M8_ALM_FAN_FAULT           ALARM_CODE_MAKE(4, 3,  3)  /* 400303 风机报警 */

/* -------------------------------------------------------------------------
 * 信号 → 报警码映射表（电平直检型，每行一个报警源）
 * ------------------------------------------------------------------------- */
typedef struct
{
    m8_signal_id_t sig;   /**< 防抖后的 DI 信号 */
    uint32_t       code;  /**< 对应报警码 */
} m8_alarm_map_t;

static const m8_alarm_map_t s_alarm_map[] = {
    { M8_SIG_ESTOP,               M8_ALM_ESTOP },
    { M8_SIG_SIDE_BRUSH_OVERLOAD, M8_ALM_SIDE_BRUSH_OVERLOAD },
    { M8_SIG_FAN_ALARM,           M8_ALM_FAN_FAULT },
};

#define M8_ALARM_MAP_COUNT  (sizeof(s_alarm_map) / sizeof(s_alarm_map[0]))

/* 上轮各信号的激活态，用于边沿检测（仅 io_poll 线程访问）*/
static bool s_last_active[M8_ALARM_MAP_COUNT];

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */

sw_err_t m8_alarm_adapt_init(void)
{
    if (alarm_binding_get_ops() == NULL)
    {
        LOG_ERROR("m8_alarm_adapt: alarm_binding_port not registered");
        return SW_ERR_NOT_INIT;
    }

    for (int i = 0; i < (int)M8_ALARM_MAP_COUNT; ++i)
    {
        s_last_active[i] = false;
    }

    LOG_INFO("m8_alarm_adapt: init ok, sources=%d", (int)M8_ALARM_MAP_COUNT);
    return SW_OK;
}

void m8_alarm_adapt_poll(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();

    if (ops == NULL)
    {
        return;
    }

    for (int i = 0; i < (int)M8_ALARM_MAP_COUNT; ++i)
    {
        bool now = m8_signal_is_active(s_alarm_map[i].sig);

        if (now == s_last_active[i])
        {
            continue; /* 无边沿 */
        }
        s_last_active[i] = now;

        if (now)
        {
            (void)ops->trigger(s_alarm_map[i].code);
        }
        else
        {
            (void)ops->clear(s_alarm_map[i].code);
        }
    }
}
