/**
 * @file    util_crc.h
 * @brief   CRC 校验工具（CRC-8 / CRC-16 / CRC-32）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef UTIL_CRC_H
#define UTIL_CRC_H

#include "sw_types.h"

/**
 * @brief  计算 CRC-8（多项式 0x07）
 * @param  p_data  数据指针
 * @param  len     数据长度（字节）
 * @retval CRC-8 校验值
 */
uint8_t util_crc8(const uint8_t *p_data, uint32_t len);

/**
 * @brief  计算 CRC-16/MODBUS（多项式 0x8005，初始值 0xFFFF）
 * @param  p_data  数据指针
 * @param  len     数据长度（字节）
 * @retval CRC-16 校验值
 */
uint16_t util_crc16_modbus(const uint8_t *p_data, uint32_t len);

/**
 * @brief  计算 CRC-32（多项式 0x04C11DB7，以太网标准）
 * @param  p_data  数据指针
 * @param  len     数据长度（字节）
 * @retval CRC-32 校验值
 */
uint32_t util_crc32(const uint8_t *p_data, uint32_t len);

#endif /* UTIL_CRC_H */
