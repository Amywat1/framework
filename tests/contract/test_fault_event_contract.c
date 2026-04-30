#include "tests/test_support.h"

#include "domain/model/fault_event.h"

int main(void)
{
    fault_event_t fault_event;

    fault_event_init(&fault_event, "fault-001", "急停触发", FAULT_CLASS_SAFETY, FAULT_SEVERITY_SAFETY);
    TEST_ASSERT(strcmp(fault_event.event_id, "fault-001") == 0);
    TEST_ASSERT(fault_event.operator_ack_required);
    return 0;
}

