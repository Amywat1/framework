#include "adapters/config/json_program_parser.h"
#include "domain/model/program_validation.h"
#include "tests/test_support.h"
#include "src/application/coordinators/system_context_private.h"

static int test_valid_program_fixture_passes(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_program.segment_count == 4);
    TEST_ASSERT(program_validation_validate(&wash_program).ok);
    return 0;
}

static int test_unsupported_conditional_control_is_rejected(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_invalid_unsupported_conditional_controls.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_UNSUPPORTED);
    return 0;
}

static int test_missing_exit_actions_is_rejected(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_invalid_missing_exit_actions.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    return 0;
}

static int test_invalid_position_semantics_is_rejected(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_invalid_position_semantics.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    return 0;
}

static int test_valid_json_edge_cases_pass(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_valid_json_edge_cases.json",
        &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_program.segment_count == 1);
    TEST_ASSERT(strcmp(wash_program.program_name, "edge {cfg} \"ok\"") == 0);
    return 0;
}

static int test_duplicate_keys_are_rejected(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_invalid_duplicate_keys.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
    return 0;
}

static int test_invalid_sequence_order_is_rejected(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_invalid_sequence_order.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
    return 0;
}

static int test_repository_save_is_rejected_without_serializer(void)
{
    operation_result_t result;
    wash_program_t wash_program;
    simulated_driver_context_t driver_context;
    system_context_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(system_context_private_runtime(system_context)->program_repository_port.save_program != 0);
    TEST_ASSERT(system_context_private_runtime(system_context)->program_repository_port.save_program(
        system_context_private_runtime(system_context)->program_repository_port.context,
        &wash_program) != 0);
    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (test_valid_program_fixture_passes() != 0) {
        return 1;
    }
    if (test_unsupported_conditional_control_is_rejected() != 0) {
        return 1;
    }
    if (test_missing_exit_actions_is_rejected() != 0) {
        return 1;
    }
    if (test_invalid_position_semantics_is_rejected() != 0) {
        return 1;
    }
    if (test_valid_json_edge_cases_pass() != 0) {
        return 1;
    }
    if (test_duplicate_keys_are_rejected() != 0) {
        return 1;
    }
    if (test_invalid_sequence_order_is_rejected() != 0) {
        return 1;
    }
    if (test_repository_save_is_rejected_without_serializer() != 0) {
        return 1;
    }
    return 0;
}

