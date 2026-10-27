/**
 * @file    test_pending_start.c
 * @brief   未准入启动门锁与就位延迟放行
 */

#include "application/job_admit.h"
#include "application/pending_start.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/job/entry_occupancy.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/storage/param_kv.h"
#include "domain/telemetry/device_snapshot.h"
#include "runtime/scheduler/periodic_task.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <string.h>

static int                     s_delay_ms;
static operational_mode_t      s_mode;
static bool                    s_admit_ready;
static bool                    s_occ_available;
static entry_occupancy_phase_t s_occ_phase;
static unsigned                s_book_count;
static uint64_t                s_now_ms;
static unsigned                s_start_count;
static sw_err_t                s_start_ret;
static op_reject_reason_t      s_fail_reason;

int param_kv_get_int(const char *key, int default_val)
{
    if ((key != NULL) && (strcmp(key, PARAM_KEY_PENDING_START_DELAY_MS) == 0)) {
        return s_delay_ms;
    }
    return default_val;
}

device_snapshot_t device_snapshot_get(void)
{
    device_snapshot_t snap;

    memset(&snap, 0, sizeof(snap));
    snap.op.mode = s_mode;
    return snap;
}

entry_occupancy_t entry_occupancy_get(void)
{
    entry_occupancy_t view;

    view.available = s_occ_available;
    view.phase     = s_occ_phase;
    return view;
}

static bool stub_admit_ready(void)
{
    return s_admit_ready;
}

static const device_ops_t s_ops = {
    .is_admit_ready = stub_admit_ready,
};

const device_ops_t *device_ops_get(void)
{
    return &s_ops;
}

unsigned order_book_count(void)
{
    return s_book_count;
}

uint64_t time_util_get_ms(void)
{
    return s_now_ms;
}

sw_err_t job_admit_start(void)
{
    s_start_count++;
    return s_start_ret;
}

op_reject_reason_t job_admit_last_fail_reason(void)
{
    return s_fail_reason;
}

sw_err_t periodic_task_register(const char        *name,
                                uint32_t           period_ms,
                                periodic_task_fn_t fn,
                                void              *ctx,
                                int                policy,
                                int                prio,
                                size_t             stack)
{
    (void)name;
    (void)period_ms;
    (void)fn;
    (void)ctx;
    (void)policy;
    (void)prio;
    (void)stack;
    return SW_OK;
}

bool sw_mutex_init_prio_inherit(pthread_mutex_t *mutex)
{
    return pthread_mutex_init(mutex, NULL) == 0;
}

void sw_log_write(sw_log_level_t level, const char *source, const char *format, ...)
{
    (void)level;
    (void)source;
    (void)format;
}

const char *sw_log_source_file_name(const char *source_path)
{
    return source_path;
}

void setUp(void)
{
    s_delay_ms      = 2000;
    s_mode          = OP_MODE_STANDBY;
    s_admit_ready   = false;
    s_occ_available = true;
    s_occ_phase     = ENTRY_OCC_EMPTY;
    s_book_count    = 1U;
    s_now_ms        = 10000U;
    s_start_count   = 0U;
    s_start_ret     = SW_OK;
    s_fail_reason   = OP_REJECT_ENTRY_NOT_READY;
    pending_start_init();
}

void tearDown(void)
{
}

static void test_delay_zero_means_never(void)
{
    s_delay_ms = 0;
    TEST_ASSERT_EQUAL_UINT32(0U, pending_start_delay_ms());
    s_delay_ms = -3;
    TEST_ASSERT_EQUAL_UINT32(0U, pending_start_delay_ms());
}

static void test_delay_clamps_to_30s(void)
{
    s_delay_ms = 30001;
    TEST_ASSERT_EQUAL_UINT32(30000U, pending_start_delay_ms());
    s_delay_ms = 1500;
    TEST_ASSERT_EQUAL_UINT32(1500U, pending_start_delay_ms());
}

static void test_not_ready_start_is_remembered(void)
{
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_TRUE(pending_start_is_armed());
}

static void test_empty_book_is_not_remembered(void)
{
    s_fail_reason = OP_REJECT_QUEUE_EMPTY;
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_already_ready_fail_is_not_remembered(void)
{
    s_fail_reason = OP_REJECT_JOB_RETRY;
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_hold_fail_is_not_remembered(void)
{
    s_fail_reason = OP_REJECT_WRONG_MODE;
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_zero_delay_is_not_remembered(void)
{
    s_delay_ms = 0;
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_success_clears(void)
{
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_TRUE(pending_start_is_armed());
    pending_start_on_start_result(SW_OK);
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_ready_waits_full_delay(void)
{
    pending_start_on_start_result(SW_ERR_STATE);
    s_admit_ready = true;
    s_occ_phase   = ENTRY_OCC_READY;
    pending_start_tick();
    TEST_ASSERT_EQUAL_UINT(0U, s_start_count);
    s_now_ms += 1999U;
    pending_start_tick();
    TEST_ASSERT_EQUAL_UINT(0U, s_start_count);
    TEST_ASSERT_TRUE(pending_start_is_armed());
}

static void test_ready_starts_after_delay(void)
{
    pending_start_on_start_result(SW_ERR_STATE);
    s_admit_ready = true;
    s_occ_phase   = ENTRY_OCC_READY;
    pending_start_tick();
    s_now_ms += 2000U;
    pending_start_tick();
    TEST_ASSERT_EQUAL_UINT(1U, s_start_count);
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_leave_after_seen_forgets(void)
{
    s_occ_phase = ENTRY_OCC_OCCUPIED;
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_TRUE(pending_start_is_armed());
    s_occ_phase = ENTRY_OCC_EMPTY;
    pending_start_tick();
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_empty_bay_keeps_memory_until_arrival(void)
{
    s_occ_phase = ENTRY_OCC_EMPTY;
    pending_start_on_start_result(SW_ERR_STATE);
    pending_start_tick();
    TEST_ASSERT_TRUE(pending_start_is_armed());
    TEST_ASSERT_EQUAL_UINT(0U, s_start_count);
}

static void test_leave_ready_resets_delay(void)
{
    pending_start_on_start_result(SW_ERR_STATE);
    s_admit_ready = true;
    s_occ_phase   = ENTRY_OCC_READY;
    pending_start_tick();
    s_now_ms += 1500U;
    pending_start_tick();
    s_admit_ready = false;
    s_occ_phase   = ENTRY_OCC_OCCUPIED;
    pending_start_tick();
    s_admit_ready = true;
    s_occ_phase   = ENTRY_OCC_READY;
    pending_start_tick();
    s_now_ms += 1500U;
    pending_start_tick();
    TEST_ASSERT_EQUAL_UINT(0U, s_start_count);
    s_now_ms += 500U;
    pending_start_tick();
    TEST_ASSERT_EQUAL_UINT(1U, s_start_count);
}

static void test_leave_standby_forgets(void)
{
    pending_start_on_start_result(SW_ERR_STATE);
    s_mode = OP_MODE_STOPPED;
    pending_start_tick();
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

static void test_unavailable_occupancy_does_not_forget(void)
{
    s_occ_available = false;
    s_occ_phase     = ENTRY_OCC_EMPTY;
    pending_start_on_start_result(SW_ERR_STATE);
    TEST_ASSERT_TRUE(pending_start_is_armed());
    pending_start_tick();
    TEST_ASSERT_TRUE(pending_start_is_armed());

    s_admit_ready = true;
    pending_start_tick();
    s_now_ms += 2000U;
    pending_start_tick();
    TEST_ASSERT_EQUAL_UINT(1U, s_start_count);
    TEST_ASSERT_FALSE(pending_start_is_armed());
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_delay_zero_means_never, "PEND-01",
                 "延迟为 0 或负值表示不记住启动");
    WDF_RUN_TEST(test_delay_clamps_to_30s, "PEND-01",
                 "延迟超过 30 秒按 30000 ms 夹紧");
    WDF_RUN_TEST(test_not_ready_start_is_remembered, "ADMIT-02",
                 "未准入启动失败且延迟非 0 时记住");
    WDF_RUN_TEST(test_empty_book_is_not_remembered, "PEND-02",
                 "无队首时启动失败不记住");
    WDF_RUN_TEST(test_already_ready_fail_is_not_remembered, "PEND-02",
                 "已准入仍失败不记住");
    WDF_RUN_TEST(test_hold_fail_is_not_remembered, "PEND-02",
                 "HOLD 点启动失败不记住");
    WDF_RUN_TEST(test_zero_delay_is_not_remembered, "PEND-01",
                 "延迟为 0 时未准入启动不记住");
    WDF_RUN_TEST(test_success_clears, "PEND-02",
                 "启动成功清掉已记住的意图");
    WDF_RUN_TEST(test_ready_waits_full_delay, "PEND-02",
                 "准入后未满延迟不开洗");
    WDF_RUN_TEST(test_ready_starts_after_delay, "PEND-02",
                 "准入并满延迟后自动放行");
    WDF_RUN_TEST(test_leave_after_seen_forgets, "PEND-03",
                 "进过车位再空场则忘掉启动");
    WDF_RUN_TEST(test_empty_bay_keeps_memory_until_arrival, "PEND-03",
                 "空场点启动可记住，等车进来");
    WDF_RUN_TEST(test_leave_ready_resets_delay, "PEND-02",
                 "准入后又离开则重新计延迟");
    WDF_RUN_TEST(test_leave_standby_forgets, "PEND-02",
                 "离开待机忘掉未准入启动");
    WDF_RUN_TEST(test_unavailable_occupancy_does_not_forget, "PEND-03",
                 "占用口未注册不因空场忘掉，准入后仍可补放行");
    return UNITY_END();
}
