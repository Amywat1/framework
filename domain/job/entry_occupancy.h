/**
 * @file    entry_occupancy.h
 * @brief   入场占用三相位（物理就位，不含放行联锁）
 *
 * @note    EMPTY / OCCUPIED / READY 描述放行发生的那个物理位置。
 *          READY 不是 is_admit_ready：后者还可含输送节距、套餐已 Load、门已关等。
 *          available 为假表示项目未注册占用口，相位填 EMPTY 但不表示场区真空。
 */

#ifndef DOMAIN_JOB_ENTRY_OCCUPANCY_H
#define DOMAIN_JOB_ENTRY_OCCUPANCY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief  入场位物理相位
 */
typedef enum {
    ENTRY_OCC_EMPTY = 0, /**< 入场位没车 */
    ENTRY_OCC_OCCUPIED,  /**< 有车，尚未就位 */
    ENTRY_OCC_READY,     /**< 入场位已就位 */
} entry_occupancy_phase_t;

/**
 * @brief  入场占用视图
 */
typedef struct {
    bool                    available; /**< 占用口已注册 */
    entry_occupancy_phase_t phase;     /**< 物理相位；未注册时为 EMPTY */
} entry_occupancy_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_JOB_ENTRY_OCCUPANCY_H */
