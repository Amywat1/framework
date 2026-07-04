/**
 * @file    scenario_engine_normal_wash.c
 * @brief   端到端场景：加载真实 m8 普通洗方案，用引擎 + sim 设备模型跑完 8 阶段
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    使用同步虚拟时钟（无线程）：每拍�?engine_tick �?device_model_tick�?
 *          确定性强、无竞态。验证全流程跑通且各阶段关键输�?时序符合预期�?
 */

#include "framework/domain/wash/engine/engine.h"
#include "framework/adapters/outbound/storage/json/engine_program_json.h"
#include "framework/adapters/outbound/hal/sim/engine_io_sim.h"
#include "projects/m8/tests/scenario/m8_device_model.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>

#ifndef M8_CONFIG_PATH
#define M8_CONFIG_PATH "doc/m8_normal_wash_program.json"
#endif

#define SCN_DT_MS      50U
#define SCN_MAX_TICKS  3000U

static int out(const char *name) { return engine_io_sim_get_output(name); }

void setUp(void)
{
    engine_io_sim_register();
    engine_io_sim_reset();
    m8_device_model_init();
}

void tearDown(void) {}

static void test_engine_normal_wash(void)
{
    engine_t *e = engine_create();
    TEST_ASSERT_NOT_NULL(e);

    char err[200] = { 0 };
    engine_program_t *prog = engine_program_load_json_file(M8_CONFIG_PATH, err, sizeof(err));
    if (prog == NULL) { printf("  装载失败(%s): %s\n", M8_CONFIG_PATH, err); }
    TEST_ASSERT_NOT_NULL(prog);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_load_program(e, prog));
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_start(e));

    /* 跑通全流程，沿途采集关键事�?*/
    int  max_phase            = 0;
    bool saw_foam_on          = false;
    bool saw_foam_off         = false;
    bool saw_side_on          = false;
    bool saw_side_off_pass3   = false;
    bool saw_dryer_on         = false;
    bool saw_top_medium       = false;
    bool ever_halted          = false;

    /* prepare 阶段断言：顶刷旋�?+ 升降跟随使能 */
    engine_tick(e, SCN_DT_MS);
    m8_device_model_tick(SCN_DT_MS);
    TEST_ASSERT_NOT_NULL(engine_current_phase_id(e));
    TEST_ASSERT_EQUAL_STRING("prepare", engine_current_phase_id(e));

    for (unsigned i = 0U; i < SCN_MAX_TICKS; ++i)
    {
        engine_tick(e, SCN_DT_MS);
        m8_device_model_tick(SCN_DT_MS);

        engine_run_state_t st = engine_state(e);
        if (st == ENGINE_STATE_HALTED) { ever_halted = true; break; }

        int ph = engine_current_phase(e);
        if (ph > max_phase) { max_phase = ph; }

        const char *id = engine_current_phase_id(e);
        if (id != NULL)
        {
            if (strcmp(id, "prepare") == 0)
            {
                TEST_ASSERT_EQUAL_INT(1, out("TOP_BRUSH_ROT"));
                TEST_ASSERT_EQUAL_INT(1, out("TOP_BRUSH_FOLLOW_EN"));
            }
            else if (strcmp(id, "pass1_fwd_foam") == 0)
            {
                if (out("WATER_TOP_FOAM") == 1) { saw_foam_on = true; }
                if (saw_foam_on && (out("WATER_TOP_FOAM") == 0)) { saw_foam_off = true; }
            }
            else if (strcmp(id, "pass2_rev_side") == 0)
            {
                if (out("SIDE_BRUSH_ROT") == 1) { saw_side_on = true; }
            }
            else if (strcmp(id, "pass3_fwd_side") == 0)
            {
                if (saw_side_on && (out("SIDE_BRUSH_ROT") == 0)) { saw_side_off_pass3 = true; }
            }
            else if (strcmp(id, "pass4_rev_rinse") == 0)
            {
                if (out("TOP_BRUSH_ROT") == 2) { saw_top_medium = true; }
            }
            else if ((strcmp(id, "pass5_fwd_dry") == 0) ||
                     (strcmp(id, "pass6_rev_dry") == 0))
            {
                if (out("DRYER_RUN") == 1) { saw_dryer_on = true; }
            }
        }

        if (st == ENGINE_STATE_DONE) { break; }
        if (st == ENGINE_STATE_PHASE_HALTED) { ever_halted = true; break; }
    }

    /* 全流程结�?*/
    TEST_ASSERT_FALSE_MESSAGE(ever_halted, "引擎意外进入 HALTED 状�?);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ENGINE_STATE_DONE, engine_state(e), "引擎未到�?DONE");

    /* 各阶段关键事�?*/
    TEST_ASSERT_TRUE_MESSAGE(saw_foam_on,        "pass1: 泡沫未开�?);
    TEST_ASSERT_TRUE_MESSAGE(saw_foam_off,       "pass1: 检测车尾后泡沫未关�?);
    TEST_ASSERT_TRUE_MESSAGE(saw_side_on,        "pass2: 侧刷未开�?);
    TEST_ASSERT_TRUE_MESSAGE(saw_side_off_pass3, "pass3: 过车尾后侧刷未停�?);
    TEST_ASSERT_TRUE_MESSAGE(saw_top_medium,     "pass4: 顶刷中�?value=2)未出�?);
    TEST_ASSERT_TRUE_MESSAGE(saw_dryer_on,       "pass5/6: 吹风机未开�?);

    /* 归位终�?*/
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, out("GANTRY_REV"), "homing: 龙门反转未停");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, out("GANTRY_FWD"), "homing: 龙门正转未停");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, out("DRYER_RUN"),  "homing: 吹风机未�?);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, out("LIFTER_UP"),  "homing: 升降未复�?);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, engine_io_sim_get_signal("REAR_LOCK_HOME"),
                                  "homing: 后轮锁未归位");

    engine_destroy(e);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_engine_normal_wash);
    return UNITY_END();
}
