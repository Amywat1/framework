#include "common/event_types.h"
#include "common/time_util.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/event_bus/event_bus_config.h"
#include "wdf_test_spec.h"

static int s_fatal_called;
static int s_fatal_errno;
static int s_fatal_reason;

static void fatal_handler(event_bus_fatal_reason_t reason, int detail_code)
{
    s_fatal_called = 1;
    s_fatal_reason = (int)reason;
    s_fatal_errno  = detail_code;
}

void setUp(void)
{
    s_fatal_called = 0;
    s_fatal_errno  = 0;
    s_fatal_reason = 0;
}

void tearDown(void)
{
    event_bus_set_fatal_cb(NULL);
}

static void test_required_publish_failure_enters_fatal_path(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    event_bus_set_fatal_cb(fatal_handler);

    for (unsigned i = 0U; i < EVENT_BUS_QUEUE_SIZE; ++i) {
        TEST_ASSERT_EQUAL_INT(SW_OK, event_publish(EVT_CMD_ORDER, i));
    }

    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, event_publish_required(EVT_CMD_ORDER, 99U));
    TEST_ASSERT_EQUAL_INT(1, s_fatal_called);
    TEST_ASSERT_EQUAL_INT(EVENT_BUS_FATAL_REQUIRED_PUBLISH, s_fatal_reason);
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, s_fatal_errno);
}

int main(void)
{
    time_util_init();
    UNITY_BEGIN();
    WDF_RUN_TEST(test_required_publish_failure_enters_fatal_path, "EBUS-02", "验证必达事件失败进入安全故障路径");
    return UNITY_END();
}
