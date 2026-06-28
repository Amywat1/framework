/**
 * @file    test_dev_ctx.c
 * @brief   dev_ctx 单元测试
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "service/dev_ctx/dev_ctx.h"
#include "domain/model/device_state.h"
#include "domain/model/wash_types.h"
#include "unity.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

void setUp(void)    { (void)dev_ctx_init(); }
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
    dev_ctx_set_wash_mode(WASH_MODE_QUICK);
    dev_ctx_set_cloud_status(true);
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

    snap1.device_state = DEV_STATE_FAULT;  /* 改快照 */

    device_context_t snap2 = dev_ctx_snapshot();
    TEST_ASSERT_EQUAL_INT(DEV_STATE_IDLE, snap2.device_state);  /* 内部未变 */
}

/* -------------------------------------------------------------------------
 * TC-5：cloud_status 切换
 * ------------------------------------------------------------------------- */
static void test_cloud_status_toggle(void)
{
    dev_ctx_set_cloud_status(true);
    TEST_ASSERT_TRUE(dev_ctx_snapshot().cloud_connected);

    dev_ctx_set_cloud_status(false);
    TEST_ASSERT_FALSE(dev_ctx_snapshot().cloud_connected);
}

/* -------------------------------------------------------------------------
 * TC-6：并发读写一致性
 * 线程内不使用 Unity 断言（longjmp 跨线程不安全），改为标志位+主线程检查
 * ------------------------------------------------------------------------- */
#define CONCURRENT_ITER  50000

static atomic_int s_stop_flag   = 0;
static volatile int s_reader_error = 0;  /* 线程检测到非法状态时置 1 */

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
        dev_ctx_set_cloud_status((idx == 0));
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

    pthread_t writer, reader;
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
    RUN_TEST(test_cloud_status_toggle);
    RUN_TEST(test_concurrent_read_write);
    return UNITY_END();
}
