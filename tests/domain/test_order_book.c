/**
 * @file    test_order_book.c
 * @brief   内存订单簿 FIFO / 满队 / 回队首契约测试
 */

#include "common/sw_error.h"
#include "domain/job/job_types.h"
#include "domain/job/machine_profile.h"
#include "domain/job/order_book.h"
#include "tests/stubs/test_wash_modes.h"
#include "wdf_test_spec.h"

static job_desc_t make_order(uint64_t order_id, wash_mode_t mode)
{
    job_desc_t order;

    order.order_id       = order_id;
    order.job_id         = 0U;
    order.correlation_id = order_id;
    order.mode           = mode;
    order.enqueued_ms    = 0U;
    return order;
}

void setUp(void)
{
    machine_profile_reset_for_test();
    order_book_reset_for_test();
}

void tearDown(void)
{
}

static void test_fifo_enqueue_pop(void)
{
    job_desc_t a = make_order(0U, TEST_WASH_MODE_A);
    job_desc_t b = make_order(0U, TEST_WASH_MODE_B);
    job_desc_t first;
    job_desc_t second;
    uint64_t   id_a = 0U;
    uint64_t   id_b = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&a, &id_a));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&b, &id_b));
    TEST_ASSERT_EQUAL_UINT(2U, order_book_count());
    TEST_ASSERT_TRUE(id_a != 0U);
    TEST_ASSERT_TRUE(id_b != id_a);

    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_pop_front(&first));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_pop_front(&second));
    TEST_ASSERT_EQUAL_UINT64(id_a, first.order_id);
    TEST_ASSERT_EQUAL_UINT64(id_b, second.order_id);
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_A, first.mode);
    TEST_ASSERT_EQUAL_INT(TEST_WASH_MODE_B, second.mode);
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

static void test_same_order_id_is_deduped(void)
{
    job_desc_t first  = make_order(9U, TEST_WASH_MODE_A);
    job_desc_t second = make_order(9U, TEST_WASH_MODE_B);
    uint64_t   id_a   = 0U;
    uint64_t   id_b   = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&first, &id_a));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&second, &id_b));
    TEST_ASSERT_EQUAL_UINT64(9U, id_a);
    TEST_ASSERT_EQUAL_UINT64(9U, id_b);
    TEST_ASSERT_EQUAL_UINT(1U, order_book_count());
}

static void test_cancel_middle_keeps_order(void)
{
    job_desc_t a = make_order(0U, TEST_WASH_MODE_A);
    job_desc_t b = make_order(0U, TEST_WASH_MODE_B);
    job_desc_t c = make_order(0U, TEST_WASH_MODE_A);
    job_desc_t head;
    uint64_t   id_a = 0U;
    uint64_t   id_b = 0U;
    uint64_t   id_c = 0U;

    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&a, &id_a));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&b, &id_b));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&c, &id_c));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_cancel(id_b));
    TEST_ASSERT_EQUAL_UINT(2U, order_book_count());
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_peek_front(&head));
    TEST_ASSERT_EQUAL_UINT64(id_a, head.order_id);
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_pop_front(&head));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_pop_front(&head));
    TEST_ASSERT_EQUAL_UINT64(id_c, head.order_id);
}

static void test_overflow_uses_profile(void)
{
    machine_profile_t profile = machine_profile_get();
    job_desc_t        first   = make_order(1U, TEST_WASH_MODE_A);
    job_desc_t        second  = make_order(2U, TEST_WASH_MODE_B);
    job_desc_t        third   = make_order(3U, TEST_WASH_MODE_A);
    uint64_t          id      = 0U;

    profile.max_queued_orders = 2U;
    TEST_ASSERT_EQUAL_INT(SW_OK, machine_profile_register(&profile));

    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&first, &id));
    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&second, &id));
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, order_book_enqueue(&third, &id));
    TEST_ASSERT_EQUAL_UINT(2U, order_book_count());
}

static void test_clear_empties(void)
{
    job_desc_t order = make_order(1U, TEST_WASH_MODE_A);

    TEST_ASSERT_EQUAL_INT(SW_OK, order_book_enqueue(&order, NULL));
    order_book_clear();
    TEST_ASSERT_EQUAL_UINT(0U, order_book_count());
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_fifo_enqueue_pop, "", "验证订单簿按入队顺序弹出");
    WDF_RUN_TEST(test_same_order_id_is_deduped, "", "验证同一订单号重复入簿去重");
    WDF_RUN_TEST(test_cancel_middle_keeps_order, "", "验证取消中间单后仍保持 FIFO");
    WDF_RUN_TEST(test_overflow_uses_profile, "", "验证满队按 profile 拒单");
    WDF_RUN_TEST(test_clear_empties, "", "验证清簿后深度为 0");
    return UNITY_END();
}
