/**
 * @file    test_job_admit.c
 * @brief   放行核：入队/启动放行/占用/急停清队/profile 契约
 */

#include "application/job_admit.h"
#include "application/side_effect_router.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/job/active_jobs.h"
#include "domain/job/job_types.h"
#include "domain/job/machine_profile.h"
#include "domain/job/order_book.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "tests/stubs/device_ops_stub.h"
#include "tests/stubs/test_wash_modes.h"
#include "wdf_test_spec.h"

#include <string.h>

static device_ops_t s_ops;

static void enter_standby(void)
{
    dev_cmd_t recover = dev_cmd_make_simple(DEV_CMD_RECOVER);

    (void)op_mode_handle_command(&recover);
    op_mode_on_recovery_completed(RECOVERY_RESULT_STANDBY);
}

static sw_err_t submit_mode(wash_mode_t mode)
{
    dev_cmd_t cmd = dev_cmd_make_submit_order(mode);

    if (op_mode_handle_command(&cmd).verdict != OP_CMD_ALLOWED) {
        return SW_ERR_STATE;
    }
    return side_effect_router_run(&cmd, op_mode_get_current());
}

void setUp(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, operational_mode_init());
    machine_profile_reset_for_test();
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_init());
    device_ops_stub_reset();
    memset(&s_ops, 0, sizeof(s_ops));
    device_ops_stub_bind(&s_ops);
    device_ops_register(&s_ops);
}

void tearDown(void)
{
    device_ops_register(NULL);
    (void)event_bus_shutdown();
}

static void test_submit_without_admit_ready_only_queues(void)
{
    enter_standby();
    device_ops_stub_set_admit_ready(false);

    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_UINT64(1U, job_admit_last_submit_id());
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_UINT64(2U, job_admit_last_submit_id());
    TEST_ASSERT_EQUAL_UINT(2U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());
}

static void test_start_releases_fifo(void)
{
    enter_standby();
    device_ops_stub_set_admit_ready(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(2U, order_book_count());

    device_ops_stub_set_admit_ready(true);
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_A, device_ops_stub_last_mode());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(1U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_ACTIVE, op_mode_get_current());
}

static void test_occupancy_full_keeps_queue(void)
{
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(1U, active_jobs_count());

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
}

static void test_multi_active_profile_start_each(void)
{
    machine_profile_t profile = {
        .max_active_jobs      = 3U,
        .max_queued_orders    = 8U,
        .hold_after_last_job  = false,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, machine_profile_register(&profile));
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(3, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(3U, active_jobs_count());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_INT(OP_MODE_ACTIVE, op_mode_get_current());
}

static void test_last_job_hold(void)
{
    job_desc_t head;

    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    job_admit_on_completed(head.job_id);
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_HOLD, op_mode_get_current());
}

static void test_last_job_standby_without_hold(void)
{
    machine_profile_t profile = {
        .max_active_jobs      = 1U,
        .max_queued_orders    = 8U,
        .hold_after_last_job  = false,
    };
    job_desc_t head;

    TEST_ASSERT_EQUAL_INT(SW_OK, machine_profile_register(&profile));
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    job_admit_on_completed(head.job_id);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());
}

static void test_service_off_clears_book(void)
{
    dev_cmd_t off = dev_cmd_make_set_service(false);

    enter_standby();
    device_ops_stub_set_admit_ready(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&off).verdict);
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&off, OP_MODE_STANDBY));
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

static void test_estop_rejects_new_order(void)
{
    enter_standby();
    op_mode_on_estop(true);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

static sw_err_t stub_stop_outputs(void)
{
    return SW_OK;
}

static void test_submit_succeeds_when_start_job_fails(void)
{
    enter_standby();
    device_ops_stub_set_start_result(SW_ERR_BUSY);
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());
}

static void test_duplicate_order_id_does_not_enqueue_twice(void)
{
    dev_cmd_t first  = dev_cmd_make_submit_order_id(TEST_WASH_MODE_A, 77U);
    dev_cmd_t second = dev_cmd_make_submit_order_id(TEST_WASH_MODE_B, 77U);

    enter_standby();
    device_ops_stub_set_admit_ready(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&first, OP_MODE_STANDBY));
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&second, OP_MODE_STANDBY));
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
}

static void test_expired_order_is_dropped(void)
{
    machine_profile_t profile = machine_profile_get();
    job_desc_t        stale;
    job_desc_t        fresh;
    uint64_t          now_ms;

    profile.max_order_age_ms = 10U;
    TEST_ASSERT_EQUAL_INT(SW_OK, machine_profile_register(&profile));
    enter_standby();
    device_ops_stub_set_admit_ready(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_peek_front(&stale));
    now_ms            = time_util_get_ms();
    stale.enqueued_ms = (now_ms > 1000U) ? (now_ms - 1000U) : 1U;
    memset(&fresh, 0, sizeof(fresh));
    fresh.mode         = TEST_WASH_MODE_B;
    fresh.enqueued_ms  = now_ms;
    order_book_clear();
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&stale, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&fresh, NULL));
    device_ops_stub_set_admit_ready(true);
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_B, device_ops_stub_last_mode());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(1U, active_jobs_count());
}

static void test_expire_drops_stale_without_start(void)
{
    machine_profile_t profile = machine_profile_get();
    job_desc_t        stale;
    job_desc_t        fresh;
    job_desc_t        head;
    uint64_t          now_ms;

    profile.max_order_age_ms = 10U;
    TEST_ASSERT_EQUAL_INT(SW_OK, machine_profile_register(&profile));
    enter_standby();
    device_ops_stub_set_admit_ready(false);
    now_ms = time_util_get_ms();
    memset(&stale, 0, sizeof(stale));
    stale.mode         = TEST_WASH_MODE_A;
    stale.enqueued_ms  = (now_ms > 1000U) ? (now_ms - 1000U) : 1U;
    memset(&fresh, 0, sizeof(fresh));
    fresh.mode        = TEST_WASH_MODE_B;
    fresh.enqueued_ms = now_ms;
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&stale, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&fresh, NULL));
    job_admit_expire();
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_peek_front(&head));
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_B, head.mode);
}

static void test_stop_all_clears_book_and_jobs(void)
{
    dev_cmd_t stop = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    s_ops.stop_all_outputs = stub_stop_outputs;
    device_ops_register(&s_ops);
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, op_mode_handle_command(&stop).verdict);
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&stop, OP_MODE_ACTIVE));
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
}

static void test_job_abort_requests_clear(void)
{
    job_desc_t head;

    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    job_admit_on_job_aborted(head.job_id);
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_CLEARING, op_mode_get_current());
}

static void test_estop_abort_clears_queued(void)
{
    enter_standby();
    device_ops_stub_set_admit_ready(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    job_admit_on_aborted(WASH_ABORT_ESTOP);
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
}

static void test_post_job_blocking_clears_queued(void)
{
    static const alarm_def_t catalog[] = {
        {
         .code         = 201101U,
         .level        = ALARM_LEVEL_MAJOR,
         .lifecycle        = ALARM_LIFECYCLE_LATCH,
         .reeval_group = ALARM_REEVAL_GROUP_NONE,
         .desc         = "test blocking",
         },
    };
    job_desc_t head;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(catalog, 1U));
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(201101U));
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    job_admit_on_completed(head.job_id);
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
}

static void test_job_abort_without_clear_keeps_queue(void)
{
    machine_profile_t profile = {
        .max_active_jobs   = 2U,
        .max_queued_orders = 8U,
    };
    job_desc_t head;

    TEST_ASSERT_EQUAL_INT(SW_OK, machine_profile_register(&profile));
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    job_admit_on_job_aborted(head.job_id);
    TEST_ASSERT_EQUAL_INT(2, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(1U, active_jobs_count());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_ACTIVE, op_mode_get_current());
}

static void test_stale_job_abort_is_ignored(void)
{
    job_desc_t head;

    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    TEST_ASSERT_EQUAL_UINT(1U, job_admit_drop_jobs(false));
    job_admit_on_job_aborted(head.job_id);
    TEST_ASSERT_EQUAL_INT(OP_MODE_ACTIVE, op_mode_get_current());
}

static void test_rejected_start_drops_order(void)
{
    enter_standby();
    device_ops_stub_set_start_result(SW_ERR_PARAM);
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(OP_MODE_STANDBY, op_mode_get_current());
}

static void test_active_order_id_is_deduped(void)
{
    dev_cmd_t first  = dev_cmd_make_submit_order_id(TEST_WASH_MODE_A, 77U);
    dev_cmd_t second = dev_cmd_make_submit_order_id(TEST_WASH_MODE_A, 77U);

    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&first, OP_MODE_STANDBY));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_UINT(1U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&second, OP_MODE_ACTIVE));
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
}

static void test_stop_all_aborts_job_admitted_after_verdict(void)
{
    dev_cmd_t stop = dev_cmd_make_simple(DEV_CMD_STOP_ALL_OUTPUTS);

    s_ops.stop_all_outputs = stub_stop_outputs;
    device_ops_register(&s_ops);
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, side_effect_router_run(&stop, OP_MODE_STANDBY));
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_abort_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
}

static void test_start_without_order_fails(void)
{
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
}

static void test_start_without_order_not_ready_fails(void)
{
    enter_standby();
    device_ops_stub_set_admit_ready(false);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
}

static void test_start_keeps_queued_mode(void)
{
    job_desc_t head;

    enter_standby();
    device_ops_stub_set_admit_ready(false);
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_peek_front(&head));
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_A, head.mode);
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
}

static void test_start_while_active_empty_book_fails(void)
{
    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(1U, active_jobs_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
}

static void test_hold_start_fails(void)
{
    job_desc_t head;

    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    job_admit_on_completed(head.job_id);
    TEST_ASSERT_EQUAL_INT(OP_MODE_HOLD, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
}

static void test_stopped_start_fails(void)
{
    TEST_ASSERT_EQUAL_INT(OP_MODE_STOPPED, op_mode_get_current());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, job_admit_start());
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
    TEST_ASSERT_EQUAL_INT(0, device_ops_stub_start_count());
}

static void test_complete_does_not_start_next(void)
{
    job_desc_t head;

    enter_standby();
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, submit_mode(TEST_WASH_MODE_B));
    TEST_ASSERT_EQUAL_INT(SW_OK, job_admit_start());
    TEST_ASSERT_EQUAL_INT(SW_OK, active_jobs_get_at(0U, &head));
    job_admit_on_completed(head.job_id);
    TEST_ASSERT_EQUAL_INT(1, device_ops_stub_start_count());
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
    TEST_ASSERT_EQUAL_UINT(0U, active_jobs_count());
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_submit_without_admit_ready_only_queues, "", "验证未准入时只入队不放行");
    WDF_RUN_TEST(test_start_releases_fifo, "", "验证启动按 FIFO 放行队首");
    WDF_RUN_TEST(test_occupancy_full_keeps_queue, "", "验证占用满时启动失败且后续单待队中");
    WDF_RUN_TEST(test_multi_active_profile_start_each, "", "验证占用未满时洗中启动可再放一单");
    WDF_RUN_TEST(test_last_job_hold, "", "验证最后一单结束后按默认 profile 进入 HOLD");
    WDF_RUN_TEST(test_last_job_standby_without_hold, "", "验证关闭 hold 后最后一单回 STANDBY");
    WDF_RUN_TEST(test_service_off_clears_book, "", "验证关总开关落到停机时清空订单簿");
    WDF_RUN_TEST(test_estop_rejects_new_order, "", "验证急停拒绝新单");
    WDF_RUN_TEST(test_submit_succeeds_when_start_job_fails, "", "验证放行失败仍保留订单且下单成功");
    WDF_RUN_TEST(test_duplicate_order_id_does_not_enqueue_twice, "", "验证同一订单号去重");
    WDF_RUN_TEST(test_expired_order_is_dropped, "", "验证过期订单在放行前淘汰");
    WDF_RUN_TEST(test_expire_drops_stale_without_start, "", "验证周期淘汰过期队首且不开洗");
    WDF_RUN_TEST(test_stop_all_clears_book_and_jobs, "", "验证全停清空订单簿与占用");
    WDF_RUN_TEST(test_job_abort_requests_clear, "", "验证单车机型工艺中止后整机进入清障并清队");
    WDF_RUN_TEST(test_estop_abort_clears_queued, "", "验证急停整机中止清空订单簿");
    WDF_RUN_TEST(test_post_job_blocking_clears_queued, "", "验证洗完禁止接单停机时清空排队单");
    WDF_RUN_TEST(test_job_abort_without_clear_keeps_queue, "", "验证多车 profile 单车中止只撤该单不补放行");
    WDF_RUN_TEST(test_stale_job_abort_is_ignored, "", "验证已被撤占用的迟到中止不再请求清障");
    WDF_RUN_TEST(test_rejected_start_drops_order, "", "验证 start_job 永久拒绝时丢弃队首单");
    WDF_RUN_TEST(test_active_order_id_is_deduped, "", "验证在洗订单号重复下单不再入队");
    WDF_RUN_TEST(test_stop_all_aborts_job_admitted_after_verdict, "", "验证全停中止裁决前模式未及 ACTIVE 的在洗作业");
    WDF_RUN_TEST(test_start_without_order_fails, "", "验证空队启动失败");
    WDF_RUN_TEST(test_start_without_order_not_ready_fails, "",
                 "验证未准入空队启动失败");
    WDF_RUN_TEST(test_start_keeps_queued_mode, "", "验证启动不改已入队订单的套餐");
    WDF_RUN_TEST(test_start_while_active_empty_book_fails, "",
                 "验证在洗且空队时启动失败");
    WDF_RUN_TEST(test_hold_start_fails, "", "验证 HOLD 点启动失败");
    WDF_RUN_TEST(test_stopped_start_fails, "", "验证停机点启动失败");
    WDF_RUN_TEST(test_complete_does_not_start_next, "", "验证作业结束不自动放行下一单");
    return UNITY_END();
}
