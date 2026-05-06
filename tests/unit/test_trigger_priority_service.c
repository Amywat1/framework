#include "tests/test_support.h"

#include "domain/services/trigger_priority_service.h"

int main(void)
{
    wash_trigger_event_t fault_event;
    wash_trigger_event_t stop_event;
    wash_trigger_event_t timeout_event;
    wash_trigger_event_t feedback_event;

    wash_trigger_event_init(&fault_event, TRIGGER_TYPE_FAULT, 0, "fault", "a", 1);
    wash_trigger_event_init(&stop_event, TRIGGER_TYPE_STOP, 0, "stop", "b", 1);
    wash_trigger_event_init(&timeout_event, TRIGGER_TYPE_TIMEOUT, 0, "timeout", "c", 1);
    wash_trigger_event_init(&feedback_event, TRIGGER_TYPE_DEVICE_FEEDBACK, 0, "feedback:pre_soak", "d", 1);

    TEST_ASSERT(trigger_priority_service_compare(&fault_event, &stop_event) > 0);
    TEST_ASSERT(trigger_priority_service_compare(&stop_event, &timeout_event) > 0);
    TEST_ASSERT(trigger_priority_service_compare(&timeout_event, &feedback_event) > 0);
    return 0;
}
