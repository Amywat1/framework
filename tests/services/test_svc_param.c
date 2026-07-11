/**
 * @file    test_svc_param.c
 * @brief   svc_param 参数服务单元测试
 */

#include "adapters/outbound/storage/json/json_param_store.h"
#include "common/sw_error.h"
#include "services/param/svc_param.h"
#include "unity.h"

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
    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, svc_param_init());
}

static void test_get_int_default_when_key_missing(void)
{
    (void)svc_param_init();
    TEST_ASSERT_EQUAL_INT(99, svc_param_get_int("missingKey", 99));
}

static void test_set_get_int_roundtrip(void)
{
    (void)svc_param_init();

    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_set_int(PARAM_KEY_WASH_MODE, 2));
    TEST_ASSERT_EQUAL_INT(2, svc_param_get_int(PARAM_KEY_WASH_MODE, 0));
}

static void test_get_int_from_loaded_file(void)
{
    write_json_file("{\"washMode\":5}");
    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_init());
    TEST_ASSERT_EQUAL_INT(5, svc_param_get_int(PARAM_KEY_WASH_MODE, 0));
}

static void test_set_str_and_get_str(void)
{
    char buf[32] = {0};

    (void)svc_param_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_set_str("deviceName", "demo-unit"));
    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_get_str("deviceName", buf, sizeof(buf), "none"));
    TEST_ASSERT_EQUAL_STRING("demo-unit", buf);
}

static void test_get_str_uses_default_when_missing(void)
{
    char buf[32] = {0};

    (void)svc_param_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_get_str("missing", buf, sizeof(buf), "fallback"));
    TEST_ASSERT_EQUAL_STRING("fallback", buf);
}

static void test_get_str_rejects_null_buf(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, svc_param_get_str("k", NULL, 32, "d"));
}

static void test_set_int_rejects_null_key(void)
{
    (void)svc_param_init();
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, svc_param_set_int(NULL, 1));
}

static void test_save_persists_values(void)
{
    (void)svc_param_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_set_int(PARAM_KEY_WASH_MODE, 7));
    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_save());

    json_param_store_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, svc_param_init());
    TEST_ASSERT_EQUAL_INT(7, svc_param_get_int(PARAM_KEY_WASH_MODE, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_empty_file_returns_storage_err);
    RUN_TEST(test_get_int_default_when_key_missing);
    RUN_TEST(test_set_get_int_roundtrip);
    RUN_TEST(test_get_int_from_loaded_file);
    RUN_TEST(test_set_str_and_get_str);
    RUN_TEST(test_get_str_uses_default_when_missing);
    RUN_TEST(test_get_str_rejects_null_buf);
    RUN_TEST(test_set_int_rejects_null_key);
    RUN_TEST(test_save_persists_values);
    return UNITY_END();
}
