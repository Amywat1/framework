/**
 * @file    test_dev_ctx.c
 * @brief   dev_ctx 组合快照单元测试
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/application/operational_projection.h"
#include "framework/application/safety_projection.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/domain/safety/alarm_registry/alarm_registry.h"
#include "framework/domain/telemetry/snapshot/wash_snapshot_internal.h"
#include "framework/domain/telemetry/snapshot/operational_snapshot_internal.h"
#include "framework/ports/outbound/cloud/link/cloud_link_port.h"
#include "framework/domain/device_control/model/device_state.h"
#include "framework/domain/wash/model/wash_types.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/common/time_util.h"
#include "unity.h"

#include <stdbool.h>
#include <pthread.h>

static bool s_fake_cloud_connected = false;

static bool fake_is_connected(void)
{
    return s_fake_cloud_connected;
}

static const cloud_link_ops_t s_fake_link_ops = {
    .is_online = fake_is_connected,
};

void setUp(void)
{
    s_fake_cloud_connected = false;
    cloud_link_register(&s_fake_link_ops);
    (void)time_util_init();
    (void)event_bus_init();
    (void)alarm_registry_init();
    (void)operational_mode_init();
    (void)dev_ctx_init();
    (void)operational_projection_init();
    (void)safety_projection_init();
}

void tearDown(void)
{
    (void)event_bus_shutdown();
}

static void test_init_defaults(void)
{
    device_context_t ctx = dev_ctx_snapshot();

    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, ctx.operational_mode);
    TEST_ASSERT_TRUE(ctx.service_enabled);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, ctx.wash_mode);
    TEST_ASSERT_FALSE(ctx.cloud_connected);
}

static void test_projection_reflects_mode_change(void)
{
    operational_snapshot_t op_snap;

    s_fake_cloud_connected = true;
    wash_snapshot_on_session_started(WASH_MODE_QUICK);

    op_snap.mode            = OP_MODE_WASHING;
    op_snap.service_enabled = true;
    op_snap.estop_active    = false;
    operational_snapshot_update(&op_snap);

    device_context_t ctx = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, ctx.operational_mode);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, ctx.wash_mode);
    TEST_ASSERT_TRUE(ctx.cloud_connected);
}

static void test_wash_snapshot_mode(void)
{
    wash_snapshot_on_session_started(WASH_MODE_QUICK);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, dev_ctx_snapshot().wash_mode);

    wash_snapshot_on_session_started(WASH_MODE_STANDARD);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, dev_ctx_snapshot().wash_mode);
}

static void test_snapshot_is_copy(void)
{
    device_context_t snap1 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, snap1.operational_mode);

    snap1.operational_mode = OP_MODE_EXCEPTION;

    device_context_t snap2 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, snap2.operational_mode);
}

typedef struct
{
    int dummy;
} reader_arg_t;

static void *reader_fn(void *arg)
{
    reader_arg_t *ra = (reader_arg_t *)arg;

    for (int i = 0; i < 1000; i++)
    {
        (void)dev_ctx_snapshot();
        (void)ra;
    }
    return NULL;
}

static void test_concurrent_readers(void)
{
    pthread_t t0;
    pthread_t t1;
    reader_arg_t a0 = { .dummy = 0 };
    reader_arg_t a1 = { .dummy = 1 };

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t0, NULL, reader_fn, &a0));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t1, NULL, reader_fn, &a1));
    (void)pthread_join(t0, NULL);
    (void)pthread_join(t1, NULL);

    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, dev_ctx_snapshot().operational_mode);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_projection_reflects_mode_change);
    RUN_TEST(test_wash_snapshot_mode);
    RUN_TEST(test_snapshot_is_copy);
    RUN_TEST(test_concurrent_readers);
    return UNITY_END();
}
