/**
 * @file    motor_executor_port.c
 * @brief   电机执行器端口封装与小工具（可选方法 NULL 安全）
 * @author  huwangwei
 * @date    2026-08-21
 */

#include "domain/mechanism/motor/motor_executor_internal.h"

#include <limits.h>

/* ------------------------- 小工具 ------------------------- */

/** @brief 64 位绝对值。 */
int64_t motor_iabs64(int64_t v)
{
    return v < 0 ? -v : v;
}

bool within_distance(int64_t left, int64_t right, int distance)
{
    if (left >= right) {
        return (right > INT64_MAX - distance) || (left <= right + distance);
    }
    return (right < INT64_MIN + distance) || (left >= right - distance);
}

int64_t lower_bound(int64_t value, int margin)
{
    return (value < INT64_MIN + margin) ? INT64_MIN : value - margin;
}

int64_t upper_bound(int64_t value, int margin)
{
    return (value > INT64_MAX - margin) ? INT64_MAX : value + margin;
}

/* ------------------------- 端口封装（可选方法 NULL 安全） ------------------------- */

motor_driver_t *motor_drv(motor_executor_t *e, int i)
{
    return e->ports.drivers[e->cfg.motors[i].driver_index];
}

sw_err_t drv_set_output(motor_driver_t *d, motor_speed_t speed, motor_dir_t dir)
{
    return d->set_output(d->ctx, speed, dir);
}

sw_err_t drv_cutoff(motor_driver_t *d)
{
    return d->cutoff(d->ctx);
}

sw_err_t drv_request_stop(motor_driver_t *d)
{
    if ((d == NULL) || (d->request_stop == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return d->request_stop(d->ctx);
}

bool drv_reset(motor_driver_t *d)
{
    return d->reset(d->ctx);
}

motor_prepare_result_t drv_prepare(motor_driver_t *d, int motor)
{
    return d->prepare ? d->prepare(d->ctx, motor) : MOTOR_PREPARE_READY;
}

motor_prepare_result_t drv_poll(motor_driver_t *d, int motor)
{
    return d->poll ? d->poll(d->ctx, motor) : MOTOR_PREPARE_READY;
}

bool drv_is_running(motor_driver_t *d)
{
    return d->is_running(d->ctx);
}

int drv_current(motor_driver_t *d)
{
    return d->current(d->ctx);
}

bool drv_temperature(motor_driver_t *d, int *out)
{
    return d->temperature ? d->temperature(d->ctx, out) : false;
}

bool drv_voltage(motor_driver_t *d, int *out)
{
    return d->voltage ? d->voltage(d->ctx, out) : false;
}

motor_port_status_t drv_status(motor_driver_t *d)
{
    return d->status ? d->status(d->ctx) : MOTOR_PORT_OK;
}

int64_t enc_raw(motor_encoder_t *e)
{
    return e->raw(e->ctx);
}

bool enc_zero(motor_encoder_t *e)
{
    return e->zero(e->ctx);
}

motor_encoder_t *motor_enc(motor_executor_t *e, int i)
{
    return e->ports.encoders[i];
}

bool sensor_limit(motor_executor_t *e, int i, motor_limit_kind_t k)
{
    return e->ports.sensors->limit(e->ports.sensors->ctx, i, k);
}

uint64_t clock_now(motor_executor_t *e)
{
    return e->ports.clock->now_ms(e->ports.clock->ctx);
}

/* ------------------------- 结果构造 ------------------------- */

motor_cmd_result_t cmd_make(motor_cmd_status_t st, const char *reason)
{
    motor_cmd_result_t r;
    r.status = st;
    r.reject = MOTOR_REJECT_NONE;
    r.reason = reason;
    return r;
}

motor_cmd_result_t cmd_reject(motor_cmd_reject_t reject, const char *reason)
{
    motor_cmd_result_t r = cmd_make(MOTOR_CMD_REJECTED, reason);
    r.reject = reject;
    return r;
}
