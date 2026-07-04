/**
 * @file    test_drv_io.c
 * @brief   drv_io 单元测试
 *
 * drv_io_init 完整重置全局状态，每个测试在 setUp 中重新初始化。
 * 后台线程不启动（不调用 drv_io_start），io_exp 桩函数不会被执行。
 *
 * 分组：
 *   A. init 参数边界
 *   B. 名称解析
 *   C. IO 读写与 test override
 *   D. 意外情况与边界
 */

#include "framework/adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"
#include "framework/common/io_handle.h"
#include "unity.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * 测试用名称映射表（子板 1，3 个 DI / 3 个 DO）
 * ------------------------------------------------------------------------- */
#define BOARD_ID 1U

static const drv_io_name_entry_t k_di_table[] = {
    {"DI_ESTOP",  IO_HANDLE_MAKE(IO_KIND_DI, BOARD_ID, 1)},
    {"DI_DOOR",   IO_HANDLE_MAKE(IO_KIND_DI, BOARD_ID, 2)},
    {"DI_SENSOR", IO_HANDLE_MAKE(IO_KIND_DI, BOARD_ID, 3)},
};
#define DI_COUNT (sizeof(k_di_table) / sizeof(k_di_table[0]))

static const drv_io_name_entry_t k_do_table[] = {
    {"DO_PUMP",  IO_HANDLE_MAKE(IO_KIND_DO, BOARD_ID, 1)},
    {"DO_MOTOR", IO_HANDLE_MAKE(IO_KIND_DO, BOARD_ID, 2)},
    {"DO_VALVE", IO_HANDLE_MAKE(IO_KIND_DO, BOARD_ID, 3)},
};
#define DO_COUNT (sizeof(k_do_table) / sizeof(k_do_table[0]))

static const drv_io_cfg_t k_cfg = {
    .board_count = 1,
    .pin_count   = 8,
    .di_table    = k_di_table,
    .di_count    = DI_COUNT,
    .do_table    = k_do_table,
    .do_count    = DO_COUNT,
};

void setUp(void)  { drv_io_init(&k_cfg); }
void tearDown(void) {}

/* =========================================================================
 * A. init 参数边界
 * ========================================================================= */

static void test_init_null_cfg(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(NULL));
}

static void test_init_board_count_zero(void)
{
    drv_io_cfg_t cfg = k_cfg; cfg.board_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));
}

static void test_init_board_count_max(void)
{
    drv_io_cfg_t cfg = k_cfg; cfg.board_count = 7; /* >= IO_BOARD_MAX */
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));
}

static void test_init_pin_count_zero(void)
{
    drv_io_cfg_t cfg = k_cfg; cfg.pin_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));
}

static void test_init_pin_count_too_large(void)
{
    drv_io_cfg_t cfg = k_cfg; cfg.pin_count = 33; /* > IO_PIN_COUNT_MAX */
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));
}

static void test_init_valid_params_ok(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_init(&k_cfg));
}

static void test_board_count_after_init(void)
{
    TEST_ASSERT_EQUAL_INT(1, drv_io_board_count());
}

/* NULL 名称表 + count=0 是允许的（不安装名称映射，优雅降级）*/
static void test_init_null_tables_allowed(void)
{
    drv_io_cfg_t cfg = {.board_count=1, .pin_count=8,
                        .di_table=NULL, .di_count=0,
                        .do_table=NULL, .do_count=0};
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_init(&cfg));

    /* 名称查找在空表下应失败而不崩溃 */
    io_di_t pin;
    TEST_ASSERT_FALSE(drv_io_try_parse_di("DI_ESTOP", &pin));

    io_di_t any = io_di_make(BOARD_ID, 1U);
    TEST_ASSERT_NULL(drv_io_di_name(any));
}

/* =========================================================================
 * B. 名称解析
 * ========================================================================= */

static void test_try_parse_di_full_name(void)
{
    io_di_t pin;
    TEST_ASSERT_TRUE(drv_io_try_parse_di("DI_ESTOP", &pin));
    TEST_ASSERT_EQUAL_UINT16(k_di_table[0].raw, pin.raw);
}

static void test_try_parse_di_short_name(void)
{
    io_di_t full, shrt;
    drv_io_try_parse_di("DI_DOOR", &full);
    TEST_ASSERT_TRUE(drv_io_try_parse_di("DOOR", &shrt));
    TEST_ASSERT_EQUAL_UINT16(full.raw, shrt.raw);
}

static void test_try_parse_di_unknown(void)
{
    io_di_t pin;
    TEST_ASSERT_FALSE(drv_io_try_parse_di("DI_UNKNOWN", &pin));
}

static void test_try_parse_di_null_name(void)
{
    io_di_t pin;
    TEST_ASSERT_FALSE(drv_io_try_parse_di(NULL, &pin));
}

/* out 为 NULL 时只检查是否存在，不写输出 */
static void test_try_parse_di_null_out_ok(void)
{
    TEST_ASSERT_TRUE(drv_io_try_parse_di("DI_ESTOP", NULL));
}

static void test_try_parse_do_full_name(void)
{
    io_do_t pin;
    TEST_ASSERT_TRUE(drv_io_try_parse_do("DO_PUMP", &pin));
    TEST_ASSERT_EQUAL_UINT16(k_do_table[0].raw, pin.raw);
}

static void test_try_parse_do_short_name(void)
{
    io_do_t full, shrt;
    drv_io_try_parse_do("DO_MOTOR", &full);
    TEST_ASSERT_TRUE(drv_io_try_parse_do("MOTOR", &shrt));
    TEST_ASSERT_EQUAL_UINT16(full.raw, shrt.raw);
}

static void test_try_parse_do_unknown(void)
{
    io_do_t pin;
    TEST_ASSERT_FALSE(drv_io_try_parse_do("DO_UNKNOWN", &pin));
}

static void test_di_name_known_pin(void)
{
    io_di_t pin = {k_di_table[0].raw};
    TEST_ASSERT_EQUAL_STRING("DI_ESTOP", drv_io_di_name(pin));
}

static void test_di_name_unknown_pin(void)
{
    io_di_t pin = io_di_make(BOARD_ID, 99U);
    TEST_ASSERT_NULL(drv_io_di_name(pin));
}

static void test_do_name_known_pin(void)
{
    io_do_t pin = {k_do_table[2].raw};
    TEST_ASSERT_EQUAL_STRING("DO_VALVE", drv_io_do_name(pin));
}

static void test_do_name_unknown_pin(void)
{
    io_do_t pin = io_do_make(BOARD_ID, 99U);
    TEST_ASSERT_NULL(drv_io_do_name(pin));
}

/* =========================================================================
 * C. IO 读写与 test override
 * ========================================================================= */

static void test_di_read_default_false(void)
{
    TEST_ASSERT_FALSE(drv_io_di_read(io_di_make(BOARD_ID, 1U)));
}

static void test_test_override_force_on(void)
{
    io_di_t pin = io_di_make(BOARD_ID, 1U);
    drv_io_set_test_override(pin, 1);
    TEST_ASSERT_TRUE(drv_io_di_read(pin));
}

static void test_test_override_force_off(void)
{
    io_di_t pin = io_di_make(BOARD_ID, 1U);
    drv_io_set_test_override(pin, 0);
    TEST_ASSERT_FALSE(drv_io_di_read(pin));
}

static void test_test_override_clear_restores_cache(void)
{
    io_di_t pin = io_di_make(BOARD_ID, 1U);
    drv_io_set_test_override(pin, 1);
    TEST_ASSERT_TRUE(drv_io_di_read(pin));
    drv_io_clear_test_override(pin);
    TEST_ASSERT_FALSE(drv_io_di_read(pin));
}

static void test_test_override_isolated_to_pin(void)
{
    io_di_t pin1 = io_di_make(BOARD_ID, 1U);
    io_di_t pin2 = io_di_make(BOARD_ID, 2U);
    drv_io_set_test_override(pin1, 1);
    TEST_ASSERT_TRUE(drv_io_di_read(pin1));
    TEST_ASSERT_FALSE(drv_io_di_read(pin2)); /* 未覆盖，读缓存（0） */
}

static void test_do_set_increments_request_count(void)
{
    io_do_t      pin = {k_do_table[0].raw};
    drv_io_stats_t stats;
    drv_io_do_set(pin, true);
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_get_stats(BOARD_ID, &stats));
    TEST_ASSERT_EQUAL_UINT32(1U, stats.output_request_count);
}

static void test_do_set_same_value_no_double_count(void)
{
    io_do_t      pin = {k_do_table[0].raw};
    drv_io_stats_t stats;
    drv_io_do_set(pin, true);
    drv_io_do_set(pin, true); /* 相同值，不重复计数 */
    drv_io_get_stats(BOARD_ID, &stats);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.output_request_count);
}

static void test_get_stats_invalid_board(void)
{
    drv_io_stats_t stats;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_get_stats(0, &stats));
}

/* =========================================================================
 * D. 意外情况与边界
 * ========================================================================= */

/* 把 DI 类型句柄传给 do_set → 类型校验应返回参数错误 */
static void test_do_set_di_kind_handle(void)
{
    /* 构造 kind=DI 的 raw，但以 io_do_t 形式传入 */
    io_do_t di_as_do = {IO_HANDLE_MAKE(IO_KIND_DI, BOARD_ID, 1)};
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_do_set(di_as_do, true));
}

/* 把 DO 类型句柄传给 di_read → 类型校验失败，返回 false 而不崩溃 */
static void test_di_read_do_kind_handle(void)
{
    io_di_t do_as_di = {IO_HANDLE_MAKE(IO_KIND_DO, BOARD_ID, 1)};
    TEST_ASSERT_FALSE(drv_io_di_read(do_as_di));
}

/* do_set 引脚 board_id 超出 board_count → 参数错误 */
static void test_do_set_invalid_board(void)
{
    io_do_t pin = io_do_make(9U, 1U); /* board_id=9 > board_count=1 */
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_do_set(pin, true));
}

/* di_read 引脚 board_id 超出范围 → false，不崩溃 */
static void test_di_read_invalid_board(void)
{
    io_di_t pin = io_di_make(9U, 1U);
    TEST_ASSERT_FALSE(drv_io_di_read(pin));
}

/* board_is_online：合法 board_id，但上线前应返回 false */
static void test_board_is_online_valid_but_offline(void)
{
    TEST_ASSERT_FALSE(drv_io_board_is_online(1));
}

/* board_is_online：非法 board_id（0 或超出范围）返回 false */
static void test_board_is_online_invalid_id(void)
{
    TEST_ASSERT_FALSE(drv_io_board_is_online(0));
    TEST_ASSERT_FALSE(drv_io_board_is_online(99));
}

/* get_stats：NULL 输出指针返回参数错误 */
static void test_get_stats_null_out(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_get_stats(1, NULL));
}

/* set_override 传入非 0/1 的值（如 99）应清除覆盖而非强制开 */
static void test_set_override_invalid_value_clears(void)
{
    io_di_t pin = io_di_make(BOARD_ID, 1U);
    drv_io_set_test_override(pin, 1);    /* 强制 ON */
    TEST_ASSERT_TRUE(drv_io_di_read(pin));
    drv_io_set_test_override(pin, 99);   /* 非法值 → 清除覆盖 */
    TEST_ASSERT_FALSE(drv_io_di_read(pin)); /* 恢复读缓存（0=false）*/
}

/* flush_outputs_now 在无在线子板时应返回 SW_OK，不崩溃 */
static void test_flush_outputs_now_no_crash(void)
{
    /* 无 board 在线，io_write_all_s 不会被调用 */
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_flush_outputs_now());
}

static void dummy_board_cb(int id, bool offline) { (void)id; (void)offline; }
static void dummy_panic_cb(void) {}

/* 注册 board_error_cb 和 panic_cb，不应崩溃（后台线程不运行，不会调用到） */
static void test_register_callbacks_no_crash(void)
{
    drv_io_register_board_error_cb(dummy_board_cb);
    drv_io_register_panic_cb(dummy_panic_cb);
    /* 再次注册（覆盖旧回调），不崩溃 */
    drv_io_register_board_error_cb(NULL);
    drv_io_register_panic_cb(NULL);
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    UNITY_BEGIN();

    /* A */
    RUN_TEST(test_init_null_cfg);
    RUN_TEST(test_init_board_count_zero);
    RUN_TEST(test_init_board_count_max);
    RUN_TEST(test_init_pin_count_zero);
    RUN_TEST(test_init_pin_count_too_large);
    RUN_TEST(test_init_valid_params_ok);
    RUN_TEST(test_board_count_after_init);
    RUN_TEST(test_init_null_tables_allowed);

    /* B */
    RUN_TEST(test_try_parse_di_full_name);
    RUN_TEST(test_try_parse_di_short_name);
    RUN_TEST(test_try_parse_di_unknown);
    RUN_TEST(test_try_parse_di_null_name);
    RUN_TEST(test_try_parse_di_null_out_ok);
    RUN_TEST(test_try_parse_do_full_name);
    RUN_TEST(test_try_parse_do_short_name);
    RUN_TEST(test_try_parse_do_unknown);
    RUN_TEST(test_di_name_known_pin);
    RUN_TEST(test_di_name_unknown_pin);
    RUN_TEST(test_do_name_known_pin);
    RUN_TEST(test_do_name_unknown_pin);

    /* C */
    RUN_TEST(test_di_read_default_false);
    RUN_TEST(test_test_override_force_on);
    RUN_TEST(test_test_override_force_off);
    RUN_TEST(test_test_override_clear_restores_cache);
    RUN_TEST(test_test_override_isolated_to_pin);
    RUN_TEST(test_do_set_increments_request_count);
    RUN_TEST(test_do_set_same_value_no_double_count);
    RUN_TEST(test_get_stats_invalid_board);

    /* D */
    RUN_TEST(test_do_set_di_kind_handle);
    RUN_TEST(test_di_read_do_kind_handle);
    RUN_TEST(test_do_set_invalid_board);
    RUN_TEST(test_di_read_invalid_board);
    RUN_TEST(test_board_is_online_valid_but_offline);
    RUN_TEST(test_board_is_online_invalid_id);
    RUN_TEST(test_get_stats_null_out);
    RUN_TEST(test_set_override_invalid_value_clears);
    RUN_TEST(test_flush_outputs_now_no_crash);
    RUN_TEST(test_register_callbacks_no_crash);

    return UNITY_END();
}
