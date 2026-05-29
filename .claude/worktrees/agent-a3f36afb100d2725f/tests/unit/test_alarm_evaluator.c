#include "tests/test_support.h"

#include "application/services/alarm_evaluator.h"

static void build_normal_snapshot(sensor_snapshot_t *sensor_snapshot)
{
    memset(sensor_snapshot, 0, sizeof(*sensor_snapshot));
    sensor_snapshot->safety_ok = true;
    sensor_snapshot->resource_ok = true;
    sensor_snapshot->position_ok = true;
    sensor_snapshot->vehicle_present = true;
    sensor_snapshot->vehicle_allowed = true;
}

static int verify_normal_input_does_not_trigger(void)
{
    alarm_evaluator_t alarm_evaluator;
    sensor_snapshot_t sensor_snapshot;
    const char *fault_code;
    unsigned long fault_occurred_at_ms;

    alarm_evaluator_init(&alarm_evaluator);
    build_normal_snapshot(&sensor_snapshot);

    fault_code = 0;
    fault_occurred_at_ms = 123ul;
    TEST_ASSERT(!alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 100ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code == 0);
    TEST_ASSERT(fault_occurred_at_ms == 0ul);
    return 0;
}

static int verify_alarm_only_triggers_once_while_active(void)
{
    alarm_evaluator_t alarm_evaluator;
    sensor_snapshot_t sensor_snapshot;
    const char *fault_code;
    unsigned long fault_occurred_at_ms;

    alarm_evaluator_init(&alarm_evaluator);
    build_normal_snapshot(&sensor_snapshot);

    sensor_snapshot.estop_active = true;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 100ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code != 0);
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);
    alarm_evaluator_mark_delivered(&alarm_evaluator);

    fault_code = 0;
    fault_occurred_at_ms = 123ul;
    TEST_ASSERT(!alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 200ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code == 0);
    TEST_ASSERT(fault_occurred_at_ms == 0ul);
    return 0;
}

static int verify_alarm_retries_until_delivery(void)
{
    alarm_evaluator_t alarm_evaluator;
    sensor_snapshot_t sensor_snapshot;
    const char *fault_code;
    unsigned long fault_occurred_at_ms;

    alarm_evaluator_init(&alarm_evaluator);
    build_normal_snapshot(&sensor_snapshot);

    sensor_snapshot.estop_active = true;
    fault_code = 0;
    fault_occurred_at_ms = 0ul;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 100ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code != 0);
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);

    fault_code = 0;
    fault_occurred_at_ms = 0ul;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 200ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code != 0);
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);

    alarm_evaluator_mark_delivered(&alarm_evaluator);
    fault_code = 0;
    fault_occurred_at_ms = 123ul;
    TEST_ASSERT(!alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 300ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code == 0);
    TEST_ASSERT(fault_occurred_at_ms == 0ul);
    return 0;
}

static int verify_pending_alarm_survives_recovery_until_delivery(void)
{
    alarm_evaluator_t alarm_evaluator;
    sensor_snapshot_t sensor_snapshot;
    const char *fault_code;
    unsigned long fault_occurred_at_ms;

    alarm_evaluator_init(&alarm_evaluator);
    build_normal_snapshot(&sensor_snapshot);

    sensor_snapshot.estop_active = true;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 100ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);

    fault_code = 0;
    sensor_snapshot.estop_active = false;
    fault_occurred_at_ms = 0ul;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 200ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code != 0);
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);

    alarm_evaluator_mark_delivered(&alarm_evaluator);
    fault_code = 0;
    fault_occurred_at_ms = 123ul;
    TEST_ASSERT(!alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 300ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code == 0);
    TEST_ASSERT(fault_occurred_at_ms == 0ul);
    return 0;
}

static int verify_alarm_recovery_rearms_after_delivery(void)
{
    alarm_evaluator_t alarm_evaluator;
    sensor_snapshot_t sensor_snapshot;
    const char *fault_code;
    unsigned long fault_occurred_at_ms;

    alarm_evaluator_init(&alarm_evaluator);
    build_normal_snapshot(&sensor_snapshot);

    sensor_snapshot.estop_active = true;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 100ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);
    alarm_evaluator_mark_delivered(&alarm_evaluator);

    fault_code = 0;
    sensor_snapshot.estop_active = false;
    fault_occurred_at_ms = 123ul;
    TEST_ASSERT(!alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 200ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code == 0);
    TEST_ASSERT(fault_occurred_at_ms == 0ul);

    sensor_snapshot.estop_active = true;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 300ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code != 0);
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 300ul);
    return 0;
}

static int verify_rule_priority_prefers_estop_first(void)
{
    alarm_evaluator_t alarm_evaluator;
    sensor_snapshot_t sensor_snapshot;
    const char *fault_code;
    unsigned long fault_occurred_at_ms;

    alarm_evaluator_init(&alarm_evaluator);
    build_normal_snapshot(&sensor_snapshot);

    sensor_snapshot.estop_active = true;
    sensor_snapshot.resource_ok = false;

    fault_code = 0;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 100ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(fault_code != 0);
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);
    return 0;
}

static int verify_pending_alarm_upgrades_to_higher_priority_fault(void)
{
    alarm_evaluator_t alarm_evaluator;
    sensor_snapshot_t sensor_snapshot;
    const char *fault_code;
    unsigned long fault_occurred_at_ms;

    alarm_evaluator_init(&alarm_evaluator);
    build_normal_snapshot(&sensor_snapshot);

    sensor_snapshot.resource_ok = false;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 100ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(strcmp(fault_code, "resource_not_ok") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 100ul);

    sensor_snapshot.estop_active = true;
    TEST_ASSERT(alarm_evaluator_evaluate(&alarm_evaluator, &sensor_snapshot, 200ul, &fault_code, &fault_occurred_at_ms));
    TEST_ASSERT(strcmp(fault_code, "estop_active") == 0);
    TEST_ASSERT(fault_occurred_at_ms == 200ul);
    return 0;
}

int main(void)
{
    if (verify_normal_input_does_not_trigger() != 0) {
        return 1;
    }
    if (verify_alarm_only_triggers_once_while_active() != 0) {
        return 1;
    }
    if (verify_alarm_retries_until_delivery() != 0) {
        return 1;
    }
    if (verify_pending_alarm_survives_recovery_until_delivery() != 0) {
        return 1;
    }
    if (verify_alarm_recovery_rearms_after_delivery() != 0) {
        return 1;
    }
    if (verify_rule_priority_prefers_estop_first() != 0) {
        return 1;
    }
    if (verify_pending_alarm_upgrades_to_higher_priority_fault() != 0) {
        return 1;
    }
    return 0;
}
