/**
 * @file    project_hooks.h
 * @brief   项目生命周期阶段钩子接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    bootstrap_run() 固定阶段顺序为 register → configure_storage → load →
 *          configure → bind → validate → init → start；项目专属落地通过本文件钩子实现。
 */

#ifndef CORE_BOOTSTRAP_PROJECT_HOOKS_H
#define CORE_BOOTSTRAP_PROJECT_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  存储配置阶段：注入项目存储路径或存储后端参数。
 *
 * @retval SW_OK 配置成功。
 * @retval 其他  存储配置失败，bootstrap 中止。
 * @note   本阶段在 storage load 前执行；禁止读取文件或启动线程。
 */
sw_err_t project_configure_storage(void);

/**
 * @brief  配置阶段：下发项目专属 HAL 参数。
 *
 * @retval SW_OK 配置成功。
 * @retval 其他  项目配置失败，bootstrap 中止。
 * @note   本阶段禁止绑定实例、初始化硬件或启动线程。
 */
sw_err_t project_configure_hal(void);

/**
 * @brief  绑定阶段：绑定项目专属 HAL 实例、backend 或事件回调。
 *
 * @retval SW_OK 绑定成功。
 * @retval 其他  项目绑定失败，bootstrap 中止。
 * @note   本阶段禁止初始化硬件或启动线程。
 */
sw_err_t project_bind_hal(void);

/**
 * @brief  初始化阶段：初始化项目专属 HAL 组合层状态。
 *
 * @retval SW_OK 初始化成功。
 * @retval 其他  项目 HAL 初始化失败，bootstrap 中止。
 * @note   调用前框架已完成 hal_io.init()、hal_vfd.init() 与 hal_voice.init()。
 */
sw_err_t project_init_hal(void);

/**
 * @brief  配置阶段：建立项目安全默认态。
 *
 * @retval SW_OK 配置成功。
 * @retval 其他  安全或传感器配置失败，bootstrap 中止。
 */
sw_err_t project_configure_safety(void);

/**
 * @brief  配置阶段：配置项目入站/出站适配器。
 *
 * @retval SW_OK 配置成功。
 * @retval 其他  适配器配置失败，bootstrap 中止。
 * @note   调用前存储已完成 load；本阶段允许读取已加载配置，禁止初始化连接或启动线程。
 */
sw_err_t project_configure_adapters(void);

/**
 * @brief  绑定阶段：注册项目设备装配接口，例如 machine_ops。
 *
 * @retval SW_OK 绑定成功。
 * @retval 其他  项目设备装配失败，bootstrap 中止。
 */
sw_err_t project_bind_machine(void);

/**
 * @brief  绑定阶段：注入项目报警目录并建立报警适配绑定。
 *
 * @retval SW_OK 绑定成功。
 * @retval 其他  报警目录或适配绑定失败，bootstrap 中止。
 * @note   调用前框架已完成 alarm_registry_init() 与 safety_posture_init()。
 */
sw_err_t project_bind_alarm_catalog(void);

/**
 * @brief  校验阶段：执行项目启动前一致性校验。
 *
 * @retval SW_OK 校验成功。
 * @retval 其他  校验失败，bootstrap 中止。
 * @note   本阶段禁止初始化 watcher、读取实时 getter、发布事件或注册任务。
 */
sw_err_t project_validate(void);

/**
 * @brief  初始化阶段：初始化项目入站适配器，禁止启动后台线程。
 *
 * @retval SW_OK 初始化成功。
 * @retval 其他  适配器初始化失败，bootstrap 中止。
 */
sw_err_t project_init_adapters(void);

/**
 * @brief  注册阶段：登记项目运行期任务，禁止创建线程或启动硬件。
 *
 * @retval SW_OK 注册成功。
 * @retval 其他  任务注册失败，bootstrap 中止。
 * @note   已登记的任务由 start 阶段的 scheduler_start_all() 统一启动。
 */
sw_err_t project_register_runtime_tasks(void);

/**
 * @brief  启动阶段：启动无法纳入 scheduler 的项目运行期线程。
 *
 * @retval SW_OK 启动成功。
 * @retval 其他  项目运行期线程启动失败，bootstrap 中止。
 * @note   新增后台任务应优先接入 project_register_runtime_tasks()，再由 scheduler_start_all() 启动。
 */
sw_err_t project_start_runtime(void);

/**
 * @brief  致命错误兜底：切断项目安全输出。
 */
void project_assert_safe_outputs(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_BOOTSTRAP_PROJECT_HOOKS_H */
