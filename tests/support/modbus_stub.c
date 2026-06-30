/**
 * @file    modbus_stub.c
 * @brief   libmodbus 函数桩实现 + 桩控制接口（用于 drv_vfd / drv_voice 单元测试）
 *
 * 桩采用全局变量记录调用状态，测试用例通过 modbus_stub_* 接口配置行为和查询结果。
 * 不同于真实 libmodbus，此处 modbus_connect/write/read 不访问任何串口。
 */

#include "tests/support/modbus_stub.h"
#include "modbus/modbus.h"
#include "modbus/modbus-rtu.h"

#include <stddef.h>

/* 固定假上下文：modbus_new_rtu 成功时返回此指针，避免返回 NULL */
static struct _modbus s_dummy_ctx;

/* ---------- 桩配置状态 ---------- */
static bool     s_new_rtu_fail   = false;
static int      s_connect_rc     = 0;   /* 默认连接成功 */
static int      s_write_rc       = 1;   /* libmodbus 写成功返回 1 */
static int      s_read_rc        = 1;   /* libmodbus 读成功返回 1 */
static uint16_t s_read_val       = 0U;

/* ---------- 调用计数 ---------- */
static int      s_new_rtu_count  = 0;
static int      s_connect_count  = 0;
static int      s_write_count    = 0;
static int      s_last_write_reg = -1;
static uint16_t s_last_write_val = 0U;

/* -------------------------------------------------------------------------
 * 桩控制接口
 * ------------------------------------------------------------------------- */
void modbus_stub_reset(void)
{
    s_new_rtu_fail   = false;
    s_connect_rc     = 0;
    s_write_rc       = 1;
    s_read_rc        = 1;
    s_read_val       = 0U;
    s_new_rtu_count  = 0;
    s_connect_count  = 0;
    s_write_count    = 0;
    s_last_write_reg = -1;
    s_last_write_val = 0U;
}

void modbus_stub_set_new_rtu_fail(bool fail)           { s_new_rtu_fail = fail; }
void modbus_stub_set_connect_result(int rc)            { s_connect_rc = rc; }
void modbus_stub_set_write_result(int rc)              { s_write_rc = rc; }
void modbus_stub_set_read_result(int rc, uint16_t val) { s_read_rc = rc; s_read_val = val; }

int      modbus_stub_new_rtu_count(void)  { return s_new_rtu_count; }
int      modbus_stub_connect_count(void)  { return s_connect_count; }
int      modbus_stub_write_count(void)    { return s_write_count; }
int      modbus_stub_last_write_reg(void) { return s_last_write_reg; }
uint16_t modbus_stub_last_write_val(void) { return s_last_write_val; }

/* -------------------------------------------------------------------------
 * libmodbus 函数桩实现
 * ------------------------------------------------------------------------- */
modbus_t *modbus_new_rtu(const char *device, int baud, char parity,
                         int data_bit, int stop_bit)
{
    (void)device; (void)baud; (void)parity; (void)data_bit; (void)stop_bit;
    s_new_rtu_count++;
    return s_new_rtu_fail ? NULL : &s_dummy_ctx;
}

int modbus_set_slave(modbus_t *ctx, int slave)
{
    (void)ctx; (void)slave;
    return 0;
}

int modbus_set_response_timeout(modbus_t *ctx, uint32_t to_sec, uint32_t to_usec)
{
    (void)ctx; (void)to_sec; (void)to_usec;
    return 0;
}

int modbus_connect(modbus_t *ctx)
{
    (void)ctx;
    s_connect_count++;
    return s_connect_rc;
}

int modbus_write_register(modbus_t *ctx, int reg_addr, int value)
{
    (void)ctx;
    s_write_count++;
    s_last_write_reg = reg_addr;
    s_last_write_val = (uint16_t)value;
    return s_write_rc;
}

int modbus_read_registers(modbus_t *ctx, int addr, int nb, uint16_t *dest)
{
    (void)ctx; (void)addr; (void)nb;
    if ((s_read_rc >= 0) && (dest != NULL)) {
        dest[0] = s_read_val;
    }
    return s_read_rc;
}

void modbus_close(modbus_t *ctx) { (void)ctx; }
void modbus_free(modbus_t *ctx)  { (void)ctx; }
