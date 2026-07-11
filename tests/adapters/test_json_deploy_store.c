/**
 * @file    test_json_deploy_store.c
 * @brief   json_deploy_store 部署配置存储适配器单元测试
 *
 * 分组：
 *   A. load
 *   B. get
 */

#include "common/sw_error.h"
#include "ports/outbound/storage/deploy_store.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

void json_deploy_store_register(void);

static const deploy_store_ops_t *store(void)
{
    return deploy_store_get_ops();
}

static void write_json_file(const char *content)
{
    FILE *fp = fopen(DEPLOY_STORE_JSON_FILE_PATH, "w");
    TEST_ASSERT_NOT_NULL(fp);
    if (content != NULL) {
        fputs(content, fp);
    }
    fclose(fp);
}

static void remove_json_file(void)
{
    (void)remove(DEPLOY_STORE_JSON_FILE_PATH);
}

void setUp(void)
{
    remove_json_file();
    json_deploy_store_register();
}

void tearDown(void)
{
    remove_json_file();
}

static void test_load_file_not_found(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, store()->load());
}

static void test_load_valid_json_string_key(void)
{
    char buf[32];

    write_json_file("{\"deviceName\":\"M8-001\"}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("M8-001", buf);
}

static void test_load_valid_json_number_key(void)
{
    char buf[32];

    write_json_file("{\"siteId\":1001}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("siteId", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("1001", buf);
}

static void test_load_invalid_json(void)
{
    write_json_file("{ invalid json");
    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, store()->load());
}

static void test_get_missing_key(void)
{
    char buf[16];

    write_json_file("{}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get("no_such_key", buf, sizeof(buf)));
}

static void test_get_without_load(void)
{
    char buf[16];

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get("deviceName", buf, sizeof(buf)));
}

static void test_get_null_args(void)
{
    char buf[16];

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get("key", NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get("key", buf, 0U));
}

static void test_get_truncates_long_string(void)
{
    char buf[8];

    write_json_file("{\"token\":\"ABCDEFGHIJ\"}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("token", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ABCDEFG", buf);
}

static void test_reload_overwrites_config(void)
{
    char buf[32];

    write_json_file("{\"deviceName\":\"old\"}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("old", buf);

    write_json_file("{\"deviceName\":\"new\"}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("new", buf);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_load_file_not_found);
    RUN_TEST(test_load_valid_json_string_key);
    RUN_TEST(test_load_valid_json_number_key);
    RUN_TEST(test_load_invalid_json);
    RUN_TEST(test_get_missing_key);
    RUN_TEST(test_get_without_load);
    RUN_TEST(test_get_null_args);
    RUN_TEST(test_get_truncates_long_string);
    RUN_TEST(test_reload_overwrites_config);

    return UNITY_END();
}
