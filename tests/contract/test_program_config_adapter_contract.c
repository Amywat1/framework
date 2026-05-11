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
    TEST_ASSERT(wash_program.segments[3].continuous_controls.dryer_enabled);

    result = json_program_parser_parse("tests/fixtures/wash_step_control/program_v1_valid_json_edge_cases.json", &wash_program);
    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(wash_program.program_id, "wash_json_edge") == 0);
    TEST_ASSERT(strcmp(wash_program.program_name, "edge {cfg} \"ok\"") == 0);
    TEST_ASSERT(strcmp(wash_program.segments[0].segment_name, "roof {A} \"B\"") == 0);
    return 0;
}

