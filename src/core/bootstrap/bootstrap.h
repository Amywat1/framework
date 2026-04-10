/**
 * @file    bootstrap.h
 * @brief   系统启动入口接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    bootstrap_run() 完成所有模块初始化并启动全部线程，
 *          由 app_main（snack 框架回调）调用。
 */

#ifndef CORE_BOOTSTRAP_BOOTSTRAP_H
#define CORE_BOOTSTRAP_BOOTSTRAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  系统完整启动序列（见注释中的初始化顺序）
 * @retval SW_OK 启动成功（scheduler 已启动，程序进入线程驱动阶段）
 * @retval 其他  某步骤初始化失败，应终止程序
 */
sw_err_t bootstrap_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_BOOTSTRAP_BOOTSTRAP_H */
