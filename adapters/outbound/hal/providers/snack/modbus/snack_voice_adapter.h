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

/** @brief  注册 snack_voice_adapter 实现到 hal_voice_port */
void snack_voice_adapter_register(void);

/**
 * @brief  初始化语音模块实例并与驱动绑定
 * @param  serial_port  Modbus RTU 串口路径（如 "/dev/ttyS1"）
 * @param  baud         串口波特率
 * @param  modbus_addr  Modbus 从站地址（1~247）
 * @retval SW_OK / SW_ERR_PARAM / SW_ERR_HW
 * @note   须在 snack_voice_adapter_register() 之后调用；
 *         由机型适配层（如 m8_voice_setup）在 bootstrap 阶段完成
 */
sw_err_t snack_voice_adapter_init(const char *serial_port, int baud, int modbus_addr);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_PROVIDERS_SNACK_MODBUS_SNACK_VOICE_ADAPTER_H */
