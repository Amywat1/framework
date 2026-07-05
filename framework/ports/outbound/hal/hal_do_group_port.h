/**
 * @file    hal_do_group_port.h
 * @brief   DO 组×槽位 HAL 端口（二维逻辑绑定表，不含业务语义）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅提供 group / slot 编号与 DO 读写；业务映射由 machine 层完成。
 *          通道绑定通过 hal_do_group_ops_t.bind() 完成，由已注册的
 *          hal_do_group 适配器（如 hal_do_group_mapper）提供具体实现。
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
    /** @brief  初始化内部状态 */
    sw_err_t (*init)(void);

    /**
     * @brief  绑定组内槽位到 DO 引脚
     * @param  group  DO 组编号
     * @param  slot   组内槽位编号
     * @param  pin    数字输出句柄；IO_HANDLE_NULL 表示未安装
     * @retval SW_OK        绑定成功
     * @retval SW_ERR_PARAM  group/slot 越界
     * @note   供项目 wiring/bindings 在启动阶段调用。
     */
    sw_err_t (*bind)(hal_do_group_t group, hal_do_slot_t slot, io_do_t pin);

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
