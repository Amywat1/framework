/**
 * @file    machine_ops_stubs.c
 * @brief   单元测试用 machine_ops_port 最小桩
 */

#include "framework/ports/outbound/machine/machine_ops_port.h"
#include "framework/common/sw_error.h"
#include <stddef.h>

static sw_err_t stub_home_device(void)
{
    return SW_OK;
}

void machine_ops_register_test_stub(void)
{
    static const machine_ops_t s_stub = {
        .deferred_stop_all    = NULL,
        .safety_home          = NULL,
        .home_device          = stub_home_device,
        .read_gantry_position = NULL,
    };

    machine_ops_register(&s_stub);
}
