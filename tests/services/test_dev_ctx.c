/**
 * @file    test_dev_ctx.c
 * @brief   dev_ctx 组合快照单元测试
 */

#include "common/sw_error.h"
#include "domain/telemetry/snapshot/operational_snapshot_internal.h"
#include "domain/telemetry/snapshot/safety_snapshot_internal.h"
#include "domain/telemetry/snapshot/wash_snapshot_internal.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
#include "services/dev_ctx/dev_ctx.h"
#include "unity.h"

#include <pthread.h>
#include <string.h>

static bool s_cloud_online;

static bool stub_is_online(void)
{
    return s_cloud_online;
}

static const cloud_link_ops_t s_link_ops = {
    .is_online = stub_is_online,
};

static void seed_snapshots(void)
{
    operational_snapshot_t op = {
        .mode            = OP_MODE_IDLE,
        .service_enabled = true,
        .estop_active    = false,
    };
    safety_snapshot_t safety;

    memset(&safety, 0, sizeof(safety));
    safety.posture            = SAFETY_POSTURE_NOMINAL;
    safety.blocking_active    = false;
    safety.top_alarm_code     = 0U;
    safety.active_alarm_count = 0U;

    operational_snapshot_update(&op);
    safety_snapshot_update(&safety);
    wash_snapshot_on_session_started(WASH_MODE_STANDARD);
}

void setUp(void)
{
    s_cloud_online = false;
    cloud_link_register(&s_link_ops);
    seed_snapshots();
    TEST_ASSERT_EQUAL_INT(SW_OK, dev_ctx_init());
}

void tearDown(void)
{
}

static void test_snapshot_combines_domain_read_models(void)
{
    device_context_t ctx;

    ctx = dev_ctx_snapshot();

    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, ctx.operational_mode);
    TEST_ASSERT_TRUE(ctx.service_enabled);
    TEST_ASSERT_FALSE(ctx.estop_active);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, ctx.wash_mode);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_NOMINAL, ctx.safety_posture);
    TEST_ASSERT_FALSE(ctx.blocking_active);
    TEST_ASSERT_FALSE(ctx.cloud_connected);
}

static void test_snapshot_reflects_cloud_and_alarm_state(void)
{
    safety_snapshot_t safety;
    device_context_t  ctx;

    memset(&safety, 0, sizeof(safety));
    safety.posture             = SAFETY_POSTURE_LOCKOUT;
    safety.blocking_active     = true;
    safety.top_alarm_code      = 201101U;
    safety.active_alarm_count  = 1U;
    safety.active_list[0].code = 201101U;
    safety_snapshot_update(&safety);
    s_cloud_online = true;

    ctx = dev_ctx_snapshot();

    TEST_ASSERT_TRUE(ctx.cloud_connected);
    TEST_ASSERT_EQUAL_INT(SAFETY_POSTURE_LOCKOUT, ctx.safety_posture);
    TEST_ASSERT_TRUE(ctx.blocking_active);
    TEST_ASSERT_EQUAL_UINT(201101U, ctx.top_alarm_code);
    TEST_ASSERT_EQUAL_UINT(1U, ctx.active_alarm_count);
    TEST_ASSERT_EQUAL_UINT(201101U, ctx.active_list[0].code);
}

static void test_operational_mode_accessor_uses_cached_snapshot(void)
{
    operational_snapshot_t op = {
        .mode            = OP_MODE_WASHING,
        .service_enabled = true,
        .estop_active    = false,
    };

    operational_snapshot_update(&op);
    wash_snapshot_on_session_started(WASH_MODE_QUICK);

    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, dev_ctx_get_operational_mode());
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, dev_ctx_snapshot().wash_mode);
}

static void test_snapshot_is_value_copy(void)
{
    device_context_t snap1;
    device_context_t snap2;

    snap1                    = dev_ctx_snapshot();
    snap1.operational_mode   = OP_MODE_EXCEPTION;
    snap1.active_alarm_count = 3U;
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, snap1.operational_mode);
    TEST_ASSERT_EQUAL_UINT(3U, snap1.active_alarm_count);

    snap2 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, snap2.operational_mode);
    TEST_ASSERT_EQUAL_UINT(0U, snap2.active_alarm_count);
}

static void *reader_fn(void *arg)
{
    (void)arg;

    for (int i = 0; i < 1000; i++) {
        (void)dev_ctx_snapshot();
        (void)dev_ctx_get_operational_mode();
    }
    return NULL;
}

static void test_concurrent_readers(void)
{
    pthread_t t0;
    pthread_t t1;

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t0, NULL, reader_fn, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t1, NULL, reader_fn, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(t0, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(t1, NULL));
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, dev_ctx_get_operational_mode());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_snapshot_combines_domain_read_models);
    RUN_TEST(test_snapshot_reflects_cloud_and_alarm_state);
    RUN_TEST(test_operational_mode_accessor_uses_cached_snapshot);
    RUN_TEST(test_snapshot_is_value_copy);
    RUN_TEST(test_concurrent_readers);

    return UNITY_END();
}
