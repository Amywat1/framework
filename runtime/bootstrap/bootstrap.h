/**
 * @file    bootstrap.h
 * @brief   系统启动入口接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef CORE_BOOTSTRAP_BOOTSTRAP_H
#define CORE_BOOTSTRAP_BOOTSTRAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  系统完整启动序列（基础设施 → 安全 → 应用 → 适配器 → 线程）
 * @retval SW_OK 启动成功
 */
sw_err_t bootstrap_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_BOOTSTRAP_BOOTSTRAP_H */
