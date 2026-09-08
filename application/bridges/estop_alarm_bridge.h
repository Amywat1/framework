/**
 * @file    estop_alarm_bridge.h
 * @brief   硬件急停确认边沿投影为报警
 * @author  HUWANGWEI
 * @date    2026-09-08
 *
 * @note    急停确认与切断仍由 estop_poll 独占。本桥只订 EVT_HW_ESTOP_ON/OFF，
 *          把已确认的条件交给 alarm_registry，避免 DI 滤波与采集器各确认一次。
 *          不进入急停热路径。报警码由项目 wiring 传入。
 */

#ifndef APPLICATION_BRIDGES_ESTOP_ALARM_BRIDGE_H
#define APPLICATION_BRIDGES_ESTOP_ALARM_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdint.h>

/**
 * @brief  订阅急停确认边沿并投影到指定报警码
 * @param  alarm_code  项目急停报警码，必须是合法编码
 * @retval SW_OK        已订阅
 * @retval SW_ERR_PARAM 报警码非法
 *
 * @note   须在 alarm_registry 已装载该码、event_bus 已初始化之后调用。
 *         建议在 op_mode_bridge_init 之后接入，使 LOCKOUT 嵌套 drain 时
 *         急停旗标与 STOPPED 已生效。同一 event_bus 实例勿重复调用。
 */
sw_err_t estop_alarm_bridge_init(uint32_t alarm_code);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_BRIDGES_ESTOP_ALARM_BRIDGE_H */
