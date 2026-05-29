#include "tests/test_support.h"
#include "src/application/coordinators/control_context_private.h"

#include "domain/services/recovery_state_machine.h"

static int test_stop_only_recovery_stops_all(void)
{
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_control_context(&driver_context);

    result = recovery_state_machine_execute(control_context_private_actuator_port(),
        0,
        RECOVERY_MODE_STOP_ONLY,
        0);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 0);

    test_release_control_context();
    return 0;
}

static int test_homing_recovery_runs_stop_and_roof_home(void)
{
    const char *failure_reason_code;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_control_context(&driver_context);

    failure_reason_code = 0;
    result = recovery_state_machine_execute(control_context_private_actuator_port(),
        control_context_private_sensor_port(),
        RECOVERY_MODE_HOME_ROOF_BRUSH,
        &failure_reason_code);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(failure_reason_code == 0);
    TEST_ASSERT(driver_context.stop_all_command_count == 1);
    TEST_ASSERT(driver_context.roof_home_command_count == 1);

    test_release_control_context();
    return 0;
}

static int test_homing_recovery_reports_feedback_timeout(void)
{
    const char *failure_reason_code;
    simulated_driver_context_t driver_context;
    operation_result_t result;

    test_setup_control_context(&driver_context);
    driver_context.roof_home_feedback_available = false;

    failure_reason_code = 0;
    result = recovery_state_machine_execute(control_context_private_actuator_port(),
        control_context_private_sensor_port(),
        RECOVERY_MODE_HOME_ROOF_BRUSH,
        &failure_reason_code);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_TIMEOUT);
    TEST_ASSERT(strcmp(failure_reason_code, "roof_home_feedback_timeout") == 0);

    test_release_control_context();
    return 0;
}

int main(void)
{
    if (test_stop_only_recovery_stops_all() != 0) {
        return 1;
    }
    if (test_homing_recovery_runs_stop_and_roof_home() != 0) {
        return 1;
    }
    if (test_homing_recovery_reports_feedback_timeout() != 0) {
        return 1;
    }
    return 0;
}
