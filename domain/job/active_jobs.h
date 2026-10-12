/**
 * @file    active_jobs.h
 * @brief   已放行未结束作业占用表
 */

#ifndef DOMAIN_JOB_ACTIVE_JOBS_H
#define DOMAIN_JOB_ACTIVE_JOBS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/job/job_types.h"

#include <stdint.h>

/**
 * @brief  初始化空占用表
 */
void active_jobs_init(void);

/**
 * @brief  登记一笔已放行作业
 * @retval SW_OK           已登记
 * @retval SW_ERR_PARAM    job 为空或 job_id 为 0
 * @retval SW_ERR_OVERFLOW 已达 profile 上限
 */
sw_err_t active_jobs_add(const job_desc_t *job);

/**
 * @brief  按框架作业号移除
 * @retval SW_OK        已移除
 * @retval SW_ERR_STATE 表中无此单
 */
sw_err_t active_jobs_remove(uint64_t job_id);

/**
 * @brief  清空全部在洗作业
 */
void active_jobs_clear(void);

/**
 * @brief  当前在洗数
 */
unsigned active_jobs_count(void);

/**
 * @brief  按槽位读取在洗作业
 * @retval SW_OK        已写入
 * @retval SW_ERR_PARAM 越界或 out 为空
 */
sw_err_t active_jobs_get_at(unsigned index, job_desc_t *out);

/**
 * @brief  测试复位
 */
void active_jobs_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_JOB_ACTIVE_JOBS_H */
