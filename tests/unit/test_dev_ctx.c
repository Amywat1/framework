/**
 * @file    test_dev_ctx.c
 * @brief   dev_ctx 单元测试
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    测试策略：
 *          - 验证 dev_ctx_init() 清零各字段为安全初始值
 *          - 验证各分片写入接口只改动对应字段（其余字段不受影响）
 *          - 验证 dev_ctx_snapshot() 返回值拷贝（不共享指针）
 *          - 验证并发读写下快照一致性（写者和读者同时运行，
 *            快照中字段必须处于合法枚举值范围内）
 */

#include "service/dev_ctx/dev_ctx.h"
#include "domain/model/device_state.h"
#include "domain/model/safety_types.h"
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
    assert(ctx.safety_state  == SAFETY_STATE_OK);
    assert(ctx.wash_step     == WASH_STEP_IDLE);
    assert(ctx.wash_mode     == WASH_MODE_STANDARD);
    assert(ctx.has_error_alarm == false);
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

    /* 先写一些非默认值到其他字段 */
    dev_ctx_set_safety_state(SAFETY_STATE_LOCKOUT);
    dev_ctx_set_wash_progress(WASH_STEP_PREWASH, WASH_MODE_QUICK);
    dev_ctx_set_alarm_state(true);
    dev_ctx_set_cloud_status(true);

    /* 只改 device_state */
    dev_ctx_set_device_state(DEV_STATE_RUN);
    device_context_t ctx = dev_ctx_snapshot();

    assert(ctx.device_state    == DEV_STATE_RUN);
    assert(ctx.safety_state    == SAFETY_STATE_LOCKOUT);
    assert(ctx.wash_step       == WASH_STEP_PREWASH);
    assert(ctx.wash_mode       == WASH_MODE_QUICK);
    assert(ctx.has_error_alarm == true);
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

    /* 再次修改 */
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

    /* 在外部修改 snap1（不应影响内部状态）*/
    snap1.device_state = DEV_STATE_FAULT;

    /* 再次快照，应仍然是 IDLE */
    device_context_t snap2 = dev_ctx_snapshot();
    assert(snap2.device_state == DEV_STATE_IDLE);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-5：alarm_state 和 cloud_status 单独切换
 * ------------------------------------------------------------------------- */
static void test_bool_fields_toggle(void)
{
    printf("TC-5: bool fields toggle independently\n");

    (void)dev_ctx_init();

    dev_ctx_set_alarm_state(true);
    dev_ctx_set_cloud_status(false);
    device_context_t ctx = dev_ctx_snapshot();
    assert(ctx.has_error_alarm == true);
    assert(ctx.cloud_connected == false);

    dev_ctx_set_alarm_state(false);
    dev_ctx_set_cloud_status(true);
    ctx = dev_ctx_snapshot();
    assert(ctx.has_error_alarm == false);
    assert(ctx.cloud_connected == true);

    printf("  PASS\n");
}

/* -------------------------------------------------------------------------
 * TC-6：并发读写一致性
 *       写者线程循环交替写两组合法值；读者线程循环读取快照，
 *       断言字段值必须是两组中的某一个（不出现撕裂值）
 * ------------------------------------------------------------------------- */
#define CONCURRENT_ITER     50000

static atomic_int s_stop_flag = 0;

/* 两组合法值 */
static const dev_state_t    k_states[2]  = { DEV_STATE_IDLE, DEV_STATE_RUN };
static const safety_state_t k_safety[2]  = { SAFETY_STATE_OK, SAFETY_STATE_LOCKOUT };
static const wash_step_t    k_steps[2]   = { WASH_STEP_IDLE, WASH_STEP_PREWASH };
static const wash_mode_t    k_modes[2]   = { WASH_MODE_STANDARD, WASH_MODE_QUICK };

static void *writer_fn(void *arg)
{
    (void)arg;
    int idx = 0;
    for (int i = 0; i < CONCURRENT_ITER; i++)
    {
        dev_ctx_set_device_state(k_states[idx]);
        dev_ctx_set_safety_state(k_safety[idx]);
        dev_ctx_set_wash_progress(k_steps[idx], k_modes[idx]);
        dev_ctx_set_alarm_state((idx == 1));
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

        /* 每个字段必须是某个合法枚举值 */
        assert(ctx.device_state == k_states[0] || ctx.device_state == k_states[1]);
        assert(ctx.safety_state == k_safety[0] || ctx.safety_state == k_safety[1]);
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
    test_bool_fields_toggle();
    test_concurrent_read_write();

    printf("=== ALL PASSED ===\n");
    return 0;
}
