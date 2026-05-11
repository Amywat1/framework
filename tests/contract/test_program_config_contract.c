#include "tests/test_support.h"

#include "adapters/config/json_program_parser.h"

int main(void)
{
    wash_program_t wash_program;
    operation_result_t result = json_program_parser_parse("./configs/programs/standard_wash.json", &wash_program);

    TEST_ASSERT(result.ok);
    TEST_ASSERT(strcmp(wash_program.program_id, "standard_wash") == 0);
    TEST_ASSERT(wash_program.stage_count >= 1);
    return 0;
}


