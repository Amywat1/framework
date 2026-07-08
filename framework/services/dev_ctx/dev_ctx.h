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
 *            wash_mode      → wash_orchestrator
 *            gantry_pos     → wash_orchestrator（tick 循环写入，非洗车期保留最后值）
 *            alarm_state    → safety_fsm
 *            cloud_connected → 读穿 cloud_connection_port（无本地副本）
 *          读取通过 dev_ctx_snapshot() 返回值拷贝，外部不持有指针。
 */

#ifndef SERVICE_DEV_CTX_H
#define SERVICE_DEV_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/domain/device_control/model/device_state.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/domain/safety/model/safety_types.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 设备状态快照结构体
 * ------------------------------------------------------------------------- */
typedef struct
{
    dev_state_t    device_state;    /* 设备 FSM 状态 */
    wash_mode_t    wash_mode;       /* 当前洗车模式 */
    int32_t        gantry_pos;      /* 龙门当前位置（脉冲数，非洗车期为最后已知值）*/
    bool           cloud_connected; /* 云端连接状态（snapshot 时读穿 connection port）*/
    safety_state_t safety_state;    /* 安全态（OK/WARNING/LOCKOUT）*/
    bool           has_alarm;       /* 是否存在活跃报警 */
    uint32_t       alarm_code;      /* 当前最高等级活跃报警码（无则 0）*/
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

/** @brief [wash_orchestrator] 更新当前洗车模式 */
void dev_ctx_set_wash_mode(wash_mode_t mode);

/** @brief [wash_orchestrator] 更新龙门位置（tick 循环调用）*/
void dev_ctx_set_gantry_pos(int32_t pos);

/** @brief [safety_supervisor] 更新安全态 */
void dev_ctx_set_safety_state(safety_state_t state);

/** @brief [safety_supervisor] 更新活跃报警投影（最高等级报警码）*/
void dev_ctx_set_alarm_state(bool has_alarm, uint32_t alarm_code);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_DEV_CTX_H */
