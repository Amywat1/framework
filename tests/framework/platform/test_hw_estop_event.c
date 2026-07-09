/**
 * @file    test_hw_estop_event.c
 * @brief   EVT_HW_ESTOP 硬件快速通道与 OperationalMode 集成测试
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/application/op_mode_bridge.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/event_types.h"
#include "framework/common/time_util.h"
#include "unity.h"

#include <pthread.h>
#include <unistd.h>

static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static pthread_t start_dispatch(void)
{
    pthread_t tid;

    (void)pthread_create(&tid, NULL, dispatch_fn, NULL);
    usleep(10000);
    return tid;
}

static void stop_dispatch(pthread_t tid)
{
    (void)event_bus_shutdown();
    (void)pthread_join(tid, NULL);
}

void setUp(void)
{
    (void)time_util_init();
    (void)event_bus_init();
    (void)operational_mode_init();
    (void)op_mode_bridge_init();
}

void tearDown(void)
{
}

static void test_hw_estop_on_enters_exception(void)
{
    pthread_t tid = start_dispatch();

    (void)event_publish(EVT_HW_ESTOP_ON, 0U);
    usleep(20000);

    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());
    TEST_ASSERT_TRUE(op_mode_is_estop_active());

    stop_dispatch(tid);
}

static void test_hw_estop_off_clears_flag_keeps_exception(void)
{
    pthread_t tid = start_dispatch();

    (void)event_publish(EVT_HW_ESTOP_ON, 0U);
    usleep(20000);
    (void)event_publish(EVT_HW_ESTOP_OFF, 0U);
    usleep(20000);

    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());
    TEST_ASSERT_FALSE(op_mode_is_estop_active());

    stop_dispatch(tid);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hw_estop_on_enters_exception);
    RUN_TEST(test_hw_estop_off_clears_flag_keeps_exception);
    return UNITY_END();
}
