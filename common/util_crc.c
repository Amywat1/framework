/**
 * @file    util_crc.c
 * @brief   CRC 校验工具实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "util_crc.h"

/* -------------------------------------------------------------------------
 * CRC-8 （多项式 0x07）
 * ------------------------------------------------------------------------- */
uint8_t util_crc8(const uint8_t *p_data, uint32_t len)
{
    uint8_t  crc = 0x00U;
    uint32_t i;
    uint8_t  j;

    for (i = 0U; i < len; i++) {
        crc ^= p_data[i];
        for (j = 0U; j < 8U; j++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1U) ^ 0x07U);
            } else {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

/* -------------------------------------------------------------------------
 * CRC-16/MODBUS
 * ------------------------------------------------------------------------- */
uint16_t util_crc16_modbus(const uint8_t *p_data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t  j;

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)p_data[i];
        for (j = 0U; j < 8U; j++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (crc >> 1U) ^ 0xA001U;
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

/* -------------------------------------------------------------------------
 * CRC-32 （IEEE 802.3 以太网标准）
 * ------------------------------------------------------------------------- */
uint32_t util_crc32(const uint8_t *p_data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t  j;

    for (i = 0U; i < len; i++) {
        crc ^= (uint32_t)p_data[i];
        for (j = 0U; j < 8U; j++) {
            if ((crc & 0x00000001UL) != 0UL) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}
