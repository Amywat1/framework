/**
 * @file    test_dev_ctx.c
 * @brief   dev_ctx 单元测试
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "service/dev_ctx/dev_ctx.h"
#include "domain/model/device_state.h"
#include "domain/model/wash_types.h"
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

/* -------------------------------------------------------------------------
 * TC-1：init 后各字段为安全初始值
 * ------------------------------------------------------------------------- */
static void test_init_defaults(void)
{
    printf("TC-1: init defaults\n");

    (void)dev_ctx_init();
    device_context_t ctx = dev_ctx_snapshot();

    assert(ctx.device_state  == DEV_STATE_INIT);
    assert(ctx.wash_step     == WASH_STEP_IDLE);
    assert(ctx.wash_mode     == WASH_MODE_STANDARD);
    assert(ctx.cloud_connected == false);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-2：set_device_state 只改 device_state，其余字段不变
 * ------------------------------------------------------------------------- */
static void test_set_device_state_isolated(void)
{
    printf("TC-2: set_device_state isolated\n");

    (void)dev_ctx_init();

    dev_ctx_set_wash_progress(WASH_STEP_PREWASH, WASH_MODE_QUICK);
    dev_ctx_set_cloud_status(true);

    dev_ctx_set_device_state(DEV_STATE_RUNNING);
    device_context_t ctx = dev_ctx_snapshot();

    assert(ctx.device_state    == DEV_STATE_RUNNING);
    assert(ctx.wash_step       == WASH_STEP_PREWASH);
    assert(ctx.wash_mode       == WASH_MODE_QUICK);
    assert(ctx.cloud_connected == true);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-3：set_wash_progress 同时更新 step 和 mode
 * ------------------------------------------------------------------------- */
static void test_set_wash_progress(void)
{
    printf("TC-3: set_wash_progress updates both fields\n");

    (void)dev_ctx_init();

    dev_ctx_set_wash_progress(WASH_STEP_RINSE_REV, WASH_MODE_QUICK);
    device_context_t ctx = dev_ctx_snapshot();

    assert(ctx.wash_step == WASH_STEP_RINSE_REV);
    assert(ctx.wash_mode == WASH_MODE_QUICK);

    dev_ctx_set_wash_progress(WASH_STEP_IDLE, WASH_MODE_STANDARD);
    ctx = dev_ctx_snapshot();
    assert(ctx.wash_step == WASH_STEP_IDLE);
    assert(ctx.wash_mode == WASH_MODE_STANDARD);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-4：snapshot 返回值拷贝，修改快照不影响内部状态
 * ------------------------------------------------------------------------- */
static void test_snapshot_is_copy(void)
{
    printf("TC-4: snapshot returns value copy\n");

    (void)dev_ctx_init();
    dev_ctx_set_device_state(DEV_STATE_IDLE);

    device_context_t snap1 = dev_ctx_snapshot();
    assert(snap1.device_state == DEV_STATE_IDLE);

    snap1.device_state = DEV_STATE_FAULT;

    device_context_t snap2 = dev_ctx_snapshot();
    assert(snap2.device_state == DEV_STATE_IDLE);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-5：cloud_status 切换
 * ------------------------------------------------------------------------- */
static void test_cloud_status_toggle(void)
{
    printf("TC-5: cloud_status toggle\n");

    (void)dev_ctx_init();

    dev_ctx_set_cloud_status(true);
    device_context_t ctx = dev_ctx_snapshot();
    assert(ctx.cloud_connected == true);

    dev_ctx_set_cloud_status(false);
    ctx = dev_ctx_snapshot();
    assert(ctx.cloud_connected == false);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-6：并发读写一致性
 * ------------------------------------------------------------------------- */
#define CONCURRENT_ITER     50000

static atomic_int s_stop_flag = 0;

static const dev_state_t k_states[2] = { DEV_STATE_IDLE, DEV_STATE_RUNNING };
static const wash_step_t k_steps[2]  = { WASH_STEP_IDLE, WASH_STEP_PREWASH };
static const wash_mode_t k_modes[2]  = { WASH_MODE_STANDARD, WASH_MODE_QUICK };

static void *writer_fn(void *arg)
{
    (void)arg;
    int idx = 0;
    for (int i = 0; i < CONCURRENT_ITER; i++)
    {
        dev_ctx_set_device_state(k_states[idx]);
        dev_ctx_set_wash_progress(k_steps[idx], k_modes[idx]);
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

        assert(ctx.device_state == k_states[0] || ctx.device_state == k_states[1]);
        assert(ctx.wash_step    == k_steps[0]  || ctx.wash_step    == k_steps[1]);
        assert(ctx.wash_mode    == k_modes[0]  || ctx.wash_mode    == k_modes[1]);
    }
    return NULL;
}

static void test_concurrent_read_write(void)
{
    printf("TC-6: concurrent read/write consistency\n");

    (void)dev_ctx_init();
    atomic_store(&s_stop_flag, 0);

    pthread_t writer, reader;
    pthread_create(&writer, NULL, writer_fn, NULL);
    pthread_create(&reader, NULL, reader_fn, NULL);

    pthread_join(writer, NULL);
    pthread_join(reader, NULL);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * 主函数
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== test_dev_ctx ===\n");

    test_init_defaults();
    test_set_device_state_isolated();
    test_set_wash_progress();
    test_snapshot_is_copy();
    test_cloud_status_toggle();
    test_concurrent_read_write();

    printf("=== ALL PASSED ===\n");
    return 0;
}
