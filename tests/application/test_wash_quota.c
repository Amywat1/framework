/**
 * @file    test_wash_quota.c
 * @brief   离线开洗次数授权：可监控放行、离线扣次、连通不续额、序号授权
 */

#include "application/wash_quota.h"
#include "common/sw_error.h"
#include "wdf_test_spec.h"

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_init());
}

void tearDown(void)
{
}

static void test_default_offline_denies(void)
{
    bool need_consume = true;

    TEST_ASSERT_FALSE(wash_quota_is_monitorable());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, wash_quota_check_start(&need_consume));
    TEST_ASSERT_FALSE(need_consume);
}

static void test_monitorable_allows_without_consume(void)
{
    bool              need_consume = true;
    wash_quota_info_t info         = {0};

    wash_quota_set_monitorable(true);
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_check_start(&need_consume));
    TEST_ASSERT_FALSE(need_consume);
    wash_quota_get_info(&info);
    TEST_ASSERT_EQUAL_UINT8(0U, info.remaining);
}

static void test_offline_grant_then_consume(void)
{
    bool              need_consume = false;
    wash_quota_info_t info         = {0};

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_apply_grant(2U, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_check_start(&need_consume));
    TEST_ASSERT_TRUE(need_consume);
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_consume_one());
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_consume_one());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, wash_quota_consume_one());
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, wash_quota_check_start(&need_consume));
    wash_quota_get_info(&info);
    TEST_ASSERT_EQUAL_UINT8(0U, info.remaining);
    TEST_ASSERT_EQUAL_UINT32(1U, info.grant_serial);
}

static void test_monitorable_edge_does_not_refill(void)
{
    bool              need_consume = false;
    wash_quota_info_t info         = {0};

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_apply_grant(1U, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_consume_one());
    wash_quota_set_monitorable(true);
    wash_quota_set_monitorable(false);
    wash_quota_get_info(&info);
    TEST_ASSERT_EQUAL_UINT8(0U, info.remaining);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, wash_quota_check_start(&need_consume));
}

static void test_stale_serial_rejected(void)
{
    wash_quota_info_t info = {0};

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_apply_grant(3U, 5U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, wash_quota_apply_grant(9U, 5U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, wash_quota_apply_grant(9U, 4U));
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_apply_grant(1U, 6U));
    wash_quota_get_info(&info);
    TEST_ASSERT_EQUAL_UINT8(1U, info.remaining);
}

static void test_set_remaining_bumps_serial(void)
{
    wash_quota_info_t info = {0};

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_set_remaining(7U));
    wash_quota_get_info(&info);
    TEST_ASSERT_EQUAL_UINT8(7U, info.remaining);
    TEST_ASSERT_EQUAL_UINT32(1U, info.grant_serial);
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_set_remaining(7U));
    wash_quota_get_info(&info);
    TEST_ASSERT_EQUAL_UINT32(1U, info.grant_serial);
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_set_remaining(3U));
    wash_quota_get_info(&info);
    TEST_ASSERT_EQUAL_UINT8(3U, info.remaining);
    TEST_ASSERT_EQUAL_UINT32(2U, info.grant_serial);
}

static void test_check_null_is_param(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, wash_quota_check_start(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_default_offline_denies, "QUOTA-05", "验证上电无授权时离线拒绝开洗");
    WDF_RUN_TEST(test_monitorable_allows_without_consume, "QUOTA-01", "验证可监控时开洗不扣次数");
    WDF_RUN_TEST(test_offline_grant_then_consume, "QUOTA-02", "验证离线授权次数用完后拒绝");
    WDF_RUN_TEST(test_monitorable_edge_does_not_refill, "QUOTA-03", "验证可监控边沿不回满次数");
    WDF_RUN_TEST(test_stale_serial_rejected, "QUOTA-04", "验证授权序号不递增则拒绝改写");
    WDF_RUN_TEST(test_set_remaining_bumps_serial, "QUOTA-07",
                 "验证只写剩余次数时本机自增序号且相同值不改序号");
    WDF_RUN_TEST(test_check_null_is_param, "", "验证 check_start 空指针返回参数错误");
    return UNITY_END();
}
