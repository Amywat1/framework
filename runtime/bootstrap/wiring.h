/**
 * @file    wiring.h
 * @brief   依赖接线接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    真机构建使用 wiring.c。
 *          仿真构建使用 wiring_sim.c。
 */

#ifndef CORE_BOOTSTRAP_WIRING_H
#define CORE_BOOTSTRAP_WIRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册项目选择的 port provider 与静态适配器。
 *
 * @retval SW_OK 注册成功。
 * @retval 其他  provider 注册失败，bootstrap 中止。
 * @note   本函数只允许执行 register 动作；禁止读取存储、配置实例、绑定业务对象、
 *         初始化硬件、创建线程或启动后台任务。
 */
sw_err_t wiring(void);

#ifdef __cplusplus
}
#endif

#endif /* CORE_BOOTSTRAP_WIRING_H */
