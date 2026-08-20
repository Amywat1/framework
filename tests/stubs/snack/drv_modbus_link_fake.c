#include "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link_internal.h"

#include <string.h>

typedef struct {
    bool                  init_called;
    bool                  ready;
    const char           *serial_port;
    int                   baud;
    int                   modbus_addr;
    uint16_t              last_write_addr;
    uint16_t              last_write_val;
    uint16_t              last_read_addr;
    drv_modbus_reg_type_t last_read_type;
    uint16_t              read_addr[16];
    uint16_t              read_value[16];
    unsigned              read_value_count;
    unsigned              write_count;
    sw_err_t              init_result;
    sw_err_t              write_results[16];
    unsigned              write_result_count;
} snack_modbus_fake_t;

static snack_modbus_fake_t s_fake;

void snack_modbus_fake_reset(void)
{
    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.init_result = SW_OK;
}

void snack_modbus_fake_set_init_result(sw_err_t ret)
{
    s_fake.init_result = ret;
}

void snack_modbus_fake_push_write_result(sw_err_t ret)
{
    if (s_fake.write_result_count < (sizeof(s_fake.write_results) / sizeof(s_fake.write_results[0]))) {
        s_fake.write_results[s_fake.write_result_count++] = ret;
    }
}

void snack_modbus_fake_set_read_value(uint16_t addr, uint16_t value)
{
    if (s_fake.read_value_count < (sizeof(s_fake.read_addr) / sizeof(s_fake.read_addr[0]))) {
        s_fake.read_addr[s_fake.read_value_count]  = addr;
        s_fake.read_value[s_fake.read_value_count] = value;
        s_fake.read_value_count++;
    }
}

bool snack_modbus_fake_init_called(void)
{
    return s_fake.init_called;
}

const char *snack_modbus_fake_serial_port(void)
{
    return s_fake.serial_port;
}

int snack_modbus_fake_baud(void)
{
    return s_fake.baud;
}

int snack_modbus_fake_addr(void)
{
    return s_fake.modbus_addr;
}

unsigned snack_modbus_fake_write_count(void)
{
    return s_fake.write_count;
}

uint16_t snack_modbus_fake_last_write_addr(void)
{
    return s_fake.last_write_addr;
}

uint16_t snack_modbus_fake_last_write_val(void)
{
    return s_fake.last_write_val;
}

uint16_t snack_modbus_fake_last_read_addr(void)
{
    return s_fake.last_read_addr;
}

drv_modbus_reg_type_t snack_modbus_fake_last_read_type(void)
{
    return s_fake.last_read_type;
}

sw_err_t drv_modbus_link_init(drv_modbus_link_t *link,
                              const char        *serial_port,
                              int                baud,
                              int                modbus_addr,
                              uint32_t           timeout_us,
                              uint16_t           reconnect_threshold)
{
    (void)timeout_us;
    (void)reconnect_threshold;

    s_fake.init_called = true;
    s_fake.serial_port = serial_port;
    s_fake.baud        = baud;
    s_fake.modbus_addr = modbus_addr;
    if (s_fake.init_result == SW_OK) {
        memset(link, 0, sizeof(*link));
        link->serial_port = serial_port;
        link->baud        = baud;
        link->modbus_addr = modbus_addr;
        s_fake.ready      = true;
    }
    return s_fake.init_result;
}

bool drv_modbus_link_is_ready(const drv_modbus_link_t *link)
{
    return s_fake.ready && (link != NULL) && (link->serial_port != NULL);
}

sw_err_t drv_modbus_link_read_reg(drv_modbus_link_t *link, drv_modbus_reg_type_t type, uint16_t addr, uint16_t *p_val)
{
    unsigned i;

    if (!drv_modbus_link_is_ready(link)) {
        return SW_ERR_NOT_INIT;
    }
    if (p_val == NULL) {
        return SW_ERR_PARAM;
    }
    if ((type != DRV_MODBUS_REG_HOLDING) && (type != DRV_MODBUS_REG_INPUT)) {
        return SW_ERR_PARAM;
    }
    s_fake.last_read_type = type;
    s_fake.last_read_addr = addr;
    for (i = 0; i < s_fake.read_value_count; i++) {
        if (s_fake.read_addr[i] == addr) {
            *p_val = s_fake.read_value[i];
            return SW_OK;
        }
    }
    *p_val = 0;
    return SW_OK;
}

sw_err_t drv_modbus_link_write_reg(drv_modbus_link_t *link, uint16_t addr, uint16_t val)
{
    sw_err_t ret = SW_OK;

    if (!drv_modbus_link_is_ready(link)) {
        return SW_ERR_NOT_INIT;
    }

    s_fake.last_write_addr = addr;
    s_fake.last_write_val  = val;
    if (s_fake.write_count < s_fake.write_result_count) {
        ret = s_fake.write_results[s_fake.write_count];
    }
    s_fake.write_count++;
    return ret;
}
