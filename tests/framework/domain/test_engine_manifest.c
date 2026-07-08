/**
 * @file    test_engine_manifest.c
 * @brief   洗车方案 manifest 完整性校验单元测试
 * @author  huwangwei
 * @date    2026-07-08
 */

#include "framework/domain/wash/model/engine_program_manifest.h"
#include "unity.h"

#include <string.h>

#ifndef M8_CONFIG_PATH
#define M8_CONFIG_PATH "projects/m8/programs/m8_normal_wash_program.json"
#endif

#ifndef M8_MANIFEST_PATH
#define M8_MANIFEST_PATH "projects/m8/programs/m8_normal_wash_program.manifest.json"
#endif

void setUp(void)    {}
void tearDown(void) {}

static void test_manifest_verify_ok(void)
{
    char err[200] = {0};
    sw_err_t ret = engine_program_manifest_verify(M8_CONFIG_PATH, M8_MANIFEST_PATH,
                                                  err, sizeof(err));
    if (ret != SW_OK)
    {
        printf("  manifest 校验失败: %s\n", err);
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
}

static void test_manifest_path_derive(void)
{
    char out[128] = {0};
    TEST_ASSERT_TRUE(engine_program_manifest_path_from_json(
        "/etc/m8/m8_normal_wash_program.json", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("/etc/m8/m8_normal_wash_program.manifest.json", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_manifest_path_derive);
    RUN_TEST(test_manifest_verify_ok);
    return UNITY_END();
}
