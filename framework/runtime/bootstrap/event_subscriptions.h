/**
 * @file    event_subscriptions.h
 * @brief   事件订阅关系一览（生产代码）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    订阅注册分散在各模块 *_init() 中；本文件仅作全局索引。
 *          优先级说明：SAFETY 类和 ESTOP 硬件事件走高优先级队列，
 *          其余事件走普通队列（dispatch_loop 优先排空高优先级队列）。
 *
 *  事件类型                    订阅者                  优先级
 *  ──────────────────────────────────────────────────────────────
 *  EVT_ALARM_TRIGGERED         safety_fsm              普通
 *  EVT_ALARM_CLEARED           safety_fsm              普通
 *  EVT_SAFETY_LOCKOUT          emergency_handler,      高
 *                              device_fsm
 *  EVT_CMD_ORDER               device_fsm              普通
 *  EVT_CMD_STOP_WASH           device_fsm              普通
 *  EVT_CMD_STOP_OPERATION      device_fsm              普通
 *  EVT_CMD_RESUME_OPERATION    device_fsm              普通
 *  EVT_CMD_RESET_FAULT         device_fsm              普通
 *  EVT_CMD_HOME_DEVICE         device_fsm              普通
 *  EVT_COMP_HOME_DONE          device_fsm              普通
 *  EVT_WASH_DONE               device_fsm              普通
 *  EVT_WASH_ABORTED            device_fsm              普通
 *  EVT_CLOUD_CONNECTED         report_scheduler        普通（重连 resync）
 *  EVT_ALARM_TRIGGERED         report_scheduler        普通（报警增量上报）
 *  EVT_ALARM_CLEARED           report_scheduler        普通（报警增量上报）
 *  EVT_CLOUD_POINT_DIRTY       report_scheduler        普通（ON_CHANGE 增量）
 *
 *  已移出事件总线（改用域内回调）：
 *  EVT_COMP_MOTOR_DONE  → motor_set_done_cb(MOTOR_GANTRY, ...)  in gantry_init
 */

#ifndef CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H
#define CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H

#endif /* CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H */
