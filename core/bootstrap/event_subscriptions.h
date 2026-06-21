/**
 * @file    event_subscriptions.h
 * @brief   事件订阅关系一览（生产代码）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    订阅注册分散在各模块 *_init() 中；本文件仅作全局索引。
 *
 *  事件类型                    订阅者
 *  ─────────────────────────────────────────────────────────
 *  EVT_ALARM_TRIGGERED         safety_fsm
 *  EVT_ALARM_CLEARED           safety_fsm
 *  EVT_SAFETY_LOCKOUT          emergency_handler, device_fsm
 *  EVT_CMD_ORDER               device_fsm
 *  EVT_CMD_STOP_WASH           device_fsm
 *  EVT_CMD_STOP_OPERATION      device_fsm
 *  EVT_CMD_RESUME_OPERATION    device_fsm
 *  EVT_CMD_RESET_FAULT         device_fsm
 *  EVT_CMD_HOME_DEVICE         device_fsm
 *  EVT_COMP_HOME_DONE          device_fsm
 *  EVT_WASH_DONE               device_fsm
 *  EVT_WASH_ABORTED            device_fsm
 *  EVT_COMP_MOTOR_DONE         gantry
 *  EVT_CLOUD_CONNECTED         report_aggregator
 *  EVT_CLOUD_DISCONNECTED      report_aggregator
 */

#ifndef CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H
#define CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H

#endif /* CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H */
