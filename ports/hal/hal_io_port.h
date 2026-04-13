/**
 * @file    hal_io_port.h
 * @brief   数字 IO HAL 端口接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    本接口统一使用强类型 IO 句柄，不再使用 `board*100+pin` 形式的旧编号语义。
 */

#ifndef PORTS_HAL_IO_PORT_H
#define PORTS_HAL_IO_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "common/sw_error.h"
#include "common/io_handle.h"

/* -------------------------------------------------------------------------
 * 调试回调类型
 * 仅用于观察输入变化，不参与项目正式控制逻辑。
 * ------------------------------------------------------------------------- */
typedef void (*hal_io_debug_input_cb_t)(io_di_t pin, bool state);

/* -------------------------------------------------------------------------
 * IO 子板在线状态回调类型
 * ------------------------------------------------------------------------- */
typedef void (*hal_io_board_status_cb_t)(int board_id, bool offline);

/* -------------------------------------------------------------------------
 * IO 操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /**
     * @brief  设置数字输出
     * @param  pin  DO 句柄
     * @param  val  true=高电平，false=低电平
     */
    sw_err_t (*do_set)(io_do_t pin, bool val);

    /**
     * @brief  读取数字输入缓存
     * @param  pin  DI 句柄
     * @retval 当前电平
     */
    bool (*di_read)(io_di_t pin);

    /**
     * @brief  注册输入变化调试回调
     */
    void (*register_debug_input_cb)(hal_io_debug_input_cb_t cb);

    /**
     * @brief  注册子板在线状态回调
     */
    void (*register_board_status_cb)(hal_io_board_status_cb_t cb);
} hal_io_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取
 * ------------------------------------------------------------------------- */
void               hal_io_register(const hal_io_ops_t *ops);
const hal_io_ops_t *hal_io_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_IO_PORT_H */
