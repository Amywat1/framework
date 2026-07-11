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
 *  事件类型                         发布者 / 订阅者              优先级
 *  ───────────────────────────────────────────────────────────────────
 *  EVT_HW_ESTOP_ON/OFF              safety_thread 发布
 *                                   op_mode_bridge / emergency_handler  高
 *  EVT_ALARM_TRIGGERED              safety_posture / safety_projection  普通
 *  EVT_ALARM_CLEARED                safety_posture / safety_projection  普通
 *  EVT_ALARM_BATCH_CLEARED          safety_posture / safety_projection  普通
 *  EVT_SAFETY_NOMINAL               emergency_handler 等              普通
 *  EVT_SAFETY_LOCKOUT               emergency_handler           高
 *  EVT_CMD_GATEWAY_WAKE             command_gateway             普通
 *  EVT_WASH_SESSION_STARTED         op_mode_bridge / wash_projection  普通
 *  EVT_WASH_DONE                    op_mode_bridge              普通
 *  EVT_WASH_ABORTED                 op_mode_bridge              普通
 *  EVT_OP_MODE_RECOVERY_REQUESTED   recovery_service            普通
 *  EVT_OP_MODE_RECOVERY_COMPLETED   op_mode_bridge              普通
 *  EVT_OP_MODE_SELF_CHECK_COMPLETED op_mode_bridge              普通
 *  EVT_OP_MODE_CHANGED              operational_projection        普通
 *  EVT_OP_MODE_CONTEXT_SYNC         operational_projection        普通
 *  EVT_CLOUD_CONNECTED              report_scheduler            普通
 *  EVT_ALARM_TRIGGERED/CLEARED      report_scheduler            普通
 *  EVT_COMP_MOTION_COMPLETED        m8_alarm_reeval_bridge      普通
 *  EVT_WASH_CHECKPOINT_REACHED      m8_alarm_reeval_bridge      普通
 *  EVT_CLOUD_POINT_DIRTY            report_scheduler            普通
 *
 *  已移出事件总线（改用域内回调）：
 *  EVT_COMP_MOTOR_DONE  → motor_set_done_cb(MOTOR_GANTRY, ...)  in gantry_init
 */

#ifndef CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H
#define CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H

#endif /* CORE_BOOTSTRAP_EVENT_SUBSCRIPTIONS_H */
