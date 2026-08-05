/**
 * @file    test_json_param_store.c
 * @brief   json_param_store 参数存储适配器单元测试
 *
 * 分组：
 *   A. load
 *   B. get
 *   C. set
 *   D. save 持久化
 */

#include "adapters/outbound/storage/json/json_param_store.h"
#include "common/sw_error.h"
#include "ports/outbound/storage/param_store.h"
#include "wdf_test_spec.h"

#include <stdio.h>
#include <string.h>

static const param_store_ops_t *store(void)
{
    return param_store_get_ops();
}

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

    write_json_file("{\"maxSpeed\":120}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("maxSpeed", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("120", buf);
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

static void test_get_null_args(void)
{
    char buf[16];

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get("key", NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->get("key", buf, 0U));
}

static void test_set_and_get_new_key(void)
{
    char buf[32];

    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->set("mode", "auto"));
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("mode", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("auto", buf);
}

static void test_set_update_string_key(void)
{
    char buf[32];

    write_json_file("{\"mode\":\"manual\"}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->set("mode", "auto"));
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("mode", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("auto", buf);
}

static void test_set_update_number_key(void)
{
    char buf[32];

    write_json_file("{\"count\":10}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->set("count", "20"));
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("count", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("20", buf);
}

static void test_set_null_args(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->set(NULL, "val"));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->set("key", NULL));
}

static void test_save_and_reload(void)
{
    char buf[32];

    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->set("siteId", "S001"));
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->save());

    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("siteId", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("S001", buf);
}

static void test_get_truncates_long_string(void)
{
    char buf[8];

    write_json_file("{\"token\":\"ABCDEFGHIJ\"}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("token", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ABCDEFG", buf);
}

static void test_save_empty_json_object(void)
{
    /* load 失败时会创建空对象，save 应写出 "{}" */
    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->save());

    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_load_file_not_found, "", "验证加载不存在的文件返回未找到");
    WDF_RUN_TEST(test_load_valid_json_string_key, "", "验证加载有效JSON字符串键");
    WDF_RUN_TEST(test_load_valid_json_number_key, "", "验证加载有效JSON数值键");
    WDF_RUN_TEST(test_load_invalid_json, "", "验证加载无效JSON");
    WDF_RUN_TEST(test_get_missing_key, "", "验证获取缺失键");
    WDF_RUN_TEST(test_get_null_args, "", "验证获取空指针参数");
    WDF_RUN_TEST(test_set_and_get_new_key, "", "验证设置并获取新键");
    WDF_RUN_TEST(test_set_update_string_key, "", "验证设置更新字符串键");
    WDF_RUN_TEST(test_set_update_number_key, "", "验证设置更新数值键");
    WDF_RUN_TEST(test_set_null_args, "", "验证设置空指针参数");
    WDF_RUN_TEST(test_save_and_reload, "", "验证保存并重新加载");
    WDF_RUN_TEST(test_save_empty_json_object, "", "验证保存空JSON对象");
    WDF_RUN_TEST(test_get_truncates_long_string, "", "验证读取长字符串时按缓冲区长度截断");

    return UNITY_END();
}
