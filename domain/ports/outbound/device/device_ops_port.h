/**
 * @file    device_ops_port.h
 * @brief   整机运行时操作端口（framework 应用层 → 项目 binding）
 * @author  HUWANGWEI
 * @date    2026-07-11
 *
 * @note    framework/application 不得直接调用项目命名机构 API；
 *          项目在 project_bind_device() 阶段注册实现。
 */

#ifndef DOMAIN_PORTS_OUTBOUND_DEVICE_DEVICE_OPS_PORT_H
#define DOMAIN_PORTS_OUTBOUND_DEVICE_DEVICE_OPS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/job/job_types.h"
#include "domain/op_mode/op_mode_types.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 整机运行时操作集合
 */
typedef struct {
    /**
     * @brief  启动整机清障/排空（CLEARING）
     * @note   仅启动异步流程；完成后须发布 EVT_CLEAR_MACHINE_DONE。
     * @note   延后完备停机统一走 safety_ops.deferred_stop，不在此重复暴露。
     */
    void (*clear_machine)(void);
    /**
     * @brief  启动一笔已放行作业（项目选择方案或放行上链）
     * @note   仅启动异步会话后返回；禁止在本调用内跑完整段工艺（会阻塞 dispatch/网关）。
     * @note   在放行锁内调用：不得同步调用 job_admit_*，作业事件须经事件总线异步发布。
     * @retval SW_ERR_PARAM 作业不可执行（如套餐未发布），放行核丢弃该单
     * @retval 其它非 SW_OK 暂时失败，订单保留在队首等下次放行
     */
    sw_err_t (*start_job)(const job_desc_t *job);
    /** @brief 中止全部在洗作业 */
    void (*abort_jobs)(wash_abort_cause_t cause);
    /** @brief 启动异步全机归位（完成后发 EVT_OP_MODE_HOME_COMPLETED） */
    sw_err_t (*home_device)(void);
    /**
     * @brief  DEV_CMD_MANUAL_ACTUATOR 副作用（act_id/param 由项目定义）
     * @note   只下发动作或启动定时/运动后立即返回；禁止在本调用内等待点动时长或
     *         阻塞等到机构到位（会占用 cmd_control，拖住后续 STOP_ALL 等命令）。
     *         运动完成/停止由项目自行观测，框架不为此发完成事件。
     */
    sw_err_t (*execute_manual_actuator)(uint32_t act_id, int32_t param);
    /**
     * @brief  DEV_CMD_STOP_ALL_OUTPUTS 副作用：落到与 cutout 等价的安全输出态
     * @note   裁决时有在洗作业（含裁决后才放行成功的）时，router 另调 abort_jobs(STOP_ALL)。
     */
    sw_err_t (*stop_all_outputs)(void);
    /**
     * @brief  下一辆/下一单是否可放行
     * @note   语义由机型定义；未注册（NULL）时框架不拦截。
     */
    bool (*is_admit_ready)(void);
} device_ops_t;

/**
 * @brief  注册整机运行时操作
 * @param  ops  操作表；须非 NULL
 */
sw_err_t device_ops_register(const device_ops_t *ops);

/**
 * @brief  获取已注册的整机操作表
 * @return 操作表指针；未注册时返回 NULL
 */
const device_ops_t *device_ops_get(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_DEVICE_DEVICE_OPS_PORT_H */
