#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "adapters/machine/m8/m8_signal_filter.h"
#include "config/machine/m8_signal_table.h"
#include "core/event_bus/event_bus.h"
#include "domain/model/alarm_code.h"
#include "domain/safety/alarm_core.h"
#include "ports/hal/hal_io_port.h"
#include "adapters/hal/generic/hal_sensor.h"
#include "adapters/machine/m8/m8_sensor_setup.h"
#include "ports/hal/hal_vfd_port.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static bool s_di_estop;
static bool s_di_gantry_fwd;
static bool s_di_gantry_rev;
static bool s_di_lift_up;
static bool s_di_lift_down;

static void reset_inputs(void)
{
    s_di_estop      = true;
    s_di_gantry_fwd = false;
    s_di_gantry_rev = false;
    s_di_lift_up    = false;
    s_di_lift_down  = false;
}

static sw_err_t mock_do_set(io_do_t pin, bool val)
{
    (void)pin;
    (void)val;
    return SW_OK;
}

static bool mock_di_read(io_di_t pin)
{
    uint16_t raw = io_di_raw(pin);

    if (raw == io_di_raw(m8_signal_table[M8_SIG_ESTOP].io_id))
    {
        return s_di_estop;
    }
    if (raw == io_di_raw(m8_signal_table[M8_SIG_GANTRY_FWD_LIM].io_id))
    {
        return s_di_gantry_fwd;
    }
    if (raw == io_di_raw(m8_signal_table[M8_SIG_GANTRY_REV_LIM].io_id))
    {
        return s_di_gantry_rev;
    }
    if (raw == io_di_raw(m8_signal_table[M8_SIG_LIFT_UP_LIM].io_id))
    {
        return s_di_lift_up;
    }
    if (raw == io_di_raw(m8_signal_table[M8_SIG_LIFT_DOWN_LIM].io_id))
    {
        return s_di_lift_down;
    }

    return false;
}

static void mock_register_debug_input_cb(hal_io_debug_input_cb_t cb)
{
    (void)cb;
}

static void mock_register_board_status_cb(hal_io_board_status_cb_t cb)
{
    (void)cb;
}

static sw_err_t mock_vfd_get_fault_code(hal_vfd_id_t vfd_id, uint16_t *p_code)
{
    (void)vfd_id;
    (void)p_code;
    return SW_ERR_COMM;
}

static const hal_vfd_ops_t s_vfd_ops = {
    .get_fault_code = mock_vfd_get_fault_code,
};

static const hal_io_ops_t s_io_ops = {
    .do_set                   = mock_do_set,
    .di_read                  = mock_di_read,
    .register_debug_input_cb  = mock_register_debug_input_cb,
    .register_board_status_cb = mock_register_board_status_cb,
};

static void run_filter_ticks(int count)
{
    for (int i = 0; i < count; i++)
    {
        m8_signal_filter_tick();
        alarm_core_tick_ms(ALARM_POLL_PERIOD_MS);
    }
}

static void init_fixture(void)
{
    reset_inputs();
    (void)event_bus_init();
    hal_io_register(&s_io_ops);
    hal_sensor_generic_register();
    hal_vfd_register(&s_vfd_ops);
    (void)alarm_core_init();
    (void)m8_sensor_setup();
    (void)m8_alarm_adapt_init();
}

static void test_single_limit_does_not_trigger_alarm(void)
{
    printf("TC-1: single limit does not directly alarm\n");

    init_fixture();
    s_di_gantry_fwd = true;

    run_filter_ticks(3);

    assert(m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM));
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(!alarm_core_is_active(ALARM_CODE_GANTRY_REV_LIM));

    printf("  PASS\n");
}

static void test_dual_gantry_limits_trigger_alarm(void)
{
    printf("TC-2: dual gantry limits trigger alarm\n");

    init_fixture();
    s_di_gantry_fwd = true;
    s_di_gantry_rev = true;

    run_filter_ticks(3);

    assert(m8_signal_is_active(M8_SIG_GANTRY_FWD_LIM));
    assert(m8_signal_is_active(M8_SIG_GANTRY_REV_LIM));
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_FWD_LIM));
    assert(alarm_core_is_active(ALARM_CODE_GANTRY_REV_LIM));

    printf("  PASS\n");
}

static void test_dual_lift_limits_suppressed_without_install(void)
{
    printf("TC-3: dual lift limits suppressed when not installed\n");

    init_fixture();
    s_di_lift_up   = true;
    s_di_lift_down = true;

    run_filter_ticks(3);

    assert(m8_signal_is_active(M8_SIG_LIFT_UP_LIM));
    assert(m8_signal_is_active(M8_SIG_LIFT_DOWN_LIM));
    assert(!alarm_core_is_active(ALARM_CODE_LIFT_UP_LIM));
    assert(!alarm_core_is_active(ALARM_CODE_LIFT_DOWN_LIM));

    printf("  PASS\n");
}

int main(void)
{
    printf("=== test_m8_limit_alarm_adapt ===\n");

    test_single_limit_does_not_trigger_alarm();
    test_dual_gantry_limits_trigger_alarm();
    test_dual_lift_limits_suppressed_without_install();

    printf("=== ALL PASSED ===\n");
    return 0;
}
