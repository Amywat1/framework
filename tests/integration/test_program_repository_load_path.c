#include "tests/test_support.h"

static int assert_equivalent_programs(const wash_program_t *left, const wash_program_t *right)
{
    int index;

    TEST_ASSERT(left != 0);
    TEST_ASSERT(right != 0);
    TEST_ASSERT(strcmp(left->program_id, right->program_id) == 0);
    TEST_ASSERT(strcmp(left->program_name, right->program_name) == 0);
    TEST_ASSERT(left->revision == right->revision);
    TEST_ASSERT(left->enabled == right->enabled);
    TEST_ASSERT(left->default_segment_timeout_ms == right->default_segment_timeout_ms);
    TEST_ASSERT(left->default_exit_timeout_ms == right->default_exit_timeout_ms);
    TEST_ASSERT(left->segment_count == right->segment_count);

    for (index = 0; index < left->segment_count; ++index) {
        TEST_ASSERT(strcmp(left->segments[index].segment_id, right->segments[index].segment_id) == 0);
        TEST_ASSERT(strcmp(left->segments[index].segment_name, right->segments[index].segment_name) == 0);
        TEST_ASSERT(left->segments[index].sequence_no == right->segments[index].sequence_no);
        TEST_ASSERT(left->segments[index].segment_kind == right->segments[index].segment_kind);
        TEST_ASSERT(left->segments[index].segment_timeout_ms == right->segments[index].segment_timeout_ms);
        TEST_ASSERT(left->segments[index].exit_timeout_ms == right->segments[index].exit_timeout_ms);
        TEST_ASSERT(left->segments[index].conditional_control_count
            == right->segments[index].conditional_control_count);
        TEST_ASSERT(left->segments[index].continuous_controls.roof_brush_follow
            == right->segments[index].continuous_controls.roof_brush_follow);
        TEST_ASSERT(left->segments[index].continuous_controls.side_brush_enabled
            == right->segments[index].continuous_controls.side_brush_enabled);
        TEST_ASSERT(left->segments[index].continuous_controls.ro_water_enabled
            == right->segments[index].continuous_controls.ro_water_enabled);
        TEST_ASSERT(left->segments[index].continuous_controls.dryer_enabled
            == right->segments[index].continuous_controls.dryer_enabled);
    }

    return 0;
}

static int verify_direct_parse_repository_load_and_test_helper_match(void)
{
    simulated_driver_context_t repository_driver_context;
    simulated_driver_context_t helper_driver_context;
    device_runtime_t repository_context;
    device_runtime_t helper_context;
    wash_program_t direct_program;
    wash_program_t repository_program;
    wash_program_t helper_program;
    wash_program_t cached_program;
    operation_result_t result;

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &direct_program);
    TEST_ASSERT(result.ok);

    test_setup_system_context(&repository_context, &repository_driver_context);
    TEST_ASSERT(test_load_program_via_repository(repository_context, "wash_step_control_v1", &repository_program) == 0);
    TEST_ASSERT(assert_equivalent_programs(&direct_program, &repository_program) == 0);
    test_release_system_context(repository_context);

    test_setup_system_context(&helper_context, &helper_driver_context);
    result = test_load_runtime_program_from_fixture(
        helper_context,
        "tests/fixtures/wash_step_control/program_v1_valid.json",
        &helper_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(test_load_program_via_repository(helper_context, "wash_step_control_v1", &cached_program) == 0);
    TEST_ASSERT(assert_equivalent_programs(&direct_program, &helper_program) == 0);
    TEST_ASSERT(assert_equivalent_programs(&helper_program, &cached_program) == 0);
    test_release_system_context(helper_context);
    return 0;
}

int main(void)
{
    if (verify_direct_parse_repository_load_and_test_helper_match() != 0) {
        return 1;
    }
    return 0;
}
