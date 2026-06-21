/**
 * @file    m8_io_poll.h
 * @brief   M8 IO 轮询线程注册
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#ifndef ADAPTERS_MACHINE_M8_M8_IO_POLL_H
#define ADAPTERS_MACHINE_M8_M8_IO_POLL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  注册 io_poll 线程（信号滤波 + 指示灯 tick + 报警时间片）
 */
sw_err_t m8_io_poll_register(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_M8_IO_POLL_H */
