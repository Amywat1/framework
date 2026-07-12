/**
 * @file    alarm_detector_helper.c
 * @brief   通用报警 detector 辅助器实现
 * @author  HUWANGWEI
 * @date    2026-07-12
 */

#include "application/alarm_detector_helper.h"

#include "ports/inbound/safety/alarm_binding_port.h"

#include <string.h>

sw_err_t alarm_detector_init(alarm_detector_t *detector, const alarm_detector_cfg_t *cfg)
{
    if ((detector == NULL) || (cfg == NULL) || (cfg->alarm_code == 0U)) {
        return SW_ERR_PARAM;
    }

    memset(detector, 0, sizeof(*detector));
    detector->cfg         = *cfg;
    detector->initialized = true;
    detector->active      = false;
    return SW_OK;
}

sw_err_t alarm_detector_update(alarm_detector_t *detector, bool fault_active)
{
    const alarm_binding_ops_t *ops;
    sw_err_t                  ret = SW_OK;

    if ((detector == NULL) || !detector->initialized) {
        return SW_ERR_PARAM;
    }

    if (fault_active == detector->active) {
        return SW_OK;
    }

    ops = alarm_binding_get_ops();
    if (ops == NULL) {
        return SW_ERR_NOT_INIT;
    }

    if (fault_active) {
        if (ops->trigger == NULL) {
            return SW_ERR_NOT_INIT;
        }
        ret = ops->trigger(detector->cfg.alarm_code);
    } else if (detector->cfg.clear_when_inactive) {
        if (ops->clear == NULL) {
            return SW_ERR_NOT_INIT;
        }
        ret = ops->clear(detector->cfg.alarm_code);
    }

    if (ret == SW_OK) {
        detector->active = fault_active;
    }
    return ret;
}

bool alarm_detector_is_active(const alarm_detector_t *detector)
{
    return (detector != NULL) && detector->initialized && detector->active;
}
