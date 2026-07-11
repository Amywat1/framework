/**
 * @file    m8_safety_deferred.c
 * @brief   M8 急停延后完备停机强符号实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    event_dispatch 线程：领域状态收敛 + 全量安全输出 flush。
 */

#include "framework/application/safety_deferred_stop.h"
#include "framework/ports/outbound/machine/machine_ops_port.h"
#include "framework/runtime/bootstrap/project_hooks.h"
#include <stddef.h>

void safety_deferred_stop(void)
{
    const machine_ops_t *ops = machine_ops_get();

    if ((ops != NULL) && (ops->deferred_stop_all != NULL))
    {
        ops->deferred_stop_all();
    }
    project_assert_safe_outputs();
}
