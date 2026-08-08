/**
 * @file    test_json_deploy_store.c
 * @brief   json_deploy_store 部署配置存储适配器单元测试
 *
 * 分组：
 *   A. load
 *   B. get
 */

#include "adapters/outbound/storage/json/json_deploy_store.h"
#include "common/sw_error.h"
#include "domain/ports/outbound/storage/deploy_store.h"
#include "wdf_test_spec.h"

#include <stdio.h>
#include <string.h>

void json_deploy_store_register(void);

static const deploy_store_ops_t *store(void)
{
    return deploy_store_get_ops();
}

/** @brief 原样写入，供版本校验与非法 JSON 用例使用 */
static void write_json_raw(const char *content)
{
    FILE *fp = fopen(DEPLOY_STORE_JSON_FILE_PATH, "w");
    TEST_ASSERT_NOT_NULL(fp);
    if (content != NULL) {
        fputs(content, fp);
    }
    fclose(fp);
}

/**
 * @brief 写入带合规 schemaVersion 的配置
 *
 * @param body_fields 不含外层花括号的字段串；空串表示无其他字段
 * @note  加载已要求 schemaVersion，若让每个用例自行重复该字段，用例的关注点
 *        就会被版本串淹没；这里统一补上，版本本身的行为由专门用例覆盖。
 */
static void write_json_file(const char *body_fields)
{
    char json[256];

    if ((body_fields == NULL) || (body_fields[0] == '\0')) {
        (void)snprintf(json, sizeof(json), "{\"schemaVersion\":\"%s\"}", DEPLOY_CONFIG_SCHEMA_SUPPORTED);
    } else {
        (void)snprintf(
            json, sizeof(json), "{\"schemaVersion\":\"%s\",%s}", DEPLOY_CONFIG_SCHEMA_SUPPORTED, body_fields);
    }
    write_json_raw(json);
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

    write_json_file("\"deviceName\":\"DEV-001\"");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("DEV-001", buf);
}

static void test_load_valid_json_number_key(void)
{
    char buf[32];

    write_json_file("\"siteId\":1001");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("siteId", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("1001", buf);
}

static void test_load_invalid_json(void)
{
    write_json_raw("{ invalid json");
    TEST_ASSERT_EQUAL_INT(SW_ERR_STORAGE, store()->load());
}

static void test_get_missing_key(void)
{
    char buf[16];

    write_json_file("");
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

    write_json_file("\"token\":\"ABCDEFGHIJ\"");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("token", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ABCDEFG", buf);
}

static void test_reload_overwrites_config(void)
{
    char buf[32];

    write_json_file("\"deviceName\":\"old\"");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("old", buf);

    write_json_file("\"deviceName\":\"new\"");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("new", buf);
}

/* -------------------------------------------------------------------------
 * C. schema 版本校验
 * ------------------------------------------------------------------------- */

static void test_load_rejects_missing_schema_version(void)
{
    write_json_raw("{\"deviceName\":\"DEV-001\"}");
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, store()->load());
}

static void test_load_rejects_major_version_mismatch(void)
{
    write_json_raw("{\"schemaVersion\":\"2.0\",\"deviceName\":\"DEV-001\"}");
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, store()->load());
}

static void test_load_rejects_higher_minor_version(void)
{
    write_json_raw("{\"schemaVersion\":\"1.9\",\"deviceName\":\"DEV-001\"}");
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, store()->load());
}

static void test_load_accepts_patch_suffix(void)
{
    char buf[32];

    /* 修订号不参与兼容判定，"1.0.7" 应与 "1.0" 同样被接受 */
    write_json_raw("{\"schemaVersion\":\"1.0.7\",\"deviceName\":\"DEV-001\"}");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("DEV-001", buf);
}

static void test_rejected_version_keeps_previous_config(void)
{
    char buf[32];

    write_json_file("\"deviceName\":\"good\"");
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->load());

    /* 新配置整份丢弃，上一次成功的内容保留：重新加载时清空会连设备身份一起
     * 丢掉，故障面反而更大。首次加载失败由 bootstrap 终止启动。 */
    write_json_raw("{\"schemaVersion\":\"2.0\",\"deviceName\":\"bad\"}");
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, store()->load());
    TEST_ASSERT_EQUAL_INT(SW_OK, store()->get("deviceName", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("good", buf);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_load_file_not_found, "", "验证加载不存在的文件返回未找到");
    WDF_RUN_TEST(test_load_valid_json_string_key, "", "验证加载有效JSON字符串键");
    WDF_RUN_TEST(test_load_valid_json_number_key, "", "验证加载有效JSON数值键");
    WDF_RUN_TEST(test_load_invalid_json, "", "验证加载无效JSON");
    WDF_RUN_TEST(test_get_missing_key, "", "验证获取缺失键");
    WDF_RUN_TEST(test_get_without_load, "", "验证未加载配置时拒绝读取");
    WDF_RUN_TEST(test_get_null_args, "", "验证获取空指针参数");
    WDF_RUN_TEST(test_get_truncates_long_string, "", "验证读取长字符串时按缓冲区长度截断");
    WDF_RUN_TEST(test_reload_overwrites_config, "", "验证重新加载覆盖配置");

    WDF_RUN_TEST(test_load_rejects_missing_schema_version, "", "验证加载时拒绝缺失模式版本");
    WDF_RUN_TEST(test_load_rejects_major_version_mismatch, "", "验证加载时拒绝主版本不匹配");
    WDF_RUN_TEST(test_load_rejects_higher_minor_version, "", "验证加载时拒绝更高的次版本");
    WDF_RUN_TEST(test_load_accepts_patch_suffix, "", "验证加载接受修订版本后缀");
    WDF_RUN_TEST(test_rejected_version_keeps_previous_config, "", "验证被拒绝版本保持原有配置");

    return UNITY_END();
}
