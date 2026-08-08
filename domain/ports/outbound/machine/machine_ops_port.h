/**
 * @file    machine_ops_port.h
 * @brief   机型运行时操作端口（framework 应用层 → 项目 binding）
 * @author  HUWANGWEI
 * @date    2026-07-11
 *
 * @note    framework/application 不得直接调用项目命名机构 API；
 *          项目在 project_machine_setup() 阶段注册实现。
 */

#ifndef DOMAIN_PORTS_OUTBOUND_MACHINE_MACHINE_OPS_PORT_H
#define DOMAIN_PORTS_OUTBOUND_MACHINE_MACHINE_OPS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 机型运行时操作集合
 */
typedef struct {
    /**
     * @brief  启动中止归位清障（ABORT_HOMING；异于 home_device）
     * @note   仅启动异步流程；完成后须发布 EVT_ABORT_HOME_DONE。
     * @note   延后完备停机统一走 safety_ops.deferred_stop，不在此重复暴露。
     */
    void (*abort_home)(void);
    /** @brief 启动洗车会话（项目选择方案并驱动引擎） */
    sw_err_t (*start_wash)(wash_mode_t mode);
    /** @brief 中止洗车会话 */
    void (*abort_wash)(wash_abort_cause_t cause);
    /** @brief 启动异步全机归位（完成后发 EVT_OP_MODE_HOME_COMPLETED） */
    sw_err_t (*home_device)(void);
    /** @brief DEV_CMD_MANUAL_ACTUATOR 副作用（act_id/param 由项目定义） */
    sw_err_t (*execute_manual_actuator)(uint32_t act_id, int32_t param);
    /** @brief DEV_CMD_STOP_ALL_OUTPUTS 副作用 */
    sw_err_t (*stop_all_outputs)(void);
    /**
     * @brief  洗车准入是否就绪（START_WASH 附加门禁）
     * @note   语义由机型定义；framework 不解释具体传感器/机构条件。
     *         未注册（NULL）时框架不拦截。
     */
    bool (*is_wash_entry_ready)(void);
} machine_ops_t;

/**
 * @brief  注册机型运行时操作
 * @param  ops  操作表；须非 NULL
 */
sw_err_t machine_ops_register(const machine_ops_t *ops);

/**
 * @brief  获取已注册的机型操作表
 * @return 操作表指针；未注册时返回 NULL
 */
const machine_ops_t *machine_ops_get(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_MACHINE_MACHINE_OPS_PORT_H */
