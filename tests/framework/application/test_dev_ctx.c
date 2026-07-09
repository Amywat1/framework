/**
 * @file    test_dev_ctx.c
 * @brief   dev_ctx 单元测试
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/ports/outbound/cloud/link/cloud_link_port.h"
#include "framework/domain/device_control/model/device_state.h"
#include "framework/domain/wash/model/wash_types.h"
#include "unity.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

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
    (void)dev_ctx_init();
}

void tearDown(void) {}

static void test_init_defaults(void)
{
    device_context_t ctx = dev_ctx_snapshot();

    TEST_ASSERT_EQUAL_INT(OP_MODE_INIT, ctx.operational_mode);
    TEST_ASSERT_TRUE(ctx.service_enabled);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, ctx.wash_mode);
    TEST_ASSERT_FALSE(ctx.cloud_connected);
}

static void test_set_operational_mode_isolated(void)
{
    s_fake_cloud_connected = true;
    dev_ctx_set_wash_mode(WASH_MODE_QUICK);
    dev_ctx_set_operational_mode(OP_MODE_WASHING);

    device_context_t ctx = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(OP_MODE_WASHING, ctx.operational_mode);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, ctx.wash_mode);
    TEST_ASSERT_TRUE(ctx.cloud_connected);
}

static void test_set_wash_mode(void)
{
    dev_ctx_set_wash_mode(WASH_MODE_QUICK);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK, dev_ctx_snapshot().wash_mode);

    dev_ctx_set_wash_mode(WASH_MODE_STANDARD);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, dev_ctx_snapshot().wash_mode);
}

static void test_snapshot_is_copy(void)
{
    dev_ctx_set_operational_mode(OP_MODE_IDLE);

    device_context_t snap1 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, snap1.operational_mode);

    snap1.operational_mode = OP_MODE_EXCEPTION;

    device_context_t snap2 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(OP_MODE_IDLE, snap2.operational_mode);
}

typedef struct
{
    int idx;
} writer_arg_t;

static const operational_mode_t k_modes[2] = { OP_MODE_IDLE, OP_MODE_WASHING };

static void *writer_fn(void *arg)
{
    writer_arg_t *wa = (writer_arg_t *)arg;

    for (int i = 0; i < 1000; i++)
    {
        dev_ctx_set_operational_mode(k_modes[wa->idx]);
    }
    return NULL;
}

static void test_concurrent_writers(void)
{
    pthread_t t0;
    pthread_t t1;
    writer_arg_t a0 = { .idx = 0 };
    writer_arg_t a1 = { .idx = 1 };

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t0, NULL, writer_fn, &a0));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&t1, NULL, writer_fn, &a1));
    (void)pthread_join(t0, NULL);
    (void)pthread_join(t1, NULL);

    device_context_t ctx = dev_ctx_snapshot();
    TEST_ASSERT_TRUE((ctx.operational_mode == OP_MODE_IDLE) ||
                     (ctx.operational_mode == OP_MODE_WASHING));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_set_operational_mode_isolated);
    RUN_TEST(test_set_wash_mode);
    RUN_TEST(test_snapshot_is_copy);
    RUN_TEST(test_concurrent_writers);
    return UNITY_END();
}
