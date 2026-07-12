/**
 * @file    snack_io_adapter.h
 * @brief   Linux 真机数字 IO HAL 内部接口（仅供机型适配层配置实例）
 * @author  HUWANGWEI
 * @date    2026-07-07
 *
 * @note    仅供 projects/<project>/adapters/hal/ 在端口注册阶段调用；
 *          业务层仍通过 hal_io_port 访问。
 */

#ifndef ADAPTERS_HAL_PROVIDERS_SNACK_IO_EXP_SNACK_IO_ADAPTER_H
#define ADAPTERS_HAL_PROVIDERS_SNACK_IO_EXP_SNACK_IO_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"

/**
 * @brief  注册 snack_io_adapter 实现到 hal_io_port
 * @param  cfg  IO 子板配置（子板/点数/DI-DO 名称表），由机型层提供；
 *              内部按值保存一份，cfg 本身可为调用点的局部变量
 * @note   须在机型 wiring 的端口注册阶段调用；实际 drv_io_init() 由
 *         hal_io.init() 在 bootstrap 初始化阶段触发
 */
void snack_io_adapter_register(const drv_io_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_PROVIDERS_SNACK_IO_EXP_SNACK_IO_ADAPTER_H */
