/**
 * @file    device_state.h
 * @brief   设备顶层状态与命令类型定义
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef DOMAIN_DEVICE_STATE_H
#define DOMAIN_DEVICE_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 设备 FSM 状态
 * ------------------------------------------------------------------------- */
typedef enum
{
    DEV_STATE_INIT       = 0, /* 系统初始化中（device_fsm_init 前）*/
    DEV_STATE_IDLE,           /* 待机，等待订单 */
    DEV_STATE_RUNNING,        /* 洗车进行中 */
    DEV_STATE_SUSPENDING,     /* 安全归位中：故障/急停后过渡态，emergency_handler 负责执行 */
    DEV_STATE_FAULT,          /* 故障停机，等待归位或复位指令 */
    DEV_STATE_HOMING,         /* 归位中：人工触发完整归位，wash_orchestrator 负责执行 */
    DEV_STATE_STOP,           /* 运营关闭（人工停止运营）*/
} dev_state_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_STATE_H */
