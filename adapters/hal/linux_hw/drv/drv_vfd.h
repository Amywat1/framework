/**
 * @file    drv_vfd.h
 * @brief   变频器驱动接口（Modbus RTU + IO 数字量控制，handle 参数化）
 * @author  HUWANGWEI
 * @date    2026-04-08
 *
 * @note    驱动层只描述 VFD 的 Modbus 读写与 IO 启停/复位方式，
 *          不包含业务机构名称；各厂家寄存器地址在 drv_vfd.c 内以宏区分。
 *          共用同一 serial_port 的实例在 drv 内自动共享 Modbus 互斥锁。
 *
 * 速度控制模型：
 *   - 两路速度 IO（pin_spd1/pin_spd2）组合最多 3 个有效速度挡；
 *     (spd1=0, spd2=0) 保留为停止态，不可作为速度挡。
 *   - 挡位值：正=正转，负=反转，0=停止；绝对值为速度挡（1~VFD_GEAR_MAX）。
 *   - 频率通过 Modbus 单独设置（drv_vfd_set_freq），与挡位 IO 控制相互独立。
 */

#ifndef DRV_VFD_H
#define DRV_VFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include "common/sw_error.h"
#include "common/sw_types.h"
#include "common/vfd_types.h"
#include "modbus/modbus.h"

#include <pthread.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 挡位类型与宏
 * 正值=正转，负值=反转，0=停止；绝对值为速度挡位（1=最低，VFD_GEAR_MAX=最高）
 * 两路速度 IO 可组合 3 个有效挡（(0,0) 保留为停止态）
 * ------------------------------------------------------------------------- */
typedef hal_vfd_gear_t drv_vfd_gear_t;

#define VFD_GEAR_STOP  ((drv_vfd_gear_t)0)
#define VFD_GEAR_FWD_1 ((drv_vfd_gear_t)1)
#define VFD_GEAR_FWD_2 ((drv_vfd_gear_t)2)
#define VFD_GEAR_FWD_3 ((drv_vfd_gear_t)3)
#define VFD_GEAR_REV_1 ((drv_vfd_gear_t) - 1)
#define VFD_GEAR_REV_2 ((drv_vfd_gear_t) - 2)
#define VFD_GEAR_REV_3 ((drv_vfd_gear_t) - 3)
#define VFD_GEAR_MAX   3 /* 最大有效速度挡数（两路 IO 去除停止态后的可用组合数）*/

/**
 * 速度挡位 IO 状态辅助宏：bit0=spd1 电平，bit1=spd2 电平
 * 禁止使用 VFD_SPD_IO(0,0)，该组合保留为停止态
 */
#define VFD_SPD_IO(s1, s2) ((uint8_t)(((s2) ? 0x02U : 0U) | ((s1) ? 0x01U : 0U)))

typedef hal_vfd_state_t drv_vfd_state_t;

/* -------------------------------------------------------------------------
 * monitor 读取项掩码
 * 标志位可按位组合；扩展时新增 DRV_VFD_MON_* 宏，不改接口签名
 * ------------------------------------------------------------------------- */
typedef uint8_t drv_vfd_monitor_mask_t;

#define DRV_VFD_MON_NONE    ((drv_vfd_monitor_mask_t)0x00U) /* 全部关闭 */
#define DRV_VFD_MON_FAULT   ((drv_vfd_monitor_mask_t)0x01U) /* 周期读取故障码 */
#define DRV_VFD_MON_CURRENT ((drv_vfd_monitor_mask_t)0x02U) /* 周期读取电流 */
#define DRV_VFD_MON_ALL     ((drv_vfd_monitor_mask_t)0x03U) /* 全部开启（默认）*/

/** DO 写回调类型，由 hal_vfd_linux 注入，用于驱动操作底层引脚 */
typedef sw_err_t (*drv_vfd_do_set_fn)(io_do_t pin, bool val);

/**
 * VFD 实例句柄，由调用方分配静态或全局存储，通过指针传入各接口。
 * 以下字段标注"内部"者，外部代码只读，禁止直接修改。
 */
typedef struct {
    modbus_t      *mb;
    io_do_t        pin_fwd;
    io_do_t        pin_rev;                 /* IO_HANDLE_NULL 表示不支持反转 */
    io_do_t        pin_rst;
    io_do_t        pin_spd1;                /* 速度 IO1；IO_HANDLE_NULL 表示未配置 */
    io_do_t        pin_spd2;                /* 速度 IO2；IO_HANDLE_NULL 表示未配置 */
    uint8_t        spd_cfg[VFD_GEAR_MAX];   /* 挡位 1~3 对应 IO 状态，由 drv_vfd_config_speed_io 写入 */
    bool           spd_io_ready;            /* 内部：drv_vfd_config_speed_io 已完成配置 */
    drv_vfd_gear_t gear;                    /* 内部：当前挡位，0=停止 */
    void (*event_cb)(int event_code);
    uint16_t               comm_fail_count; /* 内部：连续 Modbus 失败计数 */
    bool                   comm_ok;         /* 内部：当前通信是否正常 */
    const char            *serial_port;
    int                    baud;
    int                    modbus_addr;
    drv_vfd_do_set_fn      do_set;
    void                  *bus_lock;            /* 内部：同 serial_port 实例共享的 Modbus 互斥锁 */
    bool                   rst_active;          /* 内部：RST 脉冲进行中 */
    uint32_t               rst_start_ms;        /* 内部：RST 脉冲起始时刻（单调时钟 ms） */
    drv_vfd_gear_t         pending_gear;        /* 内部：方向切换等待中的目标挡位，GEAR_STOP 表示无待处理 */
    uint32_t               dir_change_start_ms; /* 内部：方向切换开始等待时刻（单调时钟 ms） */
    pthread_mutex_t        rst_mutex;           /* 内部：保护 RST 脉冲与方向切换状态 */
    bool                   mb_connected;        /* 内部：Modbus 连接已建立 */
    uint16_t               cached_fault_code;   /* 外部只读：故障码缓存，0=无故障 */
    uint16_t               cached_current;      /* 外部只读：电流缓存（0.01A） */
    bool                   fault_active;        /* 外部只读：cached_fault_code != 0 */
    uint32_t               last_slow_poll_ms;   /* 内部：上次慢速轮询时刻，用于绝对时间比较 */
    drv_vfd_monitor_mask_t monitor_mask;        /* 控制哪些项被周期读取；由 drv_vfd_set_monitor_mask 写 */
} drv_vfd_t;

/**
 * @brief  初始化 VFD 实例，建立 Modbus 上下文并将所有控制 DO 置为安全低态
 * @param[in]  vfd          VFD 实例指针，由调用方提供存储，不可为 NULL
 * @param[in]  serial_port  Modbus RTU 串口路径（如 "/dev/ttyS0"），不可为 NULL
 * @param[in]  baud         串口波特率
 * @param[in]  modbus_addr  Modbus 从站地址（1~247）
 * @param[in]  pin_fwd      正转控制 DO 引脚
 * @param[in]  pin_rev      反转控制 DO 引脚；IO_HANDLE_NULL 表示该 VFD 不支持反转
 * @param[in]  pin_rst      故障复位 DO 引脚
 * @param[in]  do_set       DO 写回调，不可为 NULL
 * @retval     SW_OK        初始化成功
 * @retval     SW_ERR_PARAM vfd / serial_port / do_set 为 NULL
 * @retval     SW_ERR_HW    资源申请失败（串口表已满、mutex 初始化失败、线程创建失败）
 * @note   Modbus 连接失败时不中止 init，后续读写会自动重连（defer-link）；
 *         速度 IO 须单独调用 drv_vfd_config_speed_io 配置
 */
sw_err_t drv_vfd_init(drv_vfd_t        *vfd,
                      const char       *serial_port,
                      int               baud,
                      int               modbus_addr,
                      io_do_t           pin_fwd,
                      io_do_t           pin_rev,
                      io_do_t           pin_rst,
                      drv_vfd_do_set_fn do_set);

/**
 * @brief  配置速度 IO 引脚与挡位映射，必须在首次调用 drv_vfd_run 非停止挡前完成
 * @param[in]  vfd       已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  pin_spd1  速度 IO1，不可为 IO_HANDLE_NULL
 * @param[in]  pin_spd2  速度 IO2，不可为 IO_HANDLE_NULL
 * @param[in]  spd_cfg   长度为 VFD_GEAR_MAX 的挡位映射数组；
 *                       spd_cfg[0..2] 依次对应挡位 1~3，
 *                       每元素用 VFD_SPD_IO(s1,s2) 填写，禁止使用 VFD_SPD_IO(0,0)
 * @retval     SW_OK        配置成功，速度 IO 已拉低至安全态
 * @retval     SW_ERR_PARAM 参数非法（vfd/spd_cfg 为 NULL、引脚为 IO_HANDLE_NULL、
 *                          任一挡位映射值为 0x00）
 * @retval     SW_ERR_NOT_INIT  vfd 未完成初始化
 */
sw_err_t drv_vfd_config_speed_io(drv_vfd_t    *vfd,
                                 io_do_t       pin_spd1,
                                 io_do_t       pin_spd2,
                                 const uint8_t spd_cfg[VFD_GEAR_MAX]);

/**
 * @brief  运行变频器至指定挡位（正值正转，负值反转，0 停止）
 * @param[in]  vfd   已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  gear  目标挡位；范围 [-VFD_GEAR_MAX, VFD_GEAR_MAX]，0 表示停止
 * @retval     SW_OK            操作成功
 * @retval     SW_ERR_NOT_INIT  vfd 未初始化，或非停止挡时速度 IO 尚未配置
 * @retval     SW_ERR_PARAM     gear 超出范围，或目标为反转但 VFD 不支持反转
 * @note   非停止挡要求已调用 drv_vfd_config_speed_io；
 *         方向切换（正转↔反转）时，本接口立即返回（非阻塞），内部先关断所有 IO
 *         并记录目标挡位；monitor worker 在 VFD_DIR_SWITCH_DELAY_MS 到期后自动执行；
 *         方向切换等待期间：发送停止指令立即生效并取消等待；
 *         发送同向指令仅更新目标挡位，不重置等待计时；
 *         IO 操作顺序：速度 IO 先于方向 IO，避免换挡瞬间出现错误组合；
 *         本接口不控制 Modbus 频率，频率须单独调用 drv_vfd_set_freq
 */
sw_err_t drv_vfd_run(drv_vfd_t *vfd, drv_vfd_gear_t gear);

/**
 * @brief  通过 Modbus 设置变频器目标频率，与挡位 IO 控制完全独立
 * @param[in]  vfd      已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  freq_hz  目标频率（Hz）
 * @retval     SW_OK           设置成功
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 * @retval     SW_ERR_COMM     Modbus 写操作失败
 */
sw_err_t drv_vfd_set_freq(drv_vfd_t *vfd, uint16_t freq_hz);

/**
 * @brief  启动故障复位脉冲（非阻塞），复位前先确保所有运行输出关断
 * @param[in]  vfd  已完成 drv_vfd_init 的 VFD 实例
 * @retval     SW_OK           复位脉冲已启动
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 * @note   脉冲宽度由 VFD_FAULT_RESET_PULSE_MS 控制，由内部 monitor worker 定时拉低；
 *         本接口立即返回，不等待脉冲结束；
 *         禁止多线程并发调用
 */
sw_err_t drv_vfd_fault_reset(drv_vfd_t *vfd);

/**
 * @brief  获取当前运行状态，从 gear 字段派生
 * @param[in]  vfd  VFD 实例指针，可为 NULL（返回 STOPPED）
 * @retval     HAL_VFD_STATE_FWD     gear > 0（正转中）
 * @retval     HAL_VFD_STATE_REV     gear < 0（反转中）
 * @retval     HAL_VFD_STATE_STOPPED gear == 0 或 vfd 为 NULL
 */
drv_vfd_state_t drv_vfd_get_state(drv_vfd_t *vfd);

/**
 * @brief  实时读取故障码（发起 Modbus IO），成功时同步更新内部缓存和 fault_active
 * @param[in]  vfd     已完成 drv_vfd_init 的 VFD 实例
 * @param[out] p_code  输出故障码，0 表示无故障，不可为 NULL
 * @retval     SW_OK           读取成功，*p_code 有效
 * @retval     SW_ERR_PARAM    vfd 或 p_code 为 NULL
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 * @retval     SW_ERR_COMM     Modbus 读操作失败
 * @note   适用于主动确认状态（如复位前验证）；
 *         高频轮询请改用 drv_vfd_get_cached_fault_code（无 Modbus IO）
 */
sw_err_t drv_vfd_get_fault_code(drv_vfd_t *vfd, uint16_t *p_code);

/**
 * @brief  实时读取电机电流（发起 Modbus IO）
 * @param[in]  vfd       已完成 drv_vfd_init 的 VFD 实例
 * @param[out] p_current 输出电流值（单位 0.01A），不可为 NULL
 * @retval     SW_OK           读取成功
 * @retval     SW_ERR_PARAM    vfd 或 p_current 为 NULL
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 * @retval     SW_ERR_COMM     Modbus 读操作失败
 */
sw_err_t drv_vfd_read_current(drv_vfd_t *vfd, uint16_t *p_current);

/**
 * @brief  实时读取 VFD 状态字（发起 Modbus IO）
 * @param[in]  vfd      已完成 drv_vfd_init 的 VFD 实例
 * @param[out] p_status 输出状态字原始值，不可为 NULL；具体位含义见厂家手册
 * @retval     SW_OK           读取成功
 * @retval     SW_ERR_PARAM    vfd 或 p_status 为 NULL
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 * @retval     SW_ERR_COMM     Modbus 读操作失败
 */
sw_err_t drv_vfd_read_status(drv_vfd_t *vfd, uint16_t *p_status);

/**
 * @brief  注册 VFD 事件回调，驱动在通信状态变化或故障状态变化时调用
 * @param[in]  vfd  VFD 实例指针
 * @param[in]  cb   回调函数，传入事件码（HAL_VFD_EVT_*）；传 NULL 可注销回调
 * @note   回调在 monitor worker 线程或调用 drv_vfd_get_fault_code 的线程中触发，
 *         须保证回调函数线程安全；回调内禁止反向调用本驱动写接口（死锁风险）
 */
void drv_vfd_register_event_cb(drv_vfd_t *vfd, void (*cb)(int event_code));

/**
 * @brief  读取故障码缓存，无 Modbus IO，由 monitor worker 每 2s 更新一次
 * @param[in]  vfd  VFD 实例指针，可为 NULL
 * @retval  缓存的故障码，0 表示无故障；vfd 为 NULL 时返回 0
 * @note   适合高频轮询；实时性不如 drv_vfd_get_fault_code
 */
uint16_t drv_vfd_get_cached_fault_code(drv_vfd_t *vfd);

/**
 * @brief  读取电流缓存，无 Modbus IO，由 monitor worker 在运行中定期更新
 * @param[in]  vfd  VFD 实例指针，可为 NULL
 * @retval  缓存的电流值（0.01A）；vfd 为 NULL 或 VFD 处于停止态时返回 0
 */
uint16_t drv_vfd_get_cached_current(drv_vfd_t *vfd);

/**
 * @brief  设置 monitor worker 的读取项掩码，控制哪些 Modbus 信息被周期读取
 * @param[in]  vfd   已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  mask  读取项掩码，由 DRV_VFD_MON_* 标志位组合；
 *                   DRV_VFD_MON_NONE 停止所有周期读取，DRV_VFD_MON_ALL 全部开启
 * @retval     SW_OK           设置成功
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 * @note   初始化后默认 DRV_VFD_MON_ALL；写操作在 rst_mutex 保护下执行，
 *         最多延迟一个 VFD_SLOW_POLL_MS 周期生效
 */
sw_err_t drv_vfd_set_monitor_mask(drv_vfd_t *vfd, drv_vfd_monitor_mask_t mask);

#ifdef __cplusplus
}
#endif

#endif /* DRV_VFD_H */
