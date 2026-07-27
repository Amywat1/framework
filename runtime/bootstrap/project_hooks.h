/**
 * @file    project_hooks.h
 * @brief   项目生命周期阶段钩子接口（结构体注册方式）
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef CORE_BOOTSTRAP_PROJECT_HOOKS_H
#define CORE_BOOTSTRAP_PROJECT_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  项目生命周期钩子结构体
 *
 * 每个字段对应 bootstrap 启动序列的一个阶段。所有函数指针不允许为 NULL。
 */
typedef struct {
    /** 存储配置阶段：注入项目存储路径或存储后端参数。禁止读取文件或启动线程。*/
    sw_err_t (*configure_storage)(void);
    /** 配置阶段：下发项目专属 HAL 参数。禁止绑定实例或启动线程。*/
    sw_err_t (*configure_hal)(void);
    /** 绑定阶段：绑定项目专属 HAL 实例。禁止初始化硬件或启动线程。*/
    sw_err_t (*bind_hal)(void);
    /** 初始化阶段：初始化项目专属 HAL 组合层状态。*/
    sw_err_t (*init_hal)(void);
    /** 配置阶段：注入项目安全策略参数。禁止访问硬件或等待设备就绪。*/
    sw_err_t (*configure_safety)(void);
    /** 安全初始化阶段：HAL 初始化完成后建立项目故障安全状态。*/
    sw_err_t (*init_safety)(void);
    /** 配置阶段：配置项目入站/出站适配器。禁止初始化连接或启动线程。*/
    sw_err_t (*configure_adapters)(void);
    /** 绑定阶段：注册项目设备装配接口，例如 machine_ops。*/
    sw_err_t (*bind_machine)(void);
    /** 机器初始化阶段：HAL 初始化完成后初始化项目机构与执行器。*/
    sw_err_t (*init_machine)(void);
    /** 绑定阶段：注入项目报警目录并建立报警适配绑定。*/
    sw_err_t (*bind_alarm_catalog)(void);
    /** 校验阶段：执行项目启动前一致性校验。*/
    sw_err_t (*validate)(void);
    /** 初始化阶段：初始化项目入站适配器，禁止启动后台线程。*/
    sw_err_t (*init_adapters)(void);
    /** 注册阶段：登记项目运行期任务，禁止创建线程或启动硬件。*/
    sw_err_t (*register_runtime_tasks)(void);
    /** 启动阶段：启动无法纳入 scheduler 的项目运行期线程。*/
    sw_err_t (*start_runtime)(void);
    /** 致命错误兜底：切断项目安全输出（可以是空操作）。*/
    void (*assert_safe_outputs)(void);
} project_hooks_t;

/**
 * @brief  向 bootstrap 注册项目生命周期钩子
 * @param  hooks  钩子结构体指针，生命周期须覆盖整个运行时。所有函数指针不允许为 NULL。
 * @retval SW_OK        注册成功
 * @retval SW_ERR_PARAM hooks 为 NULL 或含 NULL 函数指针
 */
sw_err_t bootstrap_register_hooks(const project_hooks_t *hooks);

/**
 * @brief  项目必须实现此函数，在其中填充 project_hooks_t 并调用 bootstrap_register_hooks()
 * @retval SW_OK        注册成功
 * @retval SW_ERR_PARAM 钩子结构体中含 NULL 函数指针
 */
sw_err_t project_hooks_register(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_BOOTSTRAP_PROJECT_HOOKS_H */
