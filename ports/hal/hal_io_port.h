/**
 * @file    hal_io_port.h
 * @brief   数字 IO HAL 端口接口（DO 输出 / DI 输入 / 输入变化回调）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    IO 引脚编号由 adapters/machine/m8/m8_machine_map.h 定义，
 *          此接口仅使用 int 类型传递引脚号，domain 层不感知具体编号。
 */

#ifndef PORTS_HAL_IO_PORT_H
#define PORTS_HAL_IO_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * IO 输入变化回调类型
 * @param io_id   IO 板绝对引脚号（board_id * 100 + pin）
 * @param state   当前电平（true=高）
 * ------------------------------------------------------------------------- */
typedef void (*hal_io_input_cb_t)(int io_id, bool state);

/* -------------------------------------------------------------------------
 * IO 子板在线状态回调类型
 * @param board_id  子板 ID
 * @param offline   true=掉线，false=恢复在线
 * ------------------------------------------------------------------------- */
typedef void (*hal_io_board_status_cb_t)(int board_id, bool offline);

/* -------------------------------------------------------------------------
 * IO 操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /**
     * @brief  设置数字输出
     * @param  io_id  引脚号
     * @param  val    true=高电平，false=低电平
     */
    sw_err_t (*do_set)(int io_id, bool val);

    /**
     * @brief  读取数字输入缓存（非阻塞，读取上次轮询缓存值）
     * @param  io_id  引脚号
     * @retval 当前电平
     */
    bool (*di_read)(int io_id);

    /**
     * @brief  注册输入变化回调（IO 轮询线程在输入电平变化时调用）
     * @param  cb  回调函数
     */
    void (*register_input_cb)(hal_io_input_cb_t cb);

    /**
     * @brief  注册子板在线状态回调
     * @param  cb  回调函数
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
