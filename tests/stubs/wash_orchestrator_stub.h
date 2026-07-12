/**
 * @file    wash_orchestrator_stub.h
 * @brief   洗车编排器链接桩（测试用计数器）
 */

#ifndef TESTS_STUBS_WASH_ORCHESTRATOR_STUB_H
#define TESTS_STUBS_WASH_ORCHESTRATOR_STUB_H

#include "domain/command_gateway/op_mode_types.h"
#include "domain/wash/model/wash_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  重置桩计数器 */
void wash_orchestrator_stub_reset(void);

/** @brief  获取 start 调用次数 */
int wash_orchestrator_stub_start_count(void);

/** @brief  获取 abort 调用次数 */
int wash_orchestrator_stub_abort_count(void);

/** @brief  获取最近一次 start 的模式 */
wash_mode_t wash_orchestrator_stub_last_mode(void);

/** @brief  获取最近一次 abort 原因 */
wash_abort_cause_t wash_orchestrator_stub_last_abort_cause(void);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_STUBS_WASH_ORCHESTRATOR_STUB_H */
