/**
 * @file    motor_exec_provider.h
 * @brief   电机执行器出站端口 provider SPI。
 *
 * 仅执行器 provider 包含本头文件。domain 与 composition root 只包含
 * motor_exec_port.h，不感知句柄布局或 provider 函数表。
 */
#ifndef DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_EXEC_PROVIDER_H
#define DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_EXEC_PROVIDER_H

#include "domain/ports/outbound/motor/motor_exec_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief provider 实现的每实例操作表。 */
typedef struct {
    motor_cmd_result_t (
        *run)(void *ctx, int motor, motor_speed_t speed, motor_dir_t dir, const motor_move_spec_t *spec);
    motor_cmd_result_t (*stop)(void *ctx, int motor);
    motor_cmd_result_t (*home)(void *ctx, int motor);
    motor_cmd_result_t (*recover)(void *ctx, int motor, motor_exec_recovery_step_t step);
    motor_exec_phase_t (*phase)(const void *ctx, int motor);
    int64_t (*position)(const void *ctx, int motor);
    motor_dir_t (*direction)(const void *ctx, int motor);
    motor_exec_fault_code_t (*fault_code)(const void *ctx, int motor);
    bool (*encoder_healthy)(const void *ctx, int motor);
    bool (*baseline_trusted)(const void *ctx, int motor);
    bool (*pop_event)(void *ctx, motor_event_t *out);
    bool (*pop_event_for)(void *ctx, int motor, motor_event_t *out);
} motor_exec_ops_t;

/** @brief provider 句柄头；业务代码中仍是不完整类型。 */
struct motor_exec {
    const motor_exec_ops_t *ops;
    void                       *ctx;
};

/**
 * @brief 绑定 provider 拥有的句柄存储。
 * @return true 绑定成功；false 参数或操作表不完整。
 */
bool motor_exec_provider_bind(motor_exec_t *exec, const motor_exec_ops_t *ops, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_EXEC_PROVIDER_H */
