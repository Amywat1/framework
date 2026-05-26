#include "adapters/config/json_program_parser.h"
#include "domain/model/program_validation.h"
#include "tests/test_support.h"
#include "src/application/coordinators/device_runtime_private.h"

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

static int test_default_fields_are_applied_when_optional_members_are_missing(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse("configs/programs/standard_wash.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(wash_program.default_segment_timeout_ms == 15000);
    TEST_ASSERT(wash_program.default_exit_timeout_ms == 5000);
    TEST_ASSERT(wash_program.segment_count == 4);
    TEST_ASSERT(wash_program.segments[0].segment_timeout_ms == wash_program.default_segment_timeout_ms);
    TEST_ASSERT(wash_program.segments[0].exit_timeout_ms == wash_program.default_exit_timeout_ms);
    TEST_ASSERT(wash_program.segments[1].segment_timeout_ms == wash_program.default_segment_timeout_ms);
    TEST_ASSERT(wash_program.segments[1].exit_timeout_ms == wash_program.default_exit_timeout_ms);
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
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
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
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
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
    TEST_ASSERT(strcmp(wash_program.segments[0].segment_name, "roof {A} \"B\"") == 0);
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

static int test_invalid_json_syntax_is_rejected(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_invalid_json_syntax.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
    return 0;
}

static int test_legacy_stages_schema_is_rejected(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse(
        "tests/fixtures/wash_step_control/program_v1_invalid_stages_schema.json",
        &wash_program);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_UNSUPPORTED);
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

static int write_text_file(const char *path, const char *text)
{
    FILE *file_handle;
    size_t text_length;

    if (path == 0 || text == 0) {
        return -1;
    }

    file_handle = fopen(path, "wb");
    if (file_handle == 0) {
        return -1;
    }

    text_length = strlen(text);
    if (fwrite(text, 1u, text_length, file_handle) != text_length) {
        fclose(file_handle);
        return -1;
    }

    fclose(file_handle);
    return 0;
}

static int test_oversized_input_is_rejected_before_parse(void)
{
    const char *fixture_path = "runtime/state/test_program_config_oversized.json";
    char oversized_json[17032];
    size_t cursor;
    operation_result_t result;
    wash_program_t wash_program;

    memset(oversized_json, ' ', sizeof(oversized_json));
    cursor = 0u;
    oversized_json[cursor++] = '{';
    oversized_json[cursor++] = '"';
    oversized_json[cursor++] = 'x';
    oversized_json[cursor++] = '"';
    oversized_json[cursor++] = ':';
    oversized_json[cursor++] = '"';
    while (cursor + 3u < sizeof(oversized_json)) {
        oversized_json[cursor++] = 'a';
    }
    oversized_json[cursor++] = '"';
    oversized_json[cursor++] = '}';
    oversized_json[cursor] = '\0';

    TEST_ASSERT(write_text_file(fixture_path, oversized_json) == 0);
    result = json_program_parser_parse(fixture_path, &wash_program);
    remove(fixture_path);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_IO_FAILED);
    return 0;
}

static int test_excessive_nesting_is_rejected(void)
{
    const char *fixture_path = "runtime/state/test_program_config_deep_nesting.json";
    char nested_json[2048];
    const int nesting_depth = 33;
    size_t cursor;
    int index;
    operation_result_t result;
    wash_program_t wash_program;

    cursor = 0u;
    cursor += (size_t)snprintf(nested_json + cursor,
        sizeof(nested_json) - cursor,
        "{\"program_id\":\"deep\",\"program_name\":\"deep\",\"segments\":");
    for (index = 0; index < nesting_depth; ++index) {
        nested_json[cursor++] = '[';
    }
    cursor += (size_t)snprintf(nested_json + cursor,
        sizeof(nested_json) - cursor,
        "{\"segment_id\":\"s1\",\"segment_name\":\"s1\",\"sequence_no\":1,"
        "\"segment_kind\":\"roof_brush\",\"motion_plan\":{\"direction\":\"forward\","
        "\"target_reference\":\"tail\",\"target_tolerance_mm\":50,"
        "\"requires_position_feedback\":true},\"continuous_controls\":{},"
        "\"conditional_controls\":[],\"completion_condition\":{"
        "\"reference\":\"tail_reached\",\"compare_mode\":\"true\"},"
        "\"exit_actions\":{\"stop_roof_brush\":true}}");
    for (index = 0; index < nesting_depth; ++index) {
        nested_json[cursor++] = ']';
    }
    nested_json[cursor++] = '}';
    nested_json[cursor] = '\0';

    TEST_ASSERT(write_text_file(fixture_path, nested_json) == 0);
    result = json_program_parser_parse(fixture_path, &wash_program);
    remove(fixture_path);
    TEST_ASSERT(!result.ok);
    TEST_ASSERT(result.error_code == ERROR_CODE_PARSE_FAILED);
    return 0;
}

static int test_repository_save_is_rejected_without_serializer(void)
{
    operation_result_t result;
    wash_program_t wash_program;
    simulated_driver_context_t driver_context;
    device_runtime_t system_context;

    test_setup_system_context(&system_context, &driver_context);
    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->program_repository_port.save_program != 0);
    TEST_ASSERT(device_runtime_private_runtime(system_context)->program_repository_port.save_program(
        device_runtime_private_runtime(system_context)->program_repository_port.context,
        &wash_program) != 0);
    test_release_system_context(system_context);
    return 0;
}

int main(void)
{
    if (test_valid_program_fixture_passes() != 0) {
        return 1;
    }
    if (test_default_fields_are_applied_when_optional_members_are_missing() != 0) {
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
    if (test_invalid_json_syntax_is_rejected() != 0) {
        return 1;
    }
    if (test_legacy_stages_schema_is_rejected() != 0) {
        return 1;
    }
    if (test_invalid_sequence_order_is_rejected() != 0) {
        return 1;
    }
    if (test_oversized_input_is_rejected_before_parse() != 0) {
        return 1;
    }
    if (test_excessive_nesting_is_rejected() != 0) {
        return 1;
    }
    if (test_repository_save_is_rejected_without_serializer() != 0) {
        return 1;
    }
    return 0;
}
