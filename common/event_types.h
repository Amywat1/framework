/**
 * @file    event_types.h
 * @brief   统一事件类型定义（event_type_t 枚举 + event_t 结构体）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    此文件放在 common/ 层，无层级限制，所有层均可引用。
 *          event_t.timestamp_ms 由 event_bus 在入队时自动填充，调用方无需设置。
 */

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 事件类型枚举
 * ------------------------------------------------------------------------- */
typedef enum
{
    EVT_NONE = 0,

    /* ------------------------------------------------------------------
     * 硬件异步事件（由 adapters/hal/ 发布）
     * ------------------------------------------------------------------ */
    EVT_HW_ESTOP_ON,            /* 急停按下（常闭断开）*/
    EVT_HW_ESTOP_OFF,           /* 急停释放 */
    EVT_HW_GANTRY_FWD_LIM,     /* 龙门触发前限位 */
    EVT_HW_GANTRY_REV_LIM,     /* 龙门触发后限位 */
    EVT_HW_LIFT_UP_LIM,         /* 顶刷升降触发上限位 */
    EVT_HW_LIFT_DOWN_LIM,       /* 顶刷升降触发下限位 */
    EVT_HW_ENCODER_TICK,        /* 龙门码盘脉冲（param=方向：0后退/1前进）*/
    EVT_HW_ENCODER_ERR,         /* 码盘异常（param=MOTOR_EVENT_PARAM_PACK(motor_id, info)）*/
    EVT_HW_IO_OFFLINE,          /* IO 子板掉线（param=board_id）*/
    EVT_HW_IO_ONLINE,           /* IO 子板恢复在线（param=board_id）*/
    EVT_HW_VFD_BRUSH_FAULT,     /* 刷子 VFD 故障（param=fault_code）*/
    EVT_HW_VFD_GANTRY_FAULT,    /* 龙门 VFD 故障（param=fault_code）*/

    /* ------------------------------------------------------------------
     * 组件完成事件（由 domain/device/ 发布）
     * ------------------------------------------------------------------ */
    EVT_COMP_MOTOR_DONE,        /* 电机运动完成（param=编码后的 motor_id + sw_err_t）*/
    EVT_COMP_HOME_DONE,         /* 龙门归位完成（param=sw_err_t）*/
    EVT_COMP_LIFT_DONE,         /* 顶刷升降完成（param=sw_err_t）*/
    EVT_COMP_BRUSH_STARTED,     /* 刷子已启动（param=brush_id）*/

    /* ------------------------------------------------------------------
     * 安全域事件（由 domain/safety/ 发布）
     * ------------------------------------------------------------------ */
    EVT_SAFETY_LOCKOUT,         /* 安全状态进入 LOCKOUT，所有运动停止 */
    EVT_SAFETY_WARNING,         /* 安全状态进入 WARNING，降级运行 */
    EVT_SAFETY_CLEARED,         /* 安全状态恢复 OK */

    /* ------------------------------------------------------------------
     * 报警事件（由 domain/safety/alarm_core 发布）
     * ------------------------------------------------------------------ */
    EVT_ALARM_TRIGGERED,        /* 报警激活（param=alarm_code）*/
    EVT_ALARM_CLEARED,          /* 报警清除（param=alarm_code）*/

    /* ------------------------------------------------------------------
     * 命令事件（由 adapters/ui/ 和 adapters/cloud/ 发布）
     * ------------------------------------------------------------------ */
    EVT_CMD_ORDER,              /* 新洗车订单（param=WashMode_t）*/
    EVT_CMD_STOP_WASH,          /* 停止洗车 */
    EVT_CMD_STOP_OPERATION,     /* 停止运营 */
    EVT_CMD_RESUME_OPERATION,   /* 恢复运营 */
    EVT_CMD_RESET_FAULT,        /* 故障复位 */
    EVT_CMD_HOME_DEVICE,        /* 手动归位 */

    /* ------------------------------------------------------------------
     * 云端连接事件（由 adapters/cloud/ 发布）
     * ------------------------------------------------------------------ */
    EVT_CLOUD_CONNECTED,        /* MQTT 连接建立 */
    EVT_CLOUD_DISCONNECTED,     /* MQTT 连接断开 */

    /* ------------------------------------------------------------------
     * 洗车流程事件（由 application/wash_orchestrator 发布）
     * ------------------------------------------------------------------ */
    EVT_WASH_STEP_DONE,         /* 当前步骤完成（param=WashStep_t）*/
    EVT_WASH_DONE,              /* 完整洗车流程完成 */
    EVT_WASH_ABORTED,           /* 洗车流程中止（param=sw_err_t 中止原因）*/

    EVT_MAX
} event_type_t;

/* -------------------------------------------------------------------------
 * 事件结构体
 *
 * 发布时：调用方只需设置 type 和 param，timestamp_ms 由 event_bus 入队时自动填充。
 * 接收时：handler 入参包含全部三个字段，timestamp_ms 可用于调试和事件回放。
 * ------------------------------------------------------------------------- */
typedef struct
{
    event_type_t type;          /* 事件类型 */
    uint32_t     param;         /* 简单载荷：报警码、错误码、步骤号等（无载荷时为 0）*/
    uint32_t     timestamp_ms;  /* 入队时间戳（event_bus 自动填充，调用方忽略此字段）*/
} event_t;

#ifdef __cplusplus
}
#endif

#endif /* EVENT_TYPES_H */
