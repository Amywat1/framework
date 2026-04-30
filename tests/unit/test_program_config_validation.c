#include "tests/test_support.h"

#include "domain/model/program_validation.h"

int main(void)
{
    wash_program_t wash_program;
    wash_stage_t wash_stage;
    operation_result_t result;

    wash_program_init(&wash_program, "test", "测试程序");
    wash_stage_init(&wash_stage, "stage-1", "阶段1", 1);
    wash_stage.stage_timeout_ms = 1000;
    wash_program_add_stage(&wash_program, &wash_stage);
    result = program_validation_validate(&wash_program);
    TEST_ASSERT(result.ok);

    wash_program.stages[0].stage_timeout_ms = 0;
    result = program_validation_validate(&wash_program);
    TEST_ASSERT(!result.ok);
    return 0;
}

