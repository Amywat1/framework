/**
 * @file    project_hooks.h
 * @brief   项目生命周期钩子接口。
 *
 * `bootstrap_run()` 只编排框架通用的启动阶段；每个阶段中"具体项目如何落地"
 * 的部分通过本文件声明的钩子函数下沉到项目层实现（真机见
 * projects/<project>/wiring/project_hooks.c，仿真见
 * projects/<project>/wiring/project_hooks_sim.c）。framework/runtime/bootstrap/
 * 本身不得出现任何项目名或机型名。
 *
 * 与 wiring.h 的分工：wiring() 只做 port→adapter 注册，不执行任何初始化动作；
 * 本文件的钩子承担"注册完成后，项目专属的初始化/装配/线程启动"职责，
 * 调用时机见各函数注释与 bootstrap.c 中的调用顺序。
 */
#ifndef FRAMEWORK_RUNTIME_BOOTSTRAP_PROJECT_HOOKS_H
#define FRAMEWORK_RUNTIME_BOOTSTRAP_PROJECT_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  HAL 端口注册完成后的项目专属参数下发（如 VFD/语音实例参数表）。
 * @note   在 hal_vfd/hal_voice ops 完成 init() 之后、svc_param_init() 之前调用。
 */
sw_err_t project_hal_extra_setup(void);

/**
 * @brief  项目专属安全默认态与传感器初始化。
 * @note   在 wiring() 完成之后、领域/应用模块 init() 之前调用。
 */
sw_err_t project_safety_init(void);

/**
 * @brief  项目专属设备装配：把通用领域机构绑定到具体机型资源。
 * @note   在框架安全/应用编排模块 init() 之前调用。
 */
sw_err_t project_machine_setup(void);

/**
 * @brief  项目专属报警目录注入与通讯心跳初始化。
 * @note   须在 alarm_core_init()/safety_fsm_init()/safety_supervisor_init()
 *         完成之后调用（报警目录订阅关系已就绪）。
 */
sw_err_t project_alarm_catalog_init(void);

/**
 * @brief  项目专属入站适配器（云端命令、CLI 等）装配。
 * @note   在 deploy_store 加载之后调用。
 */
sw_err_t project_adapters_init(void);

/**
 * @brief  项目专属周期任务/线程启动。
 * @note   在 scheduler_start_all() 之前调用。
 */
sw_err_t project_start_threads(void);

/**
 * @brief  项目专属紧急安全输出（致命错误兜底）。
 * @note   由 event_bus 致命错误回调与 hal_io panic 回调共用；
 *         必须可重入、避免阻塞与动态内存分配。
 */
void project_assert_safe_outputs(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_RUNTIME_BOOTSTRAP_PROJECT_HOOKS_H */
