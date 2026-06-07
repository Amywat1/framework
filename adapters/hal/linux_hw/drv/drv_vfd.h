/**
 * @file    drv_vfd.h
 * @brief   变频器驱动接口（Modbus RTU + IO 数字量控制，handle 参数化）
 * @author  胡望伟
 * @date    2026-04-08
 *
 * @note    驱动层只描述 VFD 的 Modbus 读写与 IO 启停/复位方式，
 *          不包含业务机构名称；各厂家寄存器地址在 drv_vfd.c 内以宏区分。
 *          共用同一 serial_port 的实例在 drv 内自动共享 Modbus 互斥锁。
 */

#ifndef DRV_VFD_H
#define DRV_VFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_types.h"
#include "common/sw_error.h"
#include "common/io_handle.h"
#include "common/vfd_types.h"
#include "modbus/modbus.h"
#include <pthread.h>
#include <stdint.h>

typedef hal_vfd_state_t drv_vfd_state_t;

/** DO 写回调（由 hal_vfd_linux 注入 hal_io_port） */
typedef sw_err_t (*drv_vfd_do_set_fn)(io_do_t pin, bool val);

typedef struct
{
    modbus_t        *mb;
    io_do_t          pin_fwd;
    io_do_t          pin_rev;
    bool             has_rev;
    io_do_t          pin_rst;
    drv_vfd_state_t  state;
    void           (*event_cb)(int event_code);
    uint16_t         comm_fail_count;
    bool             comm_ok;
    const char      *serial_port;
    int              baud;
    int              modbus_addr;
    drv_vfd_do_set_fn do_set;
    void            *bus_lock;          /* 内部：同 serial_port 实例共享 */
    bool             rst_active;        /* 内部：RST 脉冲进行中 */
    uint32_t         rst_start_ms;      /* 内部：RST 脉冲起始时刻（单调时钟 ms） */
    pthread_mutex_t  rst_mutex;         /* 内部：保护 rst_active / rst_start_ms */
    bool             mb_connected;      /* 内部：Modbus 连接已建立 */
    /* ---- monitor worker 维护的状态缓存（外部只读）---- */
    uint16_t         cached_fault_code; /* 故障码缓存，0=无故障 */
    uint16_t         cached_current;    /* 电流缓存（0.01A）*/
    bool             fault_active;      /* cached_fault_code != 0 */
    uint32_t         slow_poll_ms;      /* 慢速轮询累计时间（内部计数）*/
} drv_vfd_t;

/**
 * @brief  初始化 VFD 实例（创建 Modbus 上下文，设置 IO 引脚至安全状态）
 * @param  do_set  DO 写回调（不可为 NULL）
 * @note   modbus_connect 失败时不中止 init，后续读写时会自动重试连接
 */
sw_err_t drv_vfd_init(drv_vfd_t           *vfd,
                      const char          *serial_port,
                      int                  baud,
                      int                  modbus_addr,
                      io_do_t              pin_fwd,
                      bool                 has_rev,
                      io_do_t              pin_rev,
                      io_do_t              pin_rst,
                      drv_vfd_do_set_fn    do_set);

sw_err_t drv_vfd_run_fwd(drv_vfd_t *vfd, uint16_t freq_hz);
sw_err_t drv_vfd_run_rev(drv_vfd_t *vfd, uint16_t freq_hz);
sw_err_t drv_vfd_stop(drv_vfd_t *vfd);
/**
 * @brief  启动故障复位脉冲（非阻塞，脉冲宽度见 VFD_FAULT_RESET_PULSE_MS）
 * @note   drv 内部常驻 worker 轮询各实例 rst_start_ms，到期后拉低 RST；
 *         不要求高精度；勿多线程并发调用本接口。
 */
sw_err_t drv_vfd_fault_reset(drv_vfd_t *vfd);
drv_vfd_state_t drv_vfd_get_state(const drv_vfd_t *vfd);
sw_err_t drv_vfd_get_fault_code(drv_vfd_t *vfd, uint16_t *p_code);
sw_err_t drv_vfd_read_current(drv_vfd_t *vfd, uint16_t *p_current);
sw_err_t drv_vfd_read_status(drv_vfd_t *vfd, uint16_t *p_status);
void drv_vfd_register_event_cb(drv_vfd_t *vfd, void (*cb)(int event_code));

/**
 * @brief  读取故障码缓存（无 Modbus IO，由 monitor worker 定期更新）
 * @retval 缓存的故障码，0=无故障；vfd 为 NULL 时返回 0
 */
uint16_t drv_vfd_get_cached_fault_code(const drv_vfd_t *vfd);

/**
 * @brief  读取电流缓存（无 Modbus IO，由 monitor worker 定期更新）
 * @retval 缓存的电流值（0.01A）；vfd 为 NULL 时返回 0
 */
uint16_t drv_vfd_get_cached_current(const drv_vfd_t *vfd);

#ifdef __cplusplus
}
#endif

#endif /* DRV_VFD_H */
