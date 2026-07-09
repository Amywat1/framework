/**
 * @file    dev_ctx.h
 * @brief   设备状态快照接口（统一只读视图）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    dev_ctx 是设备当前运行状态的集中只读视图，供上报/CLI 查询使用。
 *          写入权限严格分片，各字段只允许指定模块更新：
 *            operational_mode / service_enabled / estop_active → mode_projection
 *            safety_state / alarm_state                      → safety_fsm
 *            wash_mode / gantry_pos                            → wash_orchestrator
 *            cloud_connected                                   → 读穿 cloud_link_port
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

/** @brief  设备状态快照（值拷贝，线程安全） */
typedef struct
{
    operational_mode_t operational_mode; /**< 运行模式聚合态 */
    bool               service_enabled;  /**< 业务服务开关（与运行模式正交） */
    bool               estop_active;     /**< 急停是否激活 */
    wash_mode_t        wash_mode;        /**< 当前洗车模式 */
    int32_t            gantry_pos;       /**< 龙门位置（脉冲数） */
    bool               cloud_connected;    /**< 云端连接状态 */
    safety_state_t     safety_state;     /**< 安全态（OK/WARNING/LOCKOUT） */
    bool               has_alarm;        /**< 是否存在活跃报警 */
    uint32_t           alarm_code;       /**< 最高等级活跃报警码 */
} device_context_t;

/**
 * @brief  初始化 dev_ctx（清零所有字段为安全初始值）
 */
sw_err_t dev_ctx_init(void);

/**
 * @brief  获取当前状态快照（值拷贝，线程安全）
 */
device_context_t dev_ctx_snapshot(void);

/**
 * @brief  读取运行模式（线程安全）
 */
operational_mode_t dev_ctx_get_operational_mode(void);

/** @brief [mode_projection] 更新运行模式 */
void dev_ctx_set_operational_mode(operational_mode_t mode);

/** @brief [mode_projection] 更新业务服务开关 */
void dev_ctx_set_service_enabled(bool enabled);

/** @brief [mode_projection] 更新急停激活标志 */
void dev_ctx_set_estop_active(bool active);

/** @brief [wash_orchestrator] 更新当前洗车模式 */
void dev_ctx_set_wash_mode(wash_mode_t mode);

/** @brief [wash_orchestrator] 更新龙门位置 */
void dev_ctx_set_gantry_pos(int32_t pos);

/** @brief [safety_supervisor] 更新安全态 */
void dev_ctx_set_safety_state(safety_state_t state);

/** @brief [safety_supervisor] 更新活跃报警投影 */
void dev_ctx_set_alarm_state(bool has_alarm, uint32_t alarm_code);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_DEV_CTX_H */
