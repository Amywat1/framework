#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

#ifndef WASH_CORE_ARCHIVE_PATH
#define WASH_CORE_ARCHIVE_PATH ""
#endif

#ifndef WASH_CORE_NM
#define WASH_CORE_NM ""
#endif

static int build_nm_command(char *command, size_t command_size)
{
    const char *nm_program;

    if (command == 0 || command_size == 0 || WASH_CORE_ARCHIVE_PATH[0] == '\0') {
        return 0;
    }

    nm_program = WASH_CORE_NM[0] != '\0' ? WASH_CORE_NM : "nm";
    snprintf(command, command_size, "\"%s\" \"%s\"", nm_program, WASH_CORE_ARCHIVE_PATH);
    return 1;
}

static int command_output_contains(const char *command, const char *needle)
{
    char line[512];
    FILE *pipe_handle;

    pipe_handle = popen(command, "r");
    if (pipe_handle == 0) {
        return 0;
    }

    while (fgets(line, sizeof(line), pipe_handle) != 0) {
        if (strstr(line, needle) != 0) {
            pclose(pipe_handle);
            return 1;
        }
    }

    pclose(pipe_handle);
    return 0;
}

static int verify_removed_compat_symbols(void)
{
    char command[1024];

    TEST_ASSERT(build_nm_command(command, sizeof(command)) == 1);
    TEST_ASSERT(command_output_contains(command, "start_wash_cycle_execute") == 0);
    TEST_ASSERT(command_output_contains(command, "stop_wash_cycle_execute") == 0);
    TEST_ASSERT(command_output_contains(command, "stop_wash_cycle_with_reason_execute") == 0);
    TEST_ASSERT(command_output_contains(command, "acknowledge_fault_execute") == 0);
    TEST_ASSERT(command_output_contains(command, "compatibility_trigger_runner_execute") == 0);
    TEST_ASSERT(command_output_contains(command, "process_formal_command_execute") == 1);
    TEST_ASSERT(command_output_contains(command, "cli_command_adapter_execute_formal_line") == 0);
    return 0;
}

static int verify_formal_path_still_works(void)
{
    simulated_driver_context_t driver_context;
    char response_line[512];
    operation_result_t result;

    test_setup_system_context( &driver_context);
    result = test_load_runtime_program_from_fixture(
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        0);
    TEST_ASSERT(result.ok);
    result = test_homing_system_and_flush();
    TEST_ASSERT(result.ok);
    result = test_process_command_and_flush(
        "start wash_step_control_v1",
        response_line,
        sizeof(response_line));
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strstr(response_line, "accepted=true") != 0);
    TEST_ASSERT(device_runtime_private_runtime_mutable()->wash_session.session_state == SESSION_STATE_RUNNING);
    return 0;
}

int main(void)
{
    if (verify_removed_compat_symbols() != 0) {
        return 1;
    }
    if (verify_formal_path_still_works() != 0) {
        return 1;
    }
    return 0;
}

