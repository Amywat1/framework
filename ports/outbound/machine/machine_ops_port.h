/**
 * @file    machine_ops_port.h
 * @brief   机型运行时操作端口（framework 应用层 → 项目 binding）
 * @author  HUWANGWEI
 * @date    2026-07-11
 *
 * @note    framework/application 不得直接调用项目命名机构 API；
 *          项目在 project_machine_setup() 阶段注册实现。
 */

#ifndef PORTS_OUTBOUND_MACHINE_MACHINE_OPS_PORT_H
#define PORTS_OUTBOUND_MACHINE_MACHINE_OPS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief 机型运行时操作集合
 */
typedef struct
{
    /** @brief 延后完备停机（wash abort / estop 延后路径） */
    void (*deferred_stop_all)(void);
    /** @brief 急停释放后安全归位 */
    void (*safety_home)(void);
    /** @brief CMD_HOME_DEVICE 副作用 */
    sw_err_t (*home_device)(void);
} machine_ops_t;

/**
 * @brief  注册机型运行时操作
 * @param  ops  操作表；须非 NULL
 */
void machine_ops_register(const machine_ops_t *ops);

/**
 * @brief  获取已注册的机型操作表
 * @return 操作表指针；未注册时返回 NULL
 */
const machine_ops_t *machine_ops_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_OUTBOUND_MACHINE_MACHINE_OPS_PORT_H */
