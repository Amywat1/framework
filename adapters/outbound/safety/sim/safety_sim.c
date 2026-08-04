/**
 * @file    safety_sim.c
 * @brief   安全端口仿真实现（供 demo 与仿真目标注册）
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    仿真环境没有真实动力输出可切断，因此 cutout / deferred_stop 只记日志，
 *          用于验证安全链路是否被正确触发。真机项目须注册自己的实现，把这两条
 *          路径接到实际的 DO 写入与变频器切断上。
 */

#include "adapters/outbound/safety/sim/safety_sim.h"

#include "adapters/outbound/safety/sim/hw_estop_sim.h"
#include "common/log.h"
#include "ports/outbound/safety/safety_port.h"

#include <stdatomic.h>

static atomic_uint s_cutout_count;
static atomic_uint s_deferred_stop_count;

static sw_err_t sim_cutout(void)
{
    atomic_fetch_add(&s_cutout_count, 1U);
    LOG_WARN("safety_sim: cutout（仿真：无实际输出可切断）");
    /* 仿真没有可失败的硬件写入，恒为成功。若需验证失败分支，
     * 由测试注册自己的 ops 返回错误码，不在此处埋可配置的失败开关。 */
    return SW_OK;
}

static bool sim_estop_is_active(void)
{
    return hw_estop_sim_get_active();
}

static bool sim_alarm_is_estop(uint32_t alarm_code)
{
    /* 仿真不携带项目报警编码表，一律判定为非急停类。 */
    (void)alarm_code;
    return false;
}

static void sim_deferred_stop(void)
{
    atomic_fetch_add(&s_deferred_stop_count, 1U);
    LOG_INFO("safety_sim: deferred stop（仿真：仅记录）");
}

static const safety_ops_t s_sim_safety_ops = {
    .cutout          = sim_cutout,
    .estop_is_active = sim_estop_is_active,
    .alarm_is_estop  = sim_alarm_is_estop,
    .deferred_stop   = sim_deferred_stop,
};

sw_err_t safety_sim_register(void)
{
    return safety_port_register(&s_sim_safety_ops);
}

unsigned safety_sim_cutout_count(void)
{
    return atomic_load(&s_cutout_count);
}

unsigned safety_sim_deferred_stop_count(void)
{
    return atomic_load(&s_deferred_stop_count);
}

void safety_sim_reset_counters(void)
{
    atomic_store(&s_cutout_count, 0U);
    atomic_store(&s_deferred_stop_count, 0U);
}
