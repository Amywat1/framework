/**
 * @file    m8_machine_ops.c
 * @brief   M8 机型 machine_ops_port 实现
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "framework/ports/outbound/machine/machine_ops_port.h"
#include "framework/domain/device_control/patterns/fluid_path.h"
#include "projects/m8/domain/mechanism/gantry.h"
#include "projects/m8/domain/mechanism/m8_brush_rotation.h"
#include "framework/runtime/bootstrap/project_hooks.h"

static void m8_deferred_stop_all(void)
{
    (void)gantry_stop();
    (void)brush_stop_all();
    (void)fluid_path_all_off();
}

static void m8_safety_home(void)
{
    (void)brush_stop_all();
}

static sw_err_t m8_home_device(void)
{
    return gantry_home();
}

void m8_machine_ops_register(void)
{
    static const machine_ops_t s_m8_ops = {
        .deferred_stop_all = m8_deferred_stop_all,
        .safety_home       = m8_safety_home,
        .home_device       = m8_home_device,
    };

    machine_ops_register(&s_m8_ops);
}
