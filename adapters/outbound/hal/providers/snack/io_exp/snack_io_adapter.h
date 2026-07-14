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
 * @brief  注册 snack_io_adapter 操作集到 hal_io_port。
 * @note   本接口只注册 ops，不读取配置、不初始化硬件、不启动线程。
 */
void snack_io_adapter_register(void);

/**
 * @brief  配置 IO 子板驱动参数。
 * @param  cfg  IO 子板配置（子板/点数/DI-DO 名称表），由机型层提供；内部按值保存一份。
 * @retval SW_OK        配置成功。
 * @retval SW_ERR_PARAM cfg 为空或配置内容非法。
 * @retval SW_ERR_BUSY  已完成配置，禁止重复覆盖。
 * @note   必须在 hal_io.init() 之前调用；实际 drv_io_init() 仍由 hal_io.init() 触发。
 */
sw_err_t snack_io_adapter_configure(const drv_io_cfg_t *cfg);

#ifdef SNACK_IO_ADAPTER_UNIT_TEST
void snack_io_adapter_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_PROVIDERS_SNACK_IO_EXP_SNACK_IO_ADAPTER_H */
