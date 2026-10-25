/**
 * @file    test_wash_quota_persist.c
 * @brief   次数授权经参数存储落盘后，重新 init 仍恢复剩余次数
 */

#include "adapters/outbound/storage/json/json_param_store.h"
#include "application/wash_quota.h"
#include "common/sw_error.h"
#include "domain/ports/outbound/storage/param_kv.h"
#include "domain/ports/outbound/storage/param_store.h"
#include "wdf_test_spec.h"

#include <stdio.h>

static void remove_store_files(void)
{
    char path[512];

    (void)remove(PARAM_STORE_JSON_FILE_PATH);
    (void)snprintf(path, sizeof(path), "%s.a", PARAM_STORE_JSON_FILE_PATH);
    (void)remove(path);
    (void)snprintf(path, sizeof(path), "%s.b", PARAM_STORE_JSON_FILE_PATH);
    (void)remove(path);
    (void)snprintf(path, sizeof(path), "%s.a.tmp", PARAM_STORE_JSON_FILE_PATH);
    (void)remove(path);
    (void)snprintf(path, sizeof(path), "%s.b.tmp", PARAM_STORE_JSON_FILE_PATH);
    (void)remove(path);
}

void setUp(void)
{
    remove_store_files();
    json_param_store_register();
    (void)param_kv_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_init());
}

void tearDown(void)
{
    (void)param_store_register(NULL);
    remove_store_files();
}

static void test_remaining_survives_reinit(void)
{
    wash_quota_info_t info = {0};

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_apply_grant(5U, 3U));
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_consume_one());

    wash_quota_reset_for_test();
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_quota_init());

    wash_quota_get_info(&info);
    TEST_ASSERT_FALSE(info.monitorable);
    TEST_ASSERT_EQUAL_UINT8(4U, info.remaining);
    TEST_ASSERT_EQUAL_UINT32(3U, info.grant_serial);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_remaining_survives_reinit, "QUOTA-06",
                 "验证授权次数落盘后重新初始化仍恢复剩余次数");
    return UNITY_END();
}
