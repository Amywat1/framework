/**
 * @file    m8_motor_binding.h
 * @brief   M8 电机槽位与机构的编译期绑定表。
 *
 * 电机索引定义见 m8_motor_exec.h（m8_motor_id_t）。
 * 本文件提供语义别名，供装配层与领域层引用，避免魔术数字散落。
 */
#ifndef MACHINES_M8_CONFIG_M8_MOTOR_BINDING_H
#define MACHINES_M8_CONFIG_M8_MOTOR_BINDING_H

#include "projects/m8/bindings/m8_motor_exec.h"

/** @brief 龙门行走电机槽位。 */
#define M8_BIND_GANTRY     M8_MOTOR_GANTRY

/** @brief 侧刷电机槽位。 */
#define M8_BIND_BRUSH_SIDE M8_MOTOR_BRUSH_SIDE

/** @brief 顶刷电机槽位。 */
#define M8_BIND_BRUSH_TOP  M8_MOTOR_BRUSH_TOP

/** @brief 顶刷升降电机槽位。 */
#define M8_BIND_LIFT       M8_MOTOR_LIFT

/** @brief 后轮锁止电机槽位。 */
#define M8_BIND_REAR_LOCK  M8_MOTOR_REAR_LOCK

/** @brief 风机电机槽位。 */
#define M8_BIND_FAN        M8_MOTOR_FAN

#endif /* MACHINES_M8_CONFIG_M8_MOTOR_BINDING_H */
