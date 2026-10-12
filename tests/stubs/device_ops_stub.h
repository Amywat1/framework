/**
 * @file    device_ops_stub.h
 * @brief   单元测试用 device_ops 桩
 */
#ifndef TESTS_STUBS_DEVICE_OPS_STUB_H
#define TESTS_STUBS_DEVICE_OPS_STUB_H

#include "domain/job/job_types.h"
#include "domain/op_mode/op_mode_types.h"
#include "domain/ports/outbound/device/device_ops_port.h"

void               device_ops_stub_reset(void);
int                device_ops_stub_start_count(void);
int                device_ops_stub_abort_count(void);
wash_mode_t        device_ops_stub_last_mode(void);
uint64_t           device_ops_stub_last_job_id(void);
void               device_ops_stub_set_start_result(sw_err_t result);
wash_abort_cause_t device_ops_stub_last_abort_cause(void);
void               device_ops_stub_set_admit_ready(bool ready);

sw_err_t device_ops_stub_start(const job_desc_t *job);
void     device_ops_stub_abort(wash_abort_cause_t cause);
void     device_ops_stub_clear(void);

void device_ops_stub_bind(device_ops_t *ops);

#endif /* TESTS_STUBS_DEVICE_OPS_STUB_H */
