/**
 * @file    m8_safety_deferred.c
 * @brief   M8 急停延后完备停机强符号实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    event_dispatch 线程：领域状态收敛 + 全量安全输出 flush。
 */

#include "framework/application/safety_deferred_stop.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/water.h"
#include "framework/runtime/bootstrap/project_hooks.h"

void safety_deferred_stop(void)
{
    (void)gantry_stop();
    (void)brush_stop_all();
    (void)water_all_off();
    project_assert_safe_outputs();
}
