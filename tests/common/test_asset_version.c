/**
 * @file    test_asset_version.c
 * @brief   资产 schema 版本解析与兼容判定测试
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "common/asset_version.h"
#include "wdf_test_spec.h"

#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

/* -------------------------------------------------------------------------
 * 解析
 * ------------------------------------------------------------------------- */
static void test_parse_two_field(void)
{
    asset_version_t v = asset_version_parse("1.0");

    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_EQUAL_UINT16(1U, v.major);
    TEST_ASSERT_EQUAL_UINT16(0U, v.minor);
    TEST_ASSERT_EQUAL_UINT16(0U, v.patch);
}

static void test_parse_three_field(void)
{
    asset_version_t v = asset_version_parse("2.13.7");

    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_EQUAL_UINT16(2U, v.major);
    TEST_ASSERT_EQUAL_UINT16(13U, v.minor);
    TEST_ASSERT_EQUAL_UINT16(7U, v.patch);
}

/* 非法输入一律 valid=false，不做宽松兜底：资产版本是机器生成字段，
 * 宽松解析只会掩盖生成端的问题 */
static void test_parse_rejects_malformed(void)
{
    TEST_ASSERT_FALSE(asset_version_parse(NULL).valid);
    TEST_ASSERT_FALSE(asset_version_parse("").valid);
    TEST_ASSERT_FALSE(asset_version_parse("1").valid);        /* 缺次版本 */
    TEST_ASSERT_FALSE(asset_version_parse("1.").valid);       /* 次版本空 */
    TEST_ASSERT_FALSE(asset_version_parse(".1").valid);       /* 主版本空 */
    TEST_ASSERT_FALSE(asset_version_parse("1.0.").valid);     /* 修订号空 */
    TEST_ASSERT_FALSE(asset_version_parse("1.0.1.2").valid);  /* 字段过多 */
    TEST_ASSERT_FALSE(asset_version_parse("v1.0").valid);     /* 前缀 */
    TEST_ASSERT_FALSE(asset_version_parse("1.0-beta").valid); /* 后缀 */
    TEST_ASSERT_FALSE(asset_version_parse(" 1.0").valid);     /* 前导空白 */
    TEST_ASSERT_FALSE(asset_version_parse("1.0 ").valid);     /* 尾随空白 */
    TEST_ASSERT_FALSE(asset_version_parse("1.x").valid);      /* 非数字 */
}

/* 字段溢出：uint16 上限 65535 */
static void test_parse_rejects_overflow(void)
{
    TEST_ASSERT_TRUE(asset_version_parse("65535.0").valid);
    TEST_ASSERT_FALSE(asset_version_parse("65536.0").valid);
    TEST_ASSERT_FALSE(asset_version_parse("1.99999").valid);
}

/* -------------------------------------------------------------------------
 * 兼容判定
 * ------------------------------------------------------------------------- */
static void test_same_version_compatible(void)
{
    TEST_ASSERT_TRUE(asset_version_is_compatible(asset_version_parse("1.2"), asset_version_parse("1.2")));
}

/* 主版本不同一律不兼容 */
static void test_major_mismatch_incompatible(void)
{
    TEST_ASSERT_FALSE(asset_version_is_compatible(asset_version_parse("2.0"), asset_version_parse("1.0")));
    TEST_ASSERT_FALSE(asset_version_is_compatible(asset_version_parse("1.0"), asset_version_parse("2.0")));
}

/* 资产次版本较低：框架向后兼容 */
static void test_lower_minor_compatible(void)
{
    TEST_ASSERT_TRUE(asset_version_is_compatible(asset_version_parse("1.0"), asset_version_parse("1.5")));
}

/* 资产次版本更高：可能含框架不认识的字段，拒绝 */
static void test_higher_minor_incompatible(void)
{
    TEST_ASSERT_FALSE(asset_version_is_compatible(asset_version_parse("1.6"), asset_version_parse("1.5")));
}

/* 修订号不参与判定 */
static void test_patch_ignored(void)
{
    TEST_ASSERT_TRUE(asset_version_is_compatible(asset_version_parse("1.2.9"), asset_version_parse("1.2.0")));
    TEST_ASSERT_TRUE(asset_version_is_compatible(asset_version_parse("1.2.0"), asset_version_parse("1.2.9")));
}

/* 任一侧解析失败即不兼容 */
static void test_invalid_never_compatible(void)
{
    TEST_ASSERT_FALSE(asset_version_is_compatible(asset_version_parse("bad"), asset_version_parse("1.0")));
    TEST_ASSERT_FALSE(asset_version_is_compatible(asset_version_parse("1.0"), asset_version_parse("bad")));
}

/* -------------------------------------------------------------------------
 * 一步校验：错误分类与描述
 * ------------------------------------------------------------------------- */
static void test_check_ok(void)
{
    char err[128] = "unchanged";

    TEST_ASSERT_EQUAL_INT(SW_OK, asset_version_check("cloud_model", "1.0", "1.2", err, sizeof(err)));
    /* 成功时不写错误描述 */
    TEST_ASSERT_EQUAL_STRING("unchanged", err);
}

/* 资产格式坏了 → PARAM（生成端问题） */
static void test_check_malformed_is_param_error(void)
{
    char err[128] = {0};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, asset_version_check("alarm_catalog", "v1", "1.0", err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "alarm_catalog"));
    TEST_ASSERT_NOT_NULL(strstr(err, "v1"));
}

/* 版本不匹配 → STATE（部署问题）；两类分开便于判断排查方向 */
static void test_check_incompatible_is_state_error(void)
{
    char err[128] = {0};

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, asset_version_check("fluid_topology", "2.0", "1.0", err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "fluid_topology"));
    TEST_ASSERT_NOT_NULL(strstr(err, "2.0"));
}

/* 框架自身支持版本串写错 → PARAM，且描述指向框架而非资产 */
static void test_check_bad_supported_string(void)
{
    char err[128] = {0};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, asset_version_check("x", "1.0", "oops", err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "框架支持版本串非法"));
}

/* err 为 NULL 时不得崩溃 */
static void test_check_tolerates_null_err(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_version_check("x", "1.0", "1.0", NULL, 0U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, asset_version_check("x", "9.0", "1.0", NULL, 0U));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_parse_two_field, "", "验证双字段版本号解析");
    WDF_RUN_TEST(test_parse_three_field, "", "验证三字段版本号解析");
    WDF_RUN_TEST(test_parse_rejects_malformed, "", "验证解析拒绝格式错误");
    WDF_RUN_TEST(test_parse_rejects_overflow, "", "验证解析拒绝溢出");
    WDF_RUN_TEST(test_same_version_compatible, "", "验证相同版本兼容");
    WDF_RUN_TEST(test_major_mismatch_incompatible, "", "验证主版本不一致时判定不兼容");
    WDF_RUN_TEST(test_lower_minor_compatible, "", "验证较低次版本保持兼容");
    WDF_RUN_TEST(test_higher_minor_incompatible, "", "验证较高次版本判定不兼容");
    WDF_RUN_TEST(test_patch_ignored, "", "验证修订版本被忽略");
    WDF_RUN_TEST(test_invalid_never_compatible, "", "验证无效永不兼容");
    WDF_RUN_TEST(test_check_ok, "", "验证检查成功");
    WDF_RUN_TEST(test_check_malformed_is_param_error, "", "验证检查格式错误为参数错误");
    WDF_RUN_TEST(test_check_incompatible_is_state_error, "", "验证检查不兼容为状态错误");
    WDF_RUN_TEST(test_check_bad_supported_string, "", "验证检查错误受支持字符串");
    WDF_RUN_TEST(test_check_tolerates_null_err, "", "验证检查允许空指针错误");
    return UNITY_END();
}
