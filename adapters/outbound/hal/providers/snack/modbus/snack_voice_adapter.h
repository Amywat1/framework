/**
 * @file    snack_voice_adapter.h
 * @brief   Linux 真机语音模块 HAL 内部接口（仅供机型适配层绑定实例）
 * @author  HUWANGWEI
 * @date    2026-06-29
 *
 * @note    仅供 projects/<project>/wiring/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_voice_port 访问。
 */

#ifndef ADAPTERS_HAL_PROVIDERS_SNACK_MODBUS_SNACK_VOICE_ADAPTER_H
#define ADAPTERS_HAL_PROVIDERS_SNACK_MODBUS_SNACK_VOICE_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/** @brief  注册 snack_voice_adapter 操作集到 hal_voice_port；不读取配置、不初始化硬件。 */
void snack_voice_adapter_register(void);

/**
 * @brief  配置语音模块连接参数。
 * @param  serial_port  Modbus RTU 串口路径（如 "/dev/ttyS1"）
 * @param  baud         串口波特率
 * @param  modbus_addr  Modbus 从站地址（1~247）
 * @retval SW_OK        配置成功。
 * @retval SW_ERR_PARAM 参数非法。
 * @retval SW_ERR_BUSY  已完成配置，禁止重复覆盖。
 * @note   须在 hal_voice.init() 之前调用；真实 drv_voice_init() 只在 hal_voice.init() 中执行。
 */
sw_err_t snack_voice_adapter_configure(const char *serial_port, int baud, int modbus_addr);

#ifdef SNACK_VOICE_ADAPTER_UNIT_TEST
void snack_voice_adapter_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_PROVIDERS_SNACK_MODBUS_SNACK_VOICE_ADAPTER_H */
