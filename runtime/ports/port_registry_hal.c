/**
 * @file    port_registry_hal.c
 * @brief   HAL 端口注册表（IO / 传感器 / 变频器 / 语音）
 *
 * @note    注册语义见 runtime/ports/port_registry.h。
 *          HAL 各字段均由调用方逐个判空后使用（bootstrap 亦容忍 init/start 缺失），
 *          因此本层不设必填字段校验，只统一返回值与解除注册语义。
 */

#include "domain/ports/outbound/hal/hal_io_port.h"
#include "domain/ports/outbound/hal/hal_sensor_port.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"
#include "domain/ports/outbound/hal/hal_voice_port.h"
#include "runtime/ports/port_registry.h"

#include <stddef.h>

/* ---- IO ---- */
static const hal_io_ops_t *s_io_ops;

sw_err_t hal_io_register(const hal_io_ops_t *ops)
{
    s_io_ops = ops;
    return SW_OK;
}

const hal_io_ops_t *hal_io_get_ops(void)
{
    return s_io_ops;
}

/* ---- 传感器 ---- */
static const hal_sensor_ops_t *s_sensor_ops;

sw_err_t hal_sensor_register(const hal_sensor_ops_t *ops)
{
    s_sensor_ops = ops;
    return SW_OK;
}

const hal_sensor_ops_t *hal_sensor_get_ops(void)
{
    return s_sensor_ops;
}

/* ---- 变频器 ---- */
static const hal_vfd_ops_t *s_vfd_ops;

sw_err_t hal_vfd_register(const hal_vfd_ops_t *ops)
{
    s_vfd_ops = ops;
    return SW_OK;
}

const hal_vfd_ops_t *hal_vfd_get_ops(void)
{
    return s_vfd_ops;
}

/* ---- 语音 ---- */
static const hal_voice_ops_t *s_voice_ops;

sw_err_t hal_voice_register(const hal_voice_ops_t *ops)
{
    s_voice_ops = ops;
    return SW_OK;
}

const hal_voice_ops_t *hal_voice_get_ops(void)
{
    return s_voice_ops;
}

void port_registry_hal_reset(void)
{
    s_io_ops     = NULL;
    s_sensor_ops = NULL;
    s_vfd_ops    = NULL;
    s_voice_ops  = NULL;
}
