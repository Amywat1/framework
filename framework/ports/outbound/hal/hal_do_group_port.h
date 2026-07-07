/**
 * @file    hal_do_group_port.h
 * @brief   DO 组×槽位 HAL 端口（二维逻辑绑定表，不含业务语义）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅提供 group / slot 编号与 DO 读写；业务映射由 machine 层完成。
 *          通道绑定由具体 HAL 组合层提供装配接口，port ops 不承载项目点位绑定。
 */

#ifndef PORTS_HAL_DO_GROUP_PORT_H
#define PORTS_HAL_DO_GROUP_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

/** 最大 DO 组数（group 编号 0 .. HAL_DO_GROUP_MAX-1） */
#define HAL_DO_GROUP_MAX         8U

/** 每组内最大槽位数（slot 编号 0 .. HAL_DO_SLOT_MAX-1） */
#define HAL_DO_SLOT_MAX          4U

typedef uint8_t hal_do_group_t;
typedef uint8_t hal_do_slot_t;

typedef struct
{
    /** @brief  初始化内部运行时状态 */
    sw_err_t (*init)(void);

    /** @brief  设置指定组内某一槽位的 DO 输出 */
    sw_err_t (*slot_set)(hal_do_group_t group, hal_do_slot_t slot, bool on);

    /** @brief  关闭全部已绑定 DO */
    sw_err_t (*all_off)(void);
} hal_do_group_ops_t;

void                        hal_do_group_register(const hal_do_group_ops_t *ops);
const hal_do_group_ops_t   *hal_do_group_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_DO_GROUP_PORT_H */
