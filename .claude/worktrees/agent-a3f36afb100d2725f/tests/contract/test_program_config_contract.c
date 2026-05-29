#include "tests/test_support.h"

#include "adapters/config/json_program_parser.h"

int main(void)
{
    wash_program_t wash_program;
    operation_result_t result = json_program_parser_parse("./configs/programs/standard_wash.json", &wash_program);

    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(wash_program.program_id, "standard_wash") == 0);
    TEST_ASSERT(wash_program.segment_count == 4);
    TEST_ASSERT(strcmp(wash_program.segments[0].segment_id, "standard_roof") == 0);
    TEST_ASSERT(wash_program.segments[0].segment_timeout_ms == wash_program.default_segment_timeout_ms);
    TEST_ASSERT(wash_program.segments[3].continuous_controls.dryer_enabled);
    return 0;
}
