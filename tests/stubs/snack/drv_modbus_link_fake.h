#ifndef TESTS_STUBS_SNACK_DRV_MODBUS_LINK_FAKE_H
#define TESTS_STUBS_SNACK_DRV_MODBUS_LINK_FAKE_H

#include "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.h"
#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

void                  snack_modbus_fake_reset(void);
void                  snack_modbus_fake_set_init_result(sw_err_t ret);
void                  snack_modbus_fake_push_write_result(sw_err_t ret);
void                  snack_modbus_fake_set_read_value(uint16_t addr, uint16_t value);
bool                  snack_modbus_fake_init_called(void);
const char           *snack_modbus_fake_serial_port(void);
int                   snack_modbus_fake_baud(void);
int                   snack_modbus_fake_addr(void);
unsigned              snack_modbus_fake_write_count(void);
uint16_t              snack_modbus_fake_last_write_addr(void);
uint16_t              snack_modbus_fake_last_write_val(void);
uint16_t              snack_modbus_fake_last_read_addr(void);
drv_modbus_reg_type_t snack_modbus_fake_last_read_type(void);

#endif /* TESTS_STUBS_SNACK_DRV_MODBUS_LINK_FAKE_H */
