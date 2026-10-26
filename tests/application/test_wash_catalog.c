/**
 * @file    test_wash_catalog.c
 * @brief   套餐目录：精确查找与登记校验
 */

#include "application/wash_catalog.h"
#include "common/sw_error.h"
#include "wdf_test_spec.h"

static const wash_catalog_entry_t s_rows[] = {
    { (wash_mode_t)0U, "/p/a.json", 1000U },
    { (wash_mode_t)2U, "/p/b.json", 2000U },
};

void setUp(void)
{
    wash_catalog_reset_for_test();
}

void tearDown(void)
{
}

static void test_published_modes_resolve(void)
{
    wash_catalog_entry_t a;
    wash_catalog_entry_t b;

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_catalog_bind(s_rows, 2U));
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_catalog_bind(s_rows, 2U));
    TEST_ASSERT_TRUE(wash_catalog_find((wash_mode_t)0U, &a));
    TEST_ASSERT_TRUE(wash_catalog_find((wash_mode_t)2U, &b));
    TEST_ASSERT_EQUAL_STRING("/p/a.json", a.program_path);
    TEST_ASSERT_EQUAL_STRING("/p/b.json", b.program_path);
    TEST_ASSERT_EQUAL_UINT32(1000U, a.timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(2000U, b.timeout_ms);
}

static void test_unknown_or_unbound_rejected(void)
{
    TEST_ASSERT_FALSE(wash_catalog_find((wash_mode_t)0U, NULL));
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_catalog_bind(s_rows, 2U));
    TEST_ASSERT_FALSE(wash_catalog_find((wash_mode_t)1U, NULL));
}

static void test_bind_rejects_bad_rows(void)
{
    static const wash_catalog_entry_t empty_path[] = {
        { (wash_mode_t)0U, "", 1000U },
    };
    static const wash_catalog_entry_t zero_timeout[] = {
        { (wash_mode_t)0U, "/p/a.json", 0U },
    };
    static const wash_catalog_entry_t dup_mode[] = {
        { (wash_mode_t)0U, "/p/a.json", 1000U },
        { (wash_mode_t)0U, "/p/b.json", 2000U },
    };
    static const wash_catalog_entry_t invalid_mode[] = {
        { WASH_MODE_INVALID, "/p/a.json", 1000U },
    };
    wash_catalog_entry_t overflow[WASH_CATALOG_MAX + 1U];
    unsigned             i;

    for (i = 0U; i < (WASH_CATALOG_MAX + 1U); i++) {
        overflow[i].mode         = (wash_mode_t)i;
        overflow[i].program_path = "/p/x.json";
        overflow[i].timeout_ms   = 1000U;
    }

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, wash_catalog_bind(NULL, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, wash_catalog_bind(s_rows, 0U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, wash_catalog_bind(empty_path, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, wash_catalog_bind(zero_timeout, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, wash_catalog_bind(dup_mode, 2U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, wash_catalog_bind(invalid_mode, 1U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW,
                          wash_catalog_bind(overflow, WASH_CATALOG_MAX + 1U));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_published_modes_resolve, "CAT-01",
                 "已登记套餐按号命中路径与超时");
    WDF_RUN_TEST(test_unknown_or_unbound_rejected, "CAT-02",
                 "未登记或未发布套餐拒查");
    WDF_RUN_TEST(test_bind_rejects_bad_rows, "CAT-03",
                 "空路径、超时 0、重复号或非法号拒绝登记");
    return UNITY_END();
}
