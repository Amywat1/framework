/**
 * @file    machine_profile.h
 * @brief   机型作业容量与收尾策略（无具体机型名）
 */

#ifndef DOMAIN_JOB_MACHINE_PROFILE_H
#define DOMAIN_JOB_MACHINE_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/job/job_types.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  机型作业 profile
 */
typedef struct {
    uint8_t max_active_jobs;     /**< 同时在洗上限，1..JOB_ACTIVE_CAP */
    uint8_t max_queued_orders;   /**< 排队上限，1..JOB_ORDER_QUEUE_CAP */
    bool     hold_after_last_job;  /**< 最后一单结束后进入 HOLD 等场区清空 */
    bool     clear_on_job_abort;   /**< 单车工艺中止后整机清障（单车机型为真，连续多车为假）*/
    uint32_t max_order_age_ms;     /**< 入队有效期；0 表示不淘汰 */
} machine_profile_t;

/**
 * @brief  装入默认 profile（单作业、可入 8 单、最后一单后 HOLD、单车中止即清障）
 */
void machine_profile_init(void);

/**
 * @brief  注册机型 profile
 * @retval SW_OK        已接受
 * @retval SW_ERR_PARAM 空指针或容量越界
 */
sw_err_t machine_profile_register(const machine_profile_t *profile);

/**
 * @brief  读取当前 profile（未注册则返回默认值）
 */
machine_profile_t machine_profile_get(void);

/**
 * @brief  测试复位为默认 profile
 */
void machine_profile_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_JOB_MACHINE_PROFILE_H */
