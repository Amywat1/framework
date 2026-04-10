/**
 * @file    device_state.h
 * @brief   设备顶层状态与命令类型定义
 * @author  胡望伟
 * @date    2026-04-10
 */

#ifndef DOMAIN_DEVICE_STATE_H
#define DOMAIN_DEVICE_STATE_H

/* -------------------------------------------------------------------------
 * 设备 FSM 状态
 * ------------------------------------------------------------------------- */
typedef enum
{
    DEV_STATE_INIT     = 0, /* 系统初始化中 */
    DEV_STATE_IDLE,         /* 空闲，等待订单 */
    DEV_STATE_RUN,          /* 洗车进行中 */
    DEV_STATE_COMPLETE,     /* 洗车完成，龙门归位中 */
    DEV_STATE_FAULT,        /* 故障停机，等待复位 */
    DEV_STATE_STOP,         /* 运营关闭（人工停止运营）*/
} dev_state_t;

#endif /* DOMAIN_DEVICE_STATE_H */
