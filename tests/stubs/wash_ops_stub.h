/**
 * @file    wash_ops_stub.h
 * @brief   单元测试用洗车 machine_ops 桩
 */

#ifndef TESTS_STUBS_WASH_OPS_STUB_H
#define TESTS_STUBS_WASH_OPS_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/op_mode/op_mode_types.h"
#include "domain/wash/model/wash_types.h"
#include "ports/outbound/machine/machine_ops_port.h"

void wash_ops_stub_reset(void);
int wash_ops_stub_start_count(void);
int wash_ops_stub_abort_count(void);
wash_mode_t wash_ops_stub_last_mode(void);
wash_abort_cause_t wash_ops_stub_last_abort_cause(void);

sw_err_t wash_ops_stub_start(wash_mode_t mode);
void wash_ops_stub_abort(wash_abort_cause_t cause);

/** 填充 ops 中与洗车相关的字段（其余保持调用方已设值）*/
void wash_ops_stub_bind(machine_ops_t *ops);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_STUBS_WASH_OPS_STUB_H */
