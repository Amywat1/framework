/**
 * @file    app_fsm.h
 * @brief   设备顶层有限状态机接口
 * @author  胡望伟
 * @date    2026-04-08
 */

#ifndef APP_FSM_H
#define APP_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 设备 FSM 状态
 * ------------------------------------------------------------------------- */
typedef enum
{
    DEV_STATE_INIT     = 0, /* 初始化 */
    DEV_STATE_IDLE,         /* 空闲，等待订单 */
    DEV_STATE_RUN,          /* 洗车进行中 */
    DEV_STATE_COMPLETE,     /* 洗车完成，龙门归位 */
    DEV_STATE_FAULT,        /* 故障停机 */
    DEV_STATE_STOP,         /* 运营关闭 */
} DevState_t;

/* -------------------------------------------------------------------------
 * 设备命令
 * ------------------------------------------------------------------------- */
typedef enum
{
    DEV_CMD_NONE   = 0,
    DEV_CMD_ORDER,          /* 新订单（开始洗车）*/
    DEV_CMD_STOP,           /* 停止运营 */
    DEV_CMD_RESUME,         /* 恢复运营 */
    DEV_CMD_RESET,          /* 故障复位 */
    DEV_CMD_HOME,           /* 手动归位 */
} DevCmd_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化所有业务模块（组件、洗车引擎）并启动 FSM 线程
 * @retval SW_OK / SW_ERR_HW
 */
sw_err_t app_fsm_init(void);

/**
 * @brief  获取当前设备状态
 */
DevState_t app_fsm_get_state(void);

/**
 * @brief  投递命令（线程安全）
 */
void app_fsm_post_cmd(DevCmd_t cmd);

/**
 * @brief  CLI 调试接口（app 命令域）
 * 命令：status / order / stop / resume / reset
 */
int app_debug_ctl(char *cmd, char *p1, char *p2);

#ifdef __cplusplus
}
#endif

#endif /* APP_FSM_H */
