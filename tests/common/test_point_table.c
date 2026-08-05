/**
 * @file    test_point_table.c
 * @brief   point_table JSON 序列化/反序列化单元测试
 */

#include "adapters/outbound/serialization/json/point_table_json.h"
#include "common/sw_error.h"
#include "wdf_test_spec.h"

#include <string.h>

static int32_t s_speed;
static bool    s_enabled;
static char    s_name[POINT_STR_MAX];

static sw_err_t get_speed(point_value_t *out)
{
    out->i = s_speed;
    return SW_OK;
}

static sw_err_t set_speed(const point_value_t *in)
{
    s_speed = in->i;
    return SW_OK;
}

static sw_err_t get_enabled(point_value_t *out)
{
    out->b = s_enabled;
    return SW_OK;
}

static sw_err_t set_enabled(const point_value_t *in)
{
    s_enabled = in->b;
    return SW_OK;
}

static sw_err_t get_name(point_value_t *out)
{
    strncpy(out->s, s_name, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

static sw_err_t set_name(const point_value_t *in)
{
    strncpy(s_name, in->s, sizeof(s_name) - 1U);
    s_name[sizeof(s_name) - 1U] = '\0';
    return SW_OK;
}

static sw_err_t get_readonly(point_value_t *out)
{
    out->i = 99;
    return SW_OK;
}

static const point_table_entry_t k_table[] = {
    {"speed",   POINT_TYPE_INT,    get_speed,    set_speed  },
    {"enabled", POINT_TYPE_BOOL,   get_enabled,  set_enabled},
    {"name",    POINT_TYPE_STRING, get_name,     set_name   },
    {"ro",      POINT_TYPE_INT,    get_readonly, NULL       },
};

void setUp(void)
{
    s_speed   = 10;
    s_enabled = true;
    strncpy(s_name, "demo", sizeof(s_name) - 1U);
}

void tearDown(void)
{
}

static void test_to_json_serializes_readable_points(void)
{
    char     buf[128];
    sw_err_t ret;

    ret = point_table_to_json(k_table, sizeof(k_table) / sizeof(k_table[0]), buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"speed\":10"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"enabled\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"demo\""));
}

static void test_apply_json_updates_values(void)
{
    point_apply_result_t result;
    sw_err_t             ret;

    ret = point_table_apply_json(
        k_table, sizeof(k_table) / sizeof(k_table[0]), "{\"speed\":88,\"enabled\":false,\"name\":\"unit\"}", &result);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(3U, result.applied);
    TEST_ASSERT_EQUAL_INT32(88, s_speed);
    TEST_ASSERT_FALSE(s_enabled);
    TEST_ASSERT_EQUAL_STRING("unit", s_name);
}

static void test_apply_json_rejects_unknown_key(void)
{
    point_apply_result_t result;
    sw_err_t             ret;

    ret = point_table_apply_json(k_table, sizeof(k_table) / sizeof(k_table[0]), "{\"unknown\":1}", &result);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ret);
    TEST_ASSERT_EQUAL_UINT32(1U, result.rejected);
}

static void test_apply_json_readonly_rejected(void)
{
    point_apply_result_t result;
    sw_err_t             ret;

    ret = point_table_apply_json(k_table, sizeof(k_table) / sizeof(k_table[0]), "{\"ro\":1}", &result);
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, ret);
    TEST_ASSERT_EQUAL_UINT32(1U, result.rejected);
}

static void test_roundtrip_via_json(void)
{
    char buf[128];

    TEST_ASSERT_EQUAL_INT(SW_OK, point_table_to_json(k_table, sizeof(k_table) / sizeof(k_table[0]), buf, sizeof(buf)));
    s_speed   = 0;
    s_enabled = false;
    s_name[0] = '\0';
    TEST_ASSERT_EQUAL_INT(SW_OK, point_table_apply_json(k_table, sizeof(k_table) / sizeof(k_table[0]), buf, NULL));
    TEST_ASSERT_EQUAL_INT32(10, s_speed);
    TEST_ASSERT_TRUE(s_enabled);
    TEST_ASSERT_EQUAL_STRING("demo", s_name);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_to_json_serializes_readable_points, "", "验证仅将可读点位序列化为 JSON");
    WDF_RUN_TEST(test_apply_json_updates_values, "", "验证应用JSON更新值");
    WDF_RUN_TEST(test_apply_json_rejects_unknown_key, "", "验证应用JSON拒绝未知键");
    WDF_RUN_TEST(test_apply_json_readonly_rejected, "", "验证应用JSON只读被拒绝");
    WDF_RUN_TEST(test_roundtrip_via_json, "", "验证往返编解码通过JSON");
    return UNITY_END();
}
