/**
 * @file    modbus.h
 * @brief   libmodbus 测试桩头文件（替代真实 libmodbus，仅用于单元测试构建）
 * @note    仅声明 drv_vfd / drv_voice 实际调用的函数，不必还原完整 API。
 */

#ifndef MODBUS_STUB_MODBUS_H
#define MODBUS_STUB_MODBUS_H

#include <stdint.h>

/* 不透明 Modbus 上下文（桩实现通过固定静态实例满足非 NULL 校验） */
struct _modbus {
    int _dummy;
};
typedef struct _modbus modbus_t;

int  modbus_set_slave(modbus_t *ctx, int slave);
int  modbus_set_response_timeout(modbus_t *ctx, uint32_t to_sec, uint32_t to_usec);
int  modbus_connect(modbus_t *ctx);
int  modbus_write_register(modbus_t *ctx, int reg_addr, int value);
int  modbus_read_registers(modbus_t *ctx, int addr, int nb, uint16_t *dest);
void modbus_close(modbus_t *ctx);
void modbus_free(modbus_t *ctx);

#endif /* MODBUS_STUB_MODBUS_H */
