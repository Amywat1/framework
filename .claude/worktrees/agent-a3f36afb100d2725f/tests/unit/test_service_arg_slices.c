#include <stddef.h>

#include "domain/services/program_snapshot_service.h"
#include "domain/services/wash_execution_service.h"
#include "domain/services/wash_session_state_machine.h"
#include "tests/test_support.h"

_Static_assert(offsetof(wash_session_service_args_t, current_time_ms) + sizeof(unsigned long)
        == sizeof(wash_session_service_args_t),
    "wash_session_service_args_t must not carry extra shared state");
_Static_assert(offsetof(program_snapshot_service_args_t, current_time_ms) + sizeof(unsigned long)
        == sizeof(program_snapshot_service_args_t),
    "program_snapshot_service_args_t must not carry extra shared state");
_Static_assert(offsetof(wash_execution_service_args_t, current_time_ms) + sizeof(unsigned long)
        == sizeof(wash_execution_service_args_t),
    "wash_execution_service_args_t must not carry extra shared state");

static int verify_session_args_slice(void)
{
    wash_session_t wash_session;
    program_snapshot_t program_snapshot;
    unsigned long next_session_sequence;
    wash_session_service_args_t service_args;

    memset(&wash_session, 0, sizeof(wash_session));
    memset(&program_snapshot, 0, sizeof(program_snapshot));
    memset(&service_args, 0, sizeof(service_args));
    next_session_sequence = 0ul;

    service_args.wash_session = &wash_session;
    service_args.program_snapshot = &program_snapshot;
    service_args.next_session_sequence = &next_session_sequence;
    service_args.current_time_ms = 0ul;

    TEST_ASSERT(service_args.wash_session == &wash_session);
    TEST_ASSERT(service_args.program_snapshot == &program_snapshot);
    TEST_ASSERT(service_args.next_session_sequence == &next_session_sequence);
    TEST_ASSERT(service_args.current_time_ms == 0ul);
    return 0;
}

static int verify_execution_args_slice(void)
{
    wash_execution_t wash_execution;
    wash_session_t wash_session;
    wait_condition_t wait_condition;
    program_snapshot_t program_snapshot;
    actuator_port_t actuator_port;
    sensor_port_t sensor_port;
    unsigned long next_execution_sequence;
    unsigned long next_wait_condition_sequence;
    wash_execution_service_args_t service_args;

    memset(&wash_execution, 0, sizeof(wash_execution));
    memset(&wash_session, 0, sizeof(wash_session));
    memset(&wait_condition, 0, sizeof(wait_condition));
    memset(&program_snapshot, 0, sizeof(program_snapshot));
    memset(&actuator_port, 0, sizeof(actuator_port));
    memset(&sensor_port, 0, sizeof(sensor_port));
    memset(&service_args, 0, sizeof(service_args));
    next_execution_sequence = 0ul;
    next_wait_condition_sequence = 0ul;

    service_args.wash_execution = &wash_execution;
    service_args.wash_session = &wash_session;
    service_args.wait_condition = &wait_condition;
    service_args.program_snapshot = &program_snapshot;
    service_args.actuator_port = &actuator_port;
    service_args.sensor_port = &sensor_port;
    service_args.next_execution_sequence = &next_execution_sequence;
    service_args.next_wait_condition_sequence = &next_wait_condition_sequence;
    service_args.current_time_ms = 0ul;

    TEST_ASSERT(service_args.wash_execution == &wash_execution);
    TEST_ASSERT(service_args.wait_condition == &wait_condition);
    TEST_ASSERT(service_args.program_snapshot == &program_snapshot);
    TEST_ASSERT(service_args.next_execution_sequence == &next_execution_sequence);
    TEST_ASSERT(service_args.next_wait_condition_sequence == &next_wait_condition_sequence);
    TEST_ASSERT(service_args.current_time_ms == 0ul);
    return 0;
}

static int verify_snapshot_args_slice(void)
{
    program_snapshot_t program_snapshot;
    wash_program_t wash_program;
    program_repository_port_t program_repository_port;
    program_snapshot_service_args_t service_args;

    memset(&program_snapshot, 0, sizeof(program_snapshot));
    memset(&wash_program, 0, sizeof(wash_program));
    memset(&program_repository_port, 0, sizeof(program_repository_port));
    memset(&service_args, 0, sizeof(service_args));

    service_args.program_snapshot = &program_snapshot;
    service_args.wash_program = &wash_program;
    service_args.program_repository_port = &program_repository_port;
    service_args.current_time_ms = 0ul;

    TEST_ASSERT(service_args.program_snapshot == &program_snapshot);
    TEST_ASSERT(service_args.wash_program == &wash_program);
    TEST_ASSERT(service_args.program_repository_port == &program_repository_port);
    TEST_ASSERT(service_args.current_time_ms == 0ul);
    return 0;
}

int main(void)
{
    if (verify_session_args_slice() != 0) {
        return 1;
    }
    if (verify_execution_args_slice() != 0) {
        return 1;
    }
    if (verify_snapshot_args_slice() != 0) {
        return 1;
    }
    return 0;
}

