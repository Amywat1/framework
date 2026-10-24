/**
 * @file    job_admit.h
 * @brief   作业放行核：队首 + 占用 + 整机态 + 项目准入
 *
 * @note    订单簿与占用表的写入只经本模块，统一在放行锁下进行；
 *          start_job 也在锁内调用，保证放行与下单/取消/全停/清障互斥。
 */

#ifndef APPLICATION_JOB_ADMIT_H
#define APPLICATION_JOB_ADMIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/job/job_types.h"
#include "domain/op_mode/op_mode_types.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化放行核（订单簿与占用表）
 */
sw_err_t job_admit_init(void);

/**
 * @brief  入簿一笔订单，不放行
 * @param  order  订单；order_id 为 0 时由订单簿分配
 * @retval SW_OK           已入簿，或同一订单号已在簿/在洗（幂等）
 * @retval SW_ERR_PARAM    order 为空
 * @retval SW_ERR_OVERFLOW 簿满
 * @note   开洗只走 job_admit_start()。
 */
sw_err_t job_admit_submit(const job_desc_t *order);

/**
 * @brief  请求开洗队首一单
 * @retval SW_OK           已放行
 * @retval SW_ERR_STATE    当时不能开洗：空簿、未准入、占用满、HOLD/停机或有阻断。已入队单保留
 * @retval SW_ERR_NOT_INIT 设备口未注册
 * @note   一次只放队首。洗着能否再放，由占用上限和 is_admit_ready 决定。
 */
sw_err_t job_admit_start(void);

/**
 * @brief  取消未放行订单
 * @retval SW_OK        已删除
 * @retval SW_ERR_PARAM 订单号为 0
 * @retval SW_ERR_STATE 簿中无此单（含已放行）
 */
sw_err_t job_admit_cancel(uint64_t order_id);

/**
 * @brief  撤掉全部在洗占用，可选清空订单簿（STOP_ALL / CLEAR_MACHINE 前置）
 * @param  clear_orders  true 时同时清空订单簿
 * @return 被撤掉的在洗作业数；调用方据此决定是否 abort_jobs
 * @note   与进行中的放行互斥：返回时不会再有作业在本次调用之后被启动并漏记。
 */
unsigned job_admit_drop_jobs(bool clear_orders);

/**
 * @brief  清空订单簿，不动在洗作业（关总开关且 profile 要求清簿）
 */
void job_admit_clear_orders(void);

/**
 * @brief  一单正常结束：按框架 job_id 撤占用；占用归零则收口整机态，不接下单
 * @param  job_id  结束的作业号；0 表示撤掉全部在洗作业
 */
void job_admit_on_completed(uint64_t job_id);

/**
 * @brief  单车中止：撤该 job_id；profile.clear_on_job_abort 为真时请求整机清障
 * @param  job_id  中止的作业号；已不在占用表中（被安全路径撤掉）时忽略
 */
void job_admit_on_job_aborted(uint64_t job_id);

/**
 * @brief  整机级中止：清空在洗占用并按原因请求清障或停机（仅安全/全停路径）
 */
void job_admit_on_aborted(wash_abort_cause_t cause);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_JOB_ADMIT_H */
