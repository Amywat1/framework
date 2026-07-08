/**
 * @file    test_engine_validate.c
 * @brief   洗车方案语义校验单元测试
 * @author  huwangwei
 * @date    2026-07-08
 */

#include "framework/adapters/outbound/storage/json/engine_program_json.h"
#include "framework/adapters/outbound/hal/sim/engine_io_sim.h"
#include "framework/domain/wash/model/engine_model.h"
#include "framework/domain/wash/model/engine_program_validate.h"
#include "framework/domain/wash/engine/engine_expr.h"
#include "unity.h"

#include <string.h>

#ifndef M8_CONFIG_PATH
#define M8_CONFIG_PATH "projects/m8/programs/m8_normal_wash_program.json"
#endif

void setUp(void)
{
    engine_io_sim_register();
}

void tearDown(void) {}

static void test_m8_program_validate_ok(void)
{
    char err[200] = {0};
    engine_program_t *p = engine_program_load_json_file(M8_CONFIG_PATH, err, sizeof(err));
    if (p == NULL)
    {
        printf("  加载失败: %s\n", err);
    }
    TEST_ASSERT_NOT_NULL(p);
    engine_program_free(p);
}

static void test_unknown_after_rejected(void)
{
    static const char *json =
        "{"
        "  \"program\": {"
        "    \"schema_version\": \"1.0\","
        "    \"id\": \"bad\","
        "    \"name\": \"bad\","
        "    \"interlocks\": [{"
        "      \"id\": \"estop\","
        "      \"condition\": \"ESTOP == 1\","
        "      \"action\": \"halt_all\","
        "      \"priority\": 0,"
        "      \"reset_condition\": \"ESTOP == 0\","
        "      \"auto_reset\": false"
        "    }],"
        "    \"phases\": [{"
        "      \"id\": \"p1\","
        "      \"name\": \"p1\","
        "      \"entry_guard\": \"true\","
        "      \"exit_guard\": \"true\","
        "      \"timeout_ms\": 1000,"
        "      \"lanes\": [{"
        "        \"id\": \"l1\","
        "        \"steps\": [{"
        "          \"id\": \"s1\","
        "          \"type\": \"event\","
        "          \"trigger\": {\"type\": \"condition\", \"expr\": \"true\"},"
        "          \"actions\": [],"
        "          \"done\": {\"type\": \"timeout\", \"timeout_ms\": 1},"
        "          \"after\": [\"missing_step\"]"
        "        }]"
        "      }]"
        "    }]"
        "  }"
        "}";

    char err[200] = {0};
    engine_program_t *p = engine_program_load_json_string(json, err, sizeof(err));
    TEST_ASSERT_NULL(p);
    TEST_ASSERT_NOT_EQUAL(0, (int)strlen(err));
}

static void test_program_clone_roundtrip(void)
{
    char err[200] = {0};
    engine_program_t *src = engine_program_load_json_file(M8_CONFIG_PATH, err, sizeof(err));
    TEST_ASSERT_NOT_NULL(src);

    engine_program_t *copy = engine_program_clone(src);
    TEST_ASSERT_NOT_NULL(copy);

    TEST_ASSERT_EQUAL_STRING(src->id, copy->id);
    TEST_ASSERT_EQUAL_UINT(src->phase_count, copy->phase_count);
    TEST_ASSERT_EQUAL_UINT(src->interlock_count, copy->interlock_count);

    if ((src->phase_count > 0U) && (src->phases[0].entry_guard != NULL) &&
        (copy->phases[0].entry_guard != NULL))
    {
        bool src_ok = false;
        bool copy_ok = false;
        double v_src  = engine_expr_eval(src->phases[0].entry_guard, NULL, &src_ok);
        double v_copy = engine_expr_eval(copy->phases[0].entry_guard, NULL, &copy_ok);
        TEST_ASSERT_TRUE(src_ok);
        TEST_ASSERT_TRUE(copy_ok);
        TEST_ASSERT_EQUAL_DOUBLE(v_src, v_copy);
        TEST_ASSERT_NOT_EQUAL((uintptr_t)src->phases[0].entry_guard,
                              (uintptr_t)copy->phases[0].entry_guard);
    }

    engine_program_free(copy);
    engine_program_free(src);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_m8_program_validate_ok);
    RUN_TEST(test_unknown_after_rejected);
    RUN_TEST(test_program_clone_roundtrip);
    return UNITY_END();
}
