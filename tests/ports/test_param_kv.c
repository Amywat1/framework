/**
 * @file    test_param_kv.c
 * @brief   param_kv 整型/字符串便利层单元测试
 */

#include "adapters/outbound/storage/json/json_param_store.h"
#include "common/sw_error.h"
#include "domain/ports/outbound/storage/param_kv.h"
#include "wdf_test_spec.h"

#include <stdio.h>
#include <string.h>

static void write_json_file(const char *content)
{
    FILE *fp = fopen(PARAM_STORE_JSON_FILE_PATH, "w");

    TEST_ASSERT_NOT_NULL(fp);
    if (content != NULL) {
        fputs(content, fp);
    }
    fclose(fp);
}

static void remove_json_file(void)
{
    (void)remove(PARAM_STORE_JSON_FILE_PATH);
}

void setUp(void)
{
    remove_json_file();
    json_param_store_register();
}

void tearDown(void)
{
    remove_json_file();
}

static void test_init_empty_file_returns_storage_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, param_kv_init());
}

static void test_get_int_default_when_key_missing(void)
{
    (void)param_kv_init();
    TEST_ASSERT_EQUAL_INT(99, param_kv_get_int("missingKey", 99));
}

static void test_set_get_int_roundtrip(void)
{
    (void)param_kv_init();

    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_set_int(PARAM_KEY_WASH_MODE, 2));
    TEST_ASSERT_EQUAL_INT(2, param_kv_get_int(PARAM_KEY_WASH_MODE, 0));
}

static void test_get_int_from_loaded_file(void)
{
    write_json_file("{\"washMode\":5}");
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_init());
    TEST_ASSERT_EQUAL_INT(5, param_kv_get_int(PARAM_KEY_WASH_MODE, 0));
}

static void test_set_str_and_get_str(void)
{
    char buf[32] = {0};

    (void)param_kv_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_set_str("deviceName", "demo-unit"));
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_get_str("deviceName", buf, sizeof(buf), "none"));
    TEST_ASSERT_EQUAL_STRING("demo-unit", buf);
}

static void test_get_str_uses_default_when_missing(void)
{
    char buf[32] = {0};

    (void)param_kv_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_get_str("missing", buf, sizeof(buf), "fallback"));
    TEST_ASSERT_EQUAL_STRING("fallback", buf);
}

static void test_get_str_rejects_null_buf(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, param_kv_get_str("k", NULL, 32, "d"));
}

static void test_set_int_rejects_null_key(void)
{
    (void)param_kv_init();
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, param_kv_set_int(NULL, 1));
}

static void test_save_persists_values(void)
{
    (void)param_kv_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_set_int(PARAM_KEY_WASH_MODE, 7));
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_save());

    json_param_store_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_init());
    TEST_ASSERT_EQUAL_INT(7, param_kv_get_int(PARAM_KEY_WASH_MODE, 0));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_init_empty_file_returns_storage_err, "", "验证初始化空文件返回存储错误");
    WDF_RUN_TEST(test_get_int_default_when_key_missing, "", "验证整数键缺失时返回默认值");
    WDF_RUN_TEST(test_set_get_int_roundtrip, "", "验证设置获取整数往返编解码");
    WDF_RUN_TEST(test_get_int_from_loaded_file, "", "验证从已加载文件读取整数值");
    WDF_RUN_TEST(test_set_str_and_get_str, "", "验证设置字符串并获取字符串");
    WDF_RUN_TEST(test_get_str_uses_default_when_missing, "", "验证字符串键缺失时返回默认值");
    WDF_RUN_TEST(test_get_str_rejects_null_buf, "", "验证获取字符串拒绝空指针缓冲区");
    WDF_RUN_TEST(test_set_int_rejects_null_key, "", "验证设置整数拒绝空指针键");
    WDF_RUN_TEST(test_save_persists_values, "", "验证保存持久化值");
    return UNITY_END();
}
