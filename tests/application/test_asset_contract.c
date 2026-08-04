/**
 * @file    test_asset_contract.c
 * @brief   必需资产启动期校验单元测试
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "application/asset_contract.h"
#include "cloud/cloud_model.h"
#include "domain/program_engine/engine/engine_io.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "unity.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * 测试资产
 * ------------------------------------------------------------------------- */
static const alarm_def_t s_defs[] = {
    {
     .code         = 0x00010101U,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = 0U,
     .desc         = "test alarm",
     },
};

static sw_err_t fake_getter(point_value_t *out)
{
    out->i = 0;
    return SW_OK;
}

static cloud_point_entry_t s_points[1];

static void register_cloud_model(void)
{
    cloud_model_bundle_t bundle;

    memset(&s_points[0], 0, sizeof(s_points[0]));
    s_points[0].base.id   = "counter";
    s_points[0].base.type = POINT_TYPE_INT;
    s_points[0].base.get  = fake_getter;
    s_points[0].access    = CLOUD_POINT_ACCESS_RO;
    s_points[0].semantic  = CLOUD_POINT_SEM_TELEMETRY;

    bundle = (cloud_model_bundle_t){.entries = s_points, .count = 1U};
    TEST_ASSERT_EQUAL_INT(SW_OK, cloud_model_register(&bundle));
}

static const char *const s_signals[] = {"di_estop"};
static const char *const s_axes[]    = {"gantry"};

static const engine_io_catalog_t s_catalog = {
    .signals      = s_signals,
    .signal_count = 1U,
    .axes         = s_axes,
    .axis_count   = 1U,
};

/* 两个数组皆空的目录：已注册但无法校验任何名称，应与未注册同等对待 */
static const engine_io_catalog_t s_empty_catalog = {
    .signals      = NULL,
    .signal_count = 0U,
    .axes         = NULL,
    .axis_count   = 0U,
};

static int fake_read_signal(const char *name)
{
    (void)name;
    return 0;
}

static sw_err_t fake_read_axis(const char *name, double *out_pos, double *out_speed, bool *out_valid)
{
    (void)name;
    *out_pos   = 0.0;
    *out_speed = 0.0;
    *out_valid = true;
    return SW_OK;
}

/* read_signal 与 read_axis 都必填：engine_io_register 缺任一即静默拒绝注册 */
static const engine_io_ops_t s_io_ops = {
    .read_signal = fake_read_signal,
    .read_axis   = fake_read_axis,
};

void setUp(void)
{
    /* 三处资产统一复位到"缺失"这一确定起点，消除用例间顺序耦合。
     * alarm_registry_init 本身兼作复位入口；engine_io_register(NULL) 直接 return
     * 而非解除注册，故改注册空目录（其语义等同缺失，见 test_empty_engine_io_catalog_fails）。 */
    const engine_io_backend_t empty_backend = {.ops = &s_io_ops, .catalog = &s_empty_catalog};

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    cloud_model_reset_for_test();
    engine_io_register(&empty_backend);

    /* 确认起点确实是全部缺失，而不是靠用例执行顺序碰巧成立 */
    TEST_ASSERT_EQUAL_UINT(0U, alarm_registry_catalog_count());
    TEST_ASSERT_EQUAL_UINT(0U, (unsigned)cloud_model_point_count());
}

void tearDown(void)
{
}

/* -------------------------------------------------------------------------
 * 基本语义
 * ------------------------------------------------------------------------- */

/* 未声明任何必需资产时直接通过，不因资产全空而失败 */
static void test_empty_requirement_passes(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(0U));
}

/* 核心场景：声明了报警目录但未加载 —— 这正是原先全仓零处检查的缺口。
 * 目录为空时 trigger 解析不到定义而静默失败，设备表现为"从不报警"。 */
static void test_missing_alarm_catalog_fails(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, asset_contract_validate(ASSET_REQ_ALARM_CATALOG));
}

static void test_loaded_alarm_catalog_passes(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_defs, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(ASSET_REQ_ALARM_CATALOG));
}

static void test_missing_cloud_point_table_fails(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, asset_contract_validate(ASSET_REQ_CLOUD_POINT_TABLE));
}

static void test_registered_cloud_point_table_passes(void)
{
    register_cloud_model();
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(ASSET_REQ_CLOUD_POINT_TABLE));
}

static void test_missing_engine_io_catalog_fails(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, asset_contract_validate(ASSET_REQ_ENGINE_IO_CATALOG));
}

static void test_registered_engine_io_catalog_passes(void)
{
    const engine_io_backend_t backend = {.ops = &s_io_ops, .catalog = &s_catalog};

    engine_io_register(&backend);
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(ASSET_REQ_ENGINE_IO_CATALOG));
}

/* 目录已注册但 signal 与 axis 皆为空：方案加载期无法校验任何名称，
 * 非法名称会一路通过到求值期，故按缺失处理 */
static void test_empty_engine_io_catalog_fails(void)
{
    const engine_io_backend_t backend = {.ops = &s_io_ops, .catalog = &s_empty_catalog};

    engine_io_register(&backend);
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, asset_contract_validate(ASSET_REQ_ENGINE_IO_CATALOG));
}

/* -------------------------------------------------------------------------
 * 组合与边界
 * ------------------------------------------------------------------------- */

/* 多资产组合：只要有一项缺失即整体失败 */
static void test_partial_assets_fail(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_defs, 1U));

    /* 物模型未注册 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT,
                          asset_contract_validate(ASSET_REQ_ALARM_CATALOG | ASSET_REQ_CLOUD_POINT_TABLE));

    register_cloud_model();
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(ASSET_REQ_ALARM_CATALOG | ASSET_REQ_CLOUD_POINT_TABLE));
}

/* 未声明的资产不参与校验：不接云的项目不应被要求提供物模型 */
static void test_undeclared_asset_not_checked(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_defs, 1U));

    /* 只声明报警目录，物模型与 IO 目录均缺失也应通过 */
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(ASSET_REQ_ALARM_CATALOG));
}

/* 校验读的是实时状态而非缓存：复位后重新校验应失败 */
static void test_reset_makes_validation_fail(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_defs, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(ASSET_REQ_ALARM_CATALOG));

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init()); /* 兼作复位，清空目录 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, asset_contract_validate(ASSET_REQ_ALARM_CATALOG));
}

/* 含未知位时忽略该位，已声明的已知资产仍照常校验 */
static void test_unknown_bits_ignored(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_defs, 1U));
    TEST_ASSERT_EQUAL_INT(SW_OK, asset_contract_validate(ASSET_REQ_ALARM_CATALOG | (1U << 30)));
}

/* 名称查询用于日志可读性 */
static void test_name_lookup(void)
{
    TEST_ASSERT_EQUAL_STRING("alarm_catalog", asset_contract_name(ASSET_REQ_ALARM_CATALOG));
    TEST_ASSERT_EQUAL_STRING("cloud_point_table", asset_contract_name(ASSET_REQ_CLOUD_POINT_TABLE));
    TEST_ASSERT_EQUAL_STRING("engine_io_catalog", asset_contract_name(ASSET_REQ_ENGINE_IO_CATALOG));
    TEST_ASSERT_EQUAL_STRING("unknown", asset_contract_name((asset_requirement_t)(1U << 29)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_requirement_passes);
    RUN_TEST(test_missing_alarm_catalog_fails);
    RUN_TEST(test_loaded_alarm_catalog_passes);
    RUN_TEST(test_missing_cloud_point_table_fails);
    RUN_TEST(test_registered_cloud_point_table_passes);
    RUN_TEST(test_missing_engine_io_catalog_fails);
    RUN_TEST(test_registered_engine_io_catalog_passes);
    RUN_TEST(test_empty_engine_io_catalog_fails);
    RUN_TEST(test_partial_assets_fail);
    RUN_TEST(test_undeclared_asset_not_checked);
    RUN_TEST(test_reset_makes_validation_fail);
    RUN_TEST(test_unknown_bits_ignored);
    RUN_TEST(test_name_lookup);
    return UNITY_END();
}
