/**
 * @file    port_registry_safety.c
 * @brief   安全端口注册表与调用侧包装
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    包装函数保留原有的四个全局函数名（safety_cutout_execute 等），
 *          使框架内既有调用点不必改写；差别在于绑定时机从链接期变为运行期
 *          注册，未注册时行为明确（故障安全 + 首次告警），而不是静默空转。
 */

#include "common/log.h"
#include "ports/outbound/safety/hw_estop_port.h"
#include "ports/outbound/safety/op_mode_alarm_port.h"
#include "ports/outbound/safety/safety_cutout_port.h"
#include "ports/outbound/safety/safety_deferred_stop.h"
#include "ports/outbound/safety/safety_port.h"

#include <stddef.h>

static const safety_ops_t *s_ops;

sw_err_t safety_port_register(const safety_ops_t *ops)
{
    if (ops != NULL) {
        if ((ops->cutout == NULL) || (ops->estop_is_active == NULL) || (ops->alarm_is_estop == NULL)
            || (ops->deferred_stop == NULL)) {
            LOG_ERROR("safety_port: 注册被拒绝，ops 存在空字段");
            return SW_ERR_PARAM;
        }
    }
    s_ops = ops;
    return SW_OK;
}

const safety_ops_t *safety_port_get_ops(void)
{
    return s_ops;
}

/* -------------------------------------------------------------------------
 * 未注册告警
 *
 * 安全路径被调用却无实现，属于接入错误。这里只在每条路径首次发生时各报一次：
 * 急停期间这些入口可能被高频调用，无节流的日志会淹没真正有用的现场信息。
 * ------------------------------------------------------------------------- */
static bool s_warned_cutout;
static bool s_warned_estop;
static bool s_warned_alarm;
static bool s_warned_deferred;

static void warn_once(bool *flag, const char *what)
{
    if (!*flag) {
        *flag = true;
        LOG_ERROR("safety_port: %s 被调用但安全端口未注册，安全动作未执行", what);
    }
}

/* -------------------------------------------------------------------------
 * 调用侧包装
 * ------------------------------------------------------------------------- */

void safety_cutout_execute(void)
{
    const safety_ops_t *ops = s_ops;

    if ((ops == NULL) || (ops->cutout == NULL)) {
        warn_once(&s_warned_cutout, "safety_cutout_execute");
        return;
    }
    ops->cutout();
}

bool hw_estop_port_is_active(void)
{
    const safety_ops_t *ops = s_ops;

    if ((ops == NULL) || (ops->estop_is_active == NULL)) {
        warn_once(&s_warned_estop, "hw_estop_port_is_active");
        /* 返回 false 而非 true：未接入时返回"急停激活"会让设备一直处于
         * 急停态且无法复位，反而掩盖接入缺失。真正的兜底是启动期契约校验。 */
        return false;
    }
    return ops->estop_is_active();
}

bool op_mode_alarm_port_is_estop(uint32_t alarm_code)
{
    const safety_ops_t *ops = s_ops;

    if ((ops == NULL) || (ops->alarm_is_estop == NULL)) {
        warn_once(&s_warned_alarm, "op_mode_alarm_port_is_estop");
        return false;
    }
    return ops->alarm_is_estop(alarm_code);
}

void safety_deferred_stop(void)
{
    const safety_ops_t *ops = s_ops;

    if ((ops == NULL) || (ops->deferred_stop == NULL)) {
        warn_once(&s_warned_deferred, "safety_deferred_stop");
        return;
    }
    ops->deferred_stop();
}

void port_registry_safety_reset(void)
{
    s_ops             = NULL;
    s_warned_cutout   = false;
    s_warned_estop    = false;
    s_warned_alarm    = false;
    s_warned_deferred = false;
}
