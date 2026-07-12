/**
 * @file    project_hooks.h
 * @brief   项目生命周期钩子接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    bootstrap_run() 编排框架通用启动阶段；项目专属落地通过本文件钩子实现。
 *          Demo 仿真实现见 demo/wiring/project_hooks_sim.c。
 */

#ifndef CORE_BOOTSTRAP_PROJECT_HOOKS_H
#define CORE_BOOTSTRAP_PROJECT_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  HAL 端口 init 完成后的项目专属参数下发
 */
sw_err_t project_hal_extra_setup(void);

/**
 * @brief  项目专属安全默认态与传感器初始化
 */
sw_err_t project_safety_init(void);

/**
 * @brief  项目设备装配（如 machine_ops 注册）
 */
sw_err_t project_machine_setup(void);

/**
 * @brief  项目报警目录注入（须在 alarm_registry_init 之后）
 */
sw_err_t project_alarm_catalog_init(void);

/**
 * @brief  项目上报调度器初始化（云端模块；Demo 可为空实现）
 */
sw_err_t project_report_scheduler_init(void);

/**
 * @brief  项目入站适配器装配（云端/CLI）
 */
sw_err_t project_adapters_init(void);

/**
 * @brief  项目专属周期任务注册（须在 scheduler_start_all 之前）
 */
sw_err_t project_start_threads(void);

/**
 * @brief  致命错误兜底：切断安全输出
 */
void project_assert_safe_outputs(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_BOOTSTRAP_PROJECT_HOOKS_H */
