/**
 * @file    modbus_stub.h
 * @brief   Modbus 桩控制接口（供 drv_vfd / drv_voice 单元测试使用）
 */

#ifndef TESTS_SUPPORT_MODBUS_STUB_H
#define TESTS_SUPPORT_MODBUS_STUB_H

#include <stdbool.h>
#include <stdint.h>

/* 重置所有桩状态：在每个测试的 setUp 中调用 */
void modbus_stub_reset(void);

/* 配置 modbus_new_rtu 是否返回 NULL（模拟上下文创建失败） */
void modbus_stub_set_new_rtu_fail(bool fail);

/* 配置 modbus_connect 返回值：0=成功，-1=失败 */
void modbus_stub_set_connect_result(int rc);

/* 配置 modbus_write_register 返回值：1=成功，-1=失败 */
void modbus_stub_set_write_result(int rc);

/* 配置 modbus_read_registers 返回值及寄存器值：rc=1成功/-1失败 */
void modbus_stub_set_read_result(int rc, uint16_t val);

/* 查询调用记录 */
int      modbus_stub_new_rtu_count(void);
int      modbus_stub_connect_count(void);
int      modbus_stub_write_count(void);
int      modbus_stub_last_write_reg(void);
uint16_t modbus_stub_last_write_val(void);

#endif /* TESTS_SUPPORT_MODBUS_STUB_H */
