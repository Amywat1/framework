/**
 * @file    port_registry_hal.c
 * @brief   HAL 端口注册表（IO / 传感器 / 变频器 / 语音）
 */

#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/hal/hal_sensor_port.h"
#include "ports/outbound/hal/hal_vfd_port.h"
#include "ports/outbound/hal/hal_voice_port.h"

/* ---- IO ---- */
static const hal_io_ops_t *s_io_ops;
void hal_io_register(const hal_io_ops_t *ops) { s_io_ops = ops; }
const hal_io_ops_t *hal_io_get_ops(void) { return s_io_ops; }

/* ---- 传感器 ---- */
static const hal_sensor_ops_t *s_sensor_ops;
void hal_sensor_register(const hal_sensor_ops_t *ops) { s_sensor_ops = ops; }
const hal_sensor_ops_t *hal_sensor_get_ops(void) { return s_sensor_ops; }

/* ---- 变频器 ---- */
static const hal_vfd_ops_t *s_vfd_ops;
void hal_vfd_register(const hal_vfd_ops_t *ops) { s_vfd_ops = ops; }
const hal_vfd_ops_t *hal_vfd_get_ops(void) { return s_vfd_ops; }

/* ---- 语音 ---- */
static const hal_voice_ops_t *s_voice_ops;
void hal_voice_register(const hal_voice_ops_t *ops) { s_voice_ops = ops; }
const hal_voice_ops_t *hal_voice_get_ops(void) { return s_voice_ops; }
