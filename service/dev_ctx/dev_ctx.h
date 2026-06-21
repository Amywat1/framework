/**
 * @file    dev_ctx.h
 * @brief   设备状态快照接口（统一只读视图）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    dev_ctx 是设备当前运行状态的集中只读视图，供上报/CLI 查询使用。
 *          写入权限严格分片，各字段只允许指定模块更新：
 *            device_state   → device_fsm（读写均经 dev_ctx，无镜像）
 *            safety_state   → safety_fsm
 *            wash_progress  → wash_orchestrator
 *            alarm_state    → safety_fsm
 *            cloud_status   → report_aggregator
 *          读取通过 dev_ctx_snapshot() 返回值拷贝，外部不持有指针。
 */

#ifndef SERVICE_DEV_CTX_H
#define SERVICE_DEV_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/device_state.h"
#include "domain/model/wash_types.h"
#include "common/sw_error.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 设备状态快照结构体
 * ------------------------------------------------------------------------- */
typedef struct
{
    dev_state_t    device_state;    /* 设备 FSM 状态 */
    wash_step_t    wash_step;       /* 当前洗车步骤（IDLE = 不在洗车中）*/
    wash_mode_t    wash_mode;       /* 当前洗车模式 */
    bool           cloud_connected; /* 云端 MQTT 连接状态 */
} device_context_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化 dev_ctx（清零所有字段为安全初始值）
 */
sw_err_t dev_ctx_init(void);

/**
 * @brief  获取当前状态快照（值拷贝，线程安全）
 */
device_context_t dev_ctx_snapshot(void);

/**
 * @brief  读取设备 FSM 状态（线程安全）
 */
dev_state_t dev_ctx_get_device_state(void);

/* -- 分片写入接口（各自只允许对应模块调用）-- */

/** @brief [device_fsm] 更新设备 FSM 状态 */
void dev_ctx_set_device_state(dev_state_t state);

/** @brief [wash_orchestrator] 更新当前洗车进度 */
void dev_ctx_set_wash_progress(wash_step_t step, wash_mode_t mode);

/** @brief [report_aggregator] 更新云端连接状态 */
void dev_ctx_set_cloud_status(bool connected);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_DEV_CTX_H */
