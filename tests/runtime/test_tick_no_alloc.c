/**
 * @file    test_tick_no_alloc.c
 * @brief   tick 路径零动态分配验证（行为契约 ARCH-15）
 *
 * 为何用链接期 --wrap 截获而不是静态 grep：tick 回调会向下调用多层，grep 只能
 * 看到第一层，而分配可能发生在任意深度的被调函数里。--wrap 捕获的是实际发生的
 * 分配，与调用深度无关。
 *
 * 为何这条必须用测试而不是靠 review：违反后不表现为功能失败——tick 里
 * malloc 一次仍然能算出正确结果，只是引入了不确定的延迟与长期碎片。功能测试
 * 永远抓不到，只在真机上表现为偶发的周期抖动。
 *
 * free 不计入违规：tick 释放别处分配的内存虽不理想，但不引入分配延迟抖动、
 * 也不加剧碎片，与"tick 内申请内存"是不同性质的问题。
 *
 * 覆盖范围只含框架自带的周期回调。domain/ 由 check_arch_boundary.sh 的 R9d
 * 全目录禁用动态内存（方案引擎三文件已登记豁免），不在此重复；项目自己注册的
 * tick 框架无从代验，属契约中的「项目自负」。
 */

#include "adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.h"
#include "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"
#include "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager_bind.h"
#include "adapters/outbound/hal/sim/hal_io_sim.h"
#include "common/io_handle.h"
#include "common/sw_error.h"
#include "domain/ports/outbound/hal/hal_io_port.h"
#include "domain/ports/outbound/hal/hal_sensor_port.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"
#include "wdf_test_spec.h"

#include <stdlib.h>
#include <string.h>

/* 测试专用入口，按本仓约定在测试侧就地声明，不进公开头文件 */
sw_err_t hal_sensor_filter_test_tick(void);
void     hal_sensor_filter_test_reset(void);
void     hal_vfd_manager_test_tick(void);
void     hal_vfd_manager_test_reset(void);

#define TEST_DI io_di_make(1U, 1U)

/* -------------------------------------------------------------------------
 * 分配计数钩子
 *
 * s_watching 为真期间的任何分配都记入 s_alloc_count。计数而非直接 abort：
 * 断言失败时能报出实际次数，比只知道"发生过"更容易定位。
 * ------------------------------------------------------------------------- */
static volatile int s_watching    = 0;
static volatile int s_alloc_count = 0;

void *__real_malloc(size_t size);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *p, size_t size);
char *__real_strdup(const char *s);

void *__wrap_malloc(size_t size)
{
    if (s_watching) {
        s_alloc_count++;
    }
    return __real_malloc(size);
}

void *__wrap_calloc(size_t n, size_t size)
{
    if (s_watching) {
        s_alloc_count++;
    }
    return __real_calloc(n, size);
}

void *__wrap_realloc(void *p, size_t size)
{
    if (s_watching) {
        s_alloc_count++;
    }
    return __real_realloc(p, size);
}

char *__wrap_strdup(const char *s)
{
    if (s_watching) {
        s_alloc_count++;
    }
    return __real_strdup(s);
}

/** @brief 开始监视分配 */
static void watch_begin(void)
{
    s_alloc_count = 0;
    s_watching    = 1;
}

/** @brief 结束监视并返回期间的分配次数 */
static int watch_end(void)
{
    s_watching = 0;
    return s_alloc_count;
}

/* -------------------------------------------------------------------------
 * VFD 后端替身：只记录调用，不分配
 * ------------------------------------------------------------------------- */
static sw_err_t mock_apply_gear(void *ctx, hal_vfd_gear_t gear)
{
    (void)ctx;
    (void)gear;
    return SW_OK;
}

static sw_err_t mock_apply_frequency(void *ctx, hal_vfd_frequency_t f)
{
    (void)ctx;
    (void)f;
    return SW_OK;
}

static sw_err_t mock_stop_outputs(void *ctx)
{
    (void)ctx;
    return SW_OK;
}

static sw_err_t mock_set_rst(void *ctx, bool level)
{
    (void)ctx;
    (void)level;
    return SW_OK;
}

static sw_err_t mock_read(void *ctx, hal_vfd_reg_t reg, uint16_t *p_val)
{
    (void)ctx;
    (void)reg;
    if (p_val != NULL) {
        *p_val = 0U;
    }
    return SW_OK;
}

static sw_err_t mock_write(void *ctx, hal_vfd_reg_t reg, uint16_t val)
{
    (void)ctx;
    (void)reg;
    (void)val;
    return SW_OK;
}

static hal_vfd_state_t mock_get_state(void *ctx)
{
    (void)ctx;
    return HAL_VFD_STATE_STOPPED;
}

static bool mock_has_rst_pin(void *ctx)
{
    (void)ctx;
    return true;
}

static const hal_vfd_backend_ops_t s_backend_ops = {
    .apply_gear      = mock_apply_gear,
    .apply_frequency = mock_apply_frequency,
    .stop_outputs    = mock_stop_outputs,
    .set_rst         = mock_set_rst,
    .read            = mock_read,
    .write           = mock_write,
    .get_state       = mock_get_state,
    .has_rst_pin     = mock_has_rst_pin,
};

static int s_drv_ctx;

void setUp(void)
{
    hal_io_sim_test_reset();
    hal_io_sim_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->init());
    hal_io_sim_set_di_level(TEST_DI, false);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_io_get_ops()->start());

    hal_sensor_filter_test_reset();
    hal_sensor_filter_register();
    hal_vfd_manager_test_reset();

    s_watching    = 0;
    s_alloc_count = 0;
}

void tearDown(void)
{
    s_watching = 0;
}

/* -------------------------------------------------------------------------
 * 用例
 * ------------------------------------------------------------------------- */

/* 钩子自身必须能抓到分配，否则后面的零断言全是假通过 */
static void test_hook_detects_allocation(void)
{
    void *p;

    watch_begin();
    p = malloc(16U);
    TEST_ASSERT_NOT_NULL(p);
    free(p);
    TEST_ASSERT_EQUAL_INT(1, watch_end());
}

/* free 不计入违规：不引入分配延迟抖动，也不加剧碎片 */
static void test_hook_ignores_free(void)
{
    void *p = malloc(16U);

    TEST_ASSERT_NOT_NULL(p);
    watch_begin();
    free(p);
    TEST_ASSERT_EQUAL_INT(0, watch_end());
}

static void test_sensor_tick_does_not_allocate(void)
{
    hal_sensor_bind_cfg_t cfg = {
        .pin           = TEST_DI,
        .active_low    = false,
        .trig_count    = 2U,
        .release_count = 3U,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_filter_bind(0U, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_sensor_get_ops()->init());

    /* 多拍并跨越一次电平翻转：滤波的状态迁移分支与稳定计数分支都要走到，
     * 只跑一拍可能恰好绕过唯一会分配的那条路径。 */
    watch_begin();
    for (unsigned i = 0U; i < 8U; i++) {
        (void)hal_sensor_filter_test_tick();
    }
    hal_io_sim_set_di_level(TEST_DI, true);
    for (unsigned i = 0U; i < 8U; i++) {
        (void)hal_sensor_filter_test_tick();
    }
    hal_io_sim_set_di_level(TEST_DI, false);
    for (unsigned i = 0U; i < 8U; i++) {
        (void)hal_sensor_filter_test_tick();
    }
    TEST_ASSERT_EQUAL_INT(0, watch_end());
}

static void test_vfd_tick_does_not_allocate(void)
{
    hal_vfd_manager_bind_cfg_t cfg = {
        .ops               = &s_backend_ops,
        .drv_ctx           = &s_drv_ctx,
        .rst_pulse_ms      = 5U,
        .fault_period_ms   = 1U,
        .current_period_ms = 1U,
        .monitor_mask      = HAL_VFD_MON_FAULT,
    };

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_manager_bind(0, &cfg));

    watch_begin();
    for (unsigned i = 0U; i < 8U; i++) {
        hal_vfd_manager_test_tick();
    }
    TEST_ASSERT_EQUAL_INT(0, watch_end());
}

/* 未绑定时的 tick 同样不得分配：错误路径也在 tick 里，不能因为提前返回就漏测 */
static void test_unbound_tick_does_not_allocate(void)
{
    watch_begin();
    (void)hal_sensor_filter_test_tick();
    hal_vfd_manager_test_tick();
    TEST_ASSERT_EQUAL_INT(0, watch_end());
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_hook_detects_allocation, "", "验证钩子检测内存分配");
    WDF_RUN_TEST(test_hook_ignores_free, "", "验证钩子忽略释放内存");
    WDF_RUN_TEST(test_sensor_tick_does_not_allocate, "", "验证传感器周期执行未分配内存");
    WDF_RUN_TEST(test_vfd_tick_does_not_allocate, "", "验证VFD周期执行未分配内存");
    WDF_RUN_TEST(test_unbound_tick_does_not_allocate, "", "验证未绑定周期执行未分配内存");
    return UNITY_END();
}
