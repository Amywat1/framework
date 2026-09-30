/**
 * @file    control_loop.h
 * @brief   单线程 FIFO 控制环：把控制类节拍并到同一拍上跑
 *
 * @note    电机 / 水路 / 传感器滤波 / 雷达 / 仿形 / 变频器脉冲挂在本环。
 *          日志、云、观测、Modbus 监视不得登记到本环。
 *          回调（含其调用链）不得做同步总线事务或等待 IO 线程回执，
 *          例如 drv_io_submit_job、Modbus 读写：一次阻塞会拖住同拍全部回调。
 *          需要总线操作时只登记请求，由 IO 线程执行（参见 drv_io_pulse_clear）。
 *          优先级低于急停采集线程。
 */

#ifndef RUNTIME_SCHEDULER_CONTROL_LOOP_H
#define RUNTIME_SCHEDULER_CONTROL_LOOP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "runtime/scheduler/periodic_task.h"

#include <stdint.h>

/**
 * @brief 控制环槽位数
 *
 * 电机 / 水路 / 传感器滤波 / 雷达 / 仿形 / 变频器脉冲，再留两格给项目。
 */
#define CONTROL_LOOP_SLOT_MAX 8U

/**
 * @brief  登记一个由控制环线程驱动的节拍回调
 * @param  name      任务名，仅保存指针，须为静态存储
 * @param  period_ms 周期毫秒，必须是 THD_MOTOR_TICK_PERIOD_MS 的整数倍且大于 0
 * @param  fn        回调，不能为空
 * @param  ctx       回调上下文，可为 NULL
 * @retval SW_OK 已登记
 * @retval SW_ERR_PARAM 参数非法
 * @retval SW_ERR_OVERFLOW 槽位或线程表已满
 * @note   所有控制环任务共享一条 SCHED_FIFO 线程（优先级低于急停）。
 *         日志、云、观测、Modbus 监视不得登记到本环；回调不得做同步总线事务。
 */
sw_err_t control_loop_register(const char *name, uint32_t period_ms, periodic_task_fn_t fn, void *ctx);

/**
 * @brief  已登记的控制环任务数量
 */
unsigned control_loop_count(void);

/**
 * @brief  读取指定控制环任务的运行观测
 * @param  index 已登记任务下标，范围 [0, control_loop_count())
 * @param  out   输出统计，不能为空
 * @retval SW_OK 读取成功
 * @retval SW_ERR_PARAM out 为空
 * @retval SW_ERR_NOT_FOUND index 超出范围
 */
sw_err_t control_loop_get_stats(unsigned index, periodic_task_stats_t *out);

/**
 * @brief  清空控制环表（仅供单元测试）
 * @note   生产路径不得调用：已启动的控制环线程无法回收。
 */
void control_loop_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SCHEDULER_CONTROL_LOOP_H */
