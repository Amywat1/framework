/**
 * @file    m8_voice_table.h
 * @brief   M8 机型语音模块实例配置�?
 * @author  HUWANGWEI
 * @date    2026-06-29
 *
 * @note    本文件描�?M8 机型语音模块的通信参数�?
 *          驱动层（drv_voice）与 HAL adapter（hal_voice_linux）只保留通用接口�?
 *          具体串口、波特率、Modbus 地址统一收口到这里�?
 */

#ifndef CONFIG_MACHINE_M8_VOICE_TABLE_H
#define CONFIG_MACHINE_M8_VOICE_TABLE_H

#include "projects/m8/config/m8_machine_config.h"

#define M8_VOICE_SERIAL_PORT  CFG_VOICE_SERIAL_PORT
#define M8_VOICE_BAUD         CFG_VOICE_BAUD
#define M8_VOICE_MODBUS_ADDR  CFG_VOICE_MODBUS_ADDR

#endif /* CONFIG_MACHINE_M8_VOICE_TABLE_H */
