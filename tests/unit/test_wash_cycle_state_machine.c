#include "tests/test_support.h"

#include "domain/model/wash_cycle.h"

int main(void)
{
    wash_cycle_t wash_cycle;

    wash_cycle_init(&wash_cycle, "cycle-001", "standard_wash");
    TEST_ASSERT(wash_cycle.cycle_state == CYCLE_STATE_IDLE);
    wash_cycle_set_state(&wash_cycle, CYCLE_STATE_PRECHECK);
    TEST_ASSERT(wash_cycle.cycle_state == CYCLE_STATE_PRECHECK);
    wash_cycle_finish(&wash_cycle, RESULT_CODE_SUCCESS);
    TEST_ASSERT(wash_cycle.cycle_state == CYCLE_STATE_COMPLETED);
    TEST_ASSERT(wash_cycle.result_code == RESULT_CODE_SUCCESS);
    return 0;
}

