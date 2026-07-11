/**
 * @file    m8_machine_ops.h
 * @brief   M8 machine_ops_port 注册
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#ifndef M8_BINDINGS_M8_MACHINE_OPS_H
#define M8_BINDINGS_M8_MACHINE_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  向 framework 注册 M8 机型运行时操作
 */
void m8_machine_ops_register(void);

#ifdef __cplusplus
}
#endif

#endif /* M8_BINDINGS_M8_MACHINE_OPS_H */
