#include "adapters/config/json_program_parser.h"
#include "tests/test_support.h"

int main(void)
{
    operation_result_t result;
    wash_program_t wash_program;

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(wash_program.program_id, "wash_step_control_v1") == 0);
    TEST_ASSERT(wash_program.segment_count == 4);
    TEST_ASSERT(strcmp(wash_program.segments[0].segment_id, "roof_segment") == 0);
    TEST_ASSERT(wash_program.segments[0].continuous_controls.roof_brush_follow);
    TEST_ASSERT(wash_program.segments[0].conditional_control_count == 1);
    TEST_ASSERT(wash_program.segments[0].exit_actions.roof_brush_home);
    TEST_ASSERT(wash_program.segments[2].continuous_controls.ro_water_enabled);
    TEST_ASSERT(wash_program.segments[2].exception_policy.on_segment_timeout == EXCEPTION_STRATEGY_SAFE_FINISH);
    TEST_ASSERT(wash_program.segments[3].continuous_controls.dryer_enabled);

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid_json_edge_cases.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(wash_program.program_id, "wash_json_edge") == 0);
    TEST_ASSERT(strcmp(wash_program.program_name, "edge {cfg} \"ok\"") == 0);
    TEST_ASSERT(strcmp(wash_program.segments[0].segment_name, "roof {A} \"B\"") == 0);

    result = json_program_parser_parse("configs/programs/standard_wash.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(wash_program.program_id, "standard_wash") == 0);
    TEST_ASSERT(wash_program.segment_count == 4);
    TEST_ASSERT(wash_program.segments[0].segment_timeout_ms == wash_program.default_segment_timeout_ms);
    TEST_ASSERT(wash_program.segments[0].exit_timeout_ms == wash_program.default_exit_timeout_ms);
    TEST_ASSERT(wash_program.segments[0].exception_policy.on_position_lost == EXCEPTION_STRATEGY_ABORT_SESSION);
    TEST_ASSERT(wash_program.segments[1].segment_timeout_ms == wash_program.default_segment_timeout_ms);
    TEST_ASSERT(wash_program.segments[1].exit_timeout_ms == wash_program.default_exit_timeout_ms);
    return 0;
}
