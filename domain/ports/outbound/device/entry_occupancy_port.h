/**
 * @file    entry_occupancy_port.h
 * @brief   入场占用查询口（项目填物理相位，框架不跑状态机）
 *
 * @note    get 须非阻塞、可并发。未注册时 entry_occupancy_get() 返回
 *          available=false、phase=EMPTY，空场忘掉不得据此触发。
 */

#ifndef DOMAIN_PORTS_OUTBOUND_DEVICE_ENTRY_OCCUPANCY_PORT_H
#define DOMAIN_PORTS_OUTBOUND_DEVICE_ENTRY_OCCUPANCY_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/job/entry_occupancy.h"

/**
 * @brief  入场占用操作表
 */
typedef struct {
    /**
     * @brief  读取当前入场占用
     * @return 物理相位；available 由 helper 在注册后置真
     * @note   须报物理事实，不得把放行联锁或运营态 DISABLED 写进相位。
     */
    entry_occupancy_t (*get)(void);
} entry_occupancy_ops_t;

/**
 * @brief  注册入场占用口
 * @param  ops  操作表；NULL 解除注册
 * @retval SW_OK        已接受或已解除
 * @retval SW_ERR_PARAM ops 非空但 get 为空，原注册不变
 */
sw_err_t entry_occupancy_register(const entry_occupancy_ops_t *ops);

/**
 * @brief  已注册的操作表
 * @return 操作表；未注册为 NULL
 */
const entry_occupancy_ops_t *entry_occupancy_get_ops(void);

/**
 * @brief  读取入场占用（未注册则 available=false）
 */
entry_occupancy_t entry_occupancy_get(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_DEVICE_ENTRY_OCCUPANCY_PORT_H */
