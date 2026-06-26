/**
 * @file    scenario_engine_normal_wash.c
 * @brief   端到端场景：加载真实 m8 普通洗方案，用引擎 + sim 设备模型跑完 8 阶段
 * @author  huwangwei
 * @date    2026-06-25
 *
 * @note    使用同步虚拟时钟（无线程）：每拍先 engine_tick 再 device_model_tick，
 *          确定性强、无竞态。验证全流程跑通且各阶段关键输出/时序符合预期。
 */

#include "domain/engine/engine.h"
#include "adapters/storage/json/engine_program_json.h"
#include "adapters/hal/sim_hw/engine_io_sim.h"
#include "tests/support/m8_device_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef M8_CONFIG_PATH
#define M8_CONFIG_PATH "doc/m8_normal_wash_program.json"
#endif

#define SCN_DT_MS      50U
#define SCN_MAX_TICKS  3000U

static int out(const char *name) { return engine_io_sim_get_output(name); }

int main(int argc, char **argv)
{
    printf("=== scenario_engine_normal_wash ===\n");

    const char *path = (argc > 1) ? argv[1] : M8_CONFIG_PATH;

    engine_io_sim_register();
    engine_io_sim_reset();
    m8_device_model_init();

    engine_t *e = engine_create();
    assert(e != NULL);

    char err[200] = { 0 };
    engine_program_t *prog = engine_program_load_json_file(path, err, sizeof(err));
    if (prog == NULL)
    {
        printf("  装载失败(%s): %s\n", path, err);
    }
    assert(prog != NULL);
    assert(engine_load_program(e, prog) == SW_OK);
    assert(engine_start(e) == SW_OK);

    /* 跑通全流程，沿途采集关键事实 */
    int  max_phase   = 0;
    bool saw_foam_on = false;
    bool saw_foam_off = false;
    bool saw_side_on  = false;
    bool saw_side_off_pass3 = false;
    bool saw_dryer_on = false;
    bool saw_top_medium = false;
    bool ever_halted  = false;

    /* prepare 阶段断言：顶刷旋转 + 升降跟随使能 */
    engine_tick(e, SCN_DT_MS);
    m8_device_model_tick(SCN_DT_MS);
    assert(engine_current_phase_id(e) != NULL);
    assert(strcmp(engine_current_phase_id(e), "prepare") == 0);

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
                assert(out("TOP_BRUSH_ROT") == 1);
                assert(out("TOP_BRUSH_FOLLOW_EN") == 1);
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

    /* 全流程结果 */
    assert(!ever_halted);
    assert(engine_state(e) == ENGINE_STATE_DONE);
    printf("  全部 8 阶段跑通，引擎到达 DONE\n");

    /* 各阶段关键事实 */
    assert(saw_foam_on);
    printf("  pass1: 泡沫开启 OK\n");
    assert(saw_foam_off);
    printf("  pass1: 检测车尾后泡沫关闭 OK\n");
    assert(saw_side_on);
    printf("  pass2: 侧刷开启 OK\n");
    assert(saw_side_off_pass3);
    printf("  pass3: 过车尾后侧刷停转 OK\n");
    assert(saw_top_medium);
    printf("  pass4: 顶刷中速(value=2) OK\n");
    assert(saw_dryer_on);
    printf("  pass5/6: 吹风机开启 OK\n");

    /* 归位终态：龙门停、吹风停、升降复位、后轮锁归位 */
    assert(out("GANTRY_REV") == 0);
    assert(out("GANTRY_FWD") == 0);
    assert(out("DRYER_RUN") == 0);
    assert(out("LIFTER_UP") == 0);
    assert(engine_io_sim_get_signal("REAR_LOCK_HOME") == 1);
    printf("  homing: 终态安全（龙门停/吹风停/升降复位/后轮锁归位）OK\n");

    engine_destroy(e);
    printf("=== ALL PASSED ===\n");
    return 0;
}
