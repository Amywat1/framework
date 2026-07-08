/**
 * @file    test_dev_ctx.c
 * @brief   dev_ctx 单元测试
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/ports/outbound/cloud/connection/connection_port.h"
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

static const cloud_connection_ops_t s_fake_connection_ops = {
    .is_connected = fake_is_connected,
};

void setUp(void)
{
    s_fake_cloud_connected = false;
    cloud_connection_register(&s_fake_connection_ops);
    (void)dev_ctx_init();
}

void tearDown(void) {}

/* -------------------------------------------------------------------------
 * TC-1：init 后各字段为安全初始值
 * ------------------------------------------------------------------------- */
static void test_init_defaults(void)
{
    device_context_t ctx = dev_ctx_snapshot();

    TEST_ASSERT_EQUAL_INT(DEV_STATE_INIT,     ctx.device_state);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, ctx.wash_mode);
    TEST_ASSERT_FALSE(ctx.cloud_connected);
}

/* -------------------------------------------------------------------------
 * TC-2：set_device_state 只改 device_state，其余字段不变
 * ------------------------------------------------------------------------- */
static void test_set_device_state_isolated(void)
{
    s_fake_cloud_connected = true;
    dev_ctx_set_wash_mode(WASH_MODE_QUICK);
    dev_ctx_set_device_state(DEV_STATE_RUNNING);

    device_context_t ctx = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(DEV_STATE_RUNNING, ctx.device_state);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK,   ctx.wash_mode);
    TEST_ASSERT_TRUE(ctx.cloud_connected);
}

/* -------------------------------------------------------------------------
 * TC-3：set_wash_mode 更新 wash_mode
 * ------------------------------------------------------------------------- */
static void test_set_wash_mode(void)
{
    dev_ctx_set_wash_mode(WASH_MODE_QUICK);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_QUICK,    dev_ctx_snapshot().wash_mode);

    dev_ctx_set_wash_mode(WASH_MODE_STANDARD);
    TEST_ASSERT_EQUAL_INT(WASH_MODE_STANDARD, dev_ctx_snapshot().wash_mode);
}

/* -------------------------------------------------------------------------
 * TC-4：snapshot 返回值拷贝，修改快照不影响内部状态
 * ------------------------------------------------------------------------- */
static void test_snapshot_is_copy(void)
{
    dev_ctx_set_device_state(DEV_STATE_IDLE);

    device_context_t snap1 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(DEV_STATE_IDLE, snap1.device_state);

    snap1.device_state = DEV_STATE_FAULT;

    device_context_t snap2 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(DEV_STATE_IDLE, snap2.device_state);
}

/* -------------------------------------------------------------------------
 * TC-5：cloud_connected 读穿 connection port
 * ------------------------------------------------------------------------- */
static void test_cloud_connected_read_through(void)
{
    s_fake_cloud_connected = true;
    TEST_ASSERT_TRUE(dev_ctx_snapshot().cloud_connected);

    s_fake_cloud_connected = false;
    TEST_ASSERT_FALSE(dev_ctx_snapshot().cloud_connected);
}

/* -------------------------------------------------------------------------
 * TC-6：并发读写一致性
 * ------------------------------------------------------------------------- */
#define CONCURRENT_ITER  50000

static atomic_int s_stop_flag      = 0;
static volatile int s_reader_error = 0;

static const dev_state_t k_states[2] = { DEV_STATE_IDLE, DEV_STATE_RUNNING };
static const wash_mode_t k_modes[2]  = { WASH_MODE_STANDARD, WASH_MODE_QUICK };

static void *writer_fn(void *arg)
{
    (void)arg;
    int idx = 0;

    for (int i = 0; i < CONCURRENT_ITER; i++)
    {
        dev_ctx_set_device_state(k_states[idx]);
        dev_ctx_set_wash_mode(k_modes[idx]);
        s_fake_cloud_connected = (idx == 0);
        idx ^= 1;
    }
    atomic_store(&s_stop_flag, 1);
    return NULL;
}

static void *reader_fn(void *arg)
{
    (void)arg;

    while (!atomic_load(&s_stop_flag))
    {
        device_context_t ctx = dev_ctx_snapshot();

        if ((ctx.device_state != k_states[0] && ctx.device_state != k_states[1]) ||
            (ctx.wash_mode    != k_modes[0]  && ctx.wash_mode    != k_modes[1]))
        {
            s_reader_error = 1;
            return NULL;
        }
    }
    return NULL;
}

static void test_concurrent_read_write(void)
{
    atomic_store(&s_stop_flag, 0);
    s_reader_error = 0;

    pthread_t writer;
    pthread_t reader;

    pthread_create(&writer, NULL, writer_fn, NULL);
    pthread_create(&reader, NULL, reader_fn, NULL);
    pthread_join(writer, NULL);
    pthread_join(reader, NULL);

    TEST_ASSERT_FALSE_MESSAGE(s_reader_error, "并发读写检测到非法状态");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_set_device_state_isolated);
    RUN_TEST(test_set_wash_mode);
    RUN_TEST(test_snapshot_is_copy);
    RUN_TEST(test_cloud_connected_read_through);
    RUN_TEST(test_concurrent_read_write);
    return UNITY_END();
}
