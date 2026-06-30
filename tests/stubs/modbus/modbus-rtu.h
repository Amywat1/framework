/**
 * @file    modbus-rtu.h
 * @brief   libmodbus RTU 测试桩头文件
 */

#ifndef MODBUS_STUB_RTU_H
#define MODBUS_STUB_RTU_H

#include "modbus/modbus.h"

modbus_t *modbus_new_rtu(const char *device, int baud, char parity,
                         int data_bit, int stop_bit);

#endif /* MODBUS_STUB_RTU_H */
