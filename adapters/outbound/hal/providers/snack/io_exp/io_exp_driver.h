/**
 * @file    io_exp_driver.h
 * @brief   io_exp CAN IO 子板 provider 接口
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    工程内部唯一 IO 标识为强类型句柄：
 *          - `io_di_t`：数字输入句柄（来自 common/io_handle.h）
 *          - `io_do_t`：数字输出句柄（来自 common/io_handle.h）
 *
 *          句柄底层为 16 位编码，包含：
 *          - bit15：类型位，0=DI，1=DO
 *          - bit14~8：子板号
 *          - bit7~0：引脚号
 */

#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_IO_EXP_IO_EXP_DRIVER_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_IO_EXP_IO_EXP_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include "common/io_sample.h"
#include "common/sw_error.h"
#include "common/sw_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief  初始化 io_exp SDK 的 CAN 总线访问。
 * @note   仅供 snack_io_adapter 等 provider 内部调用；项目入口不应直接依赖。
 */
sw_err_t io_exp_driver_sdk_init(const char *can_bus, int can_baud, int self_node, int board_count);

/** IO 名称表条目（由调用方用 X-macro 展开后传入驱动） */
typedef struct {
    const char *name; /**< 标准名称，如 "DI_ESTOP" */
    uint16_t    raw;  /**< 句柄底层编码 */
} drv_io_name_entry_t;

/** IO 驱动运行时统计（按子板，近似快照） */
typedef struct {
    bool     online;                /**< 当前是否在线 */
    bool     dirty_pending;         /**< 当前是否存在未落地输出 */
    uint32_t offline_count;         /**< 确认离线次数 */
    uint32_t online_recover_count;  /**< 离线后恢复在线次数 */
    uint32_t input_refresh_count;   /**< 输入缓存刷新次数 */
    uint32_t output_request_count;  /**< 输出状态变更请求次数 */
    uint32_t output_flush_count;    /**< 输出实际写硬件次数 */
    uint64_t last_online_ms;        /**< 最近一次恢复在线时间戳 */
    uint64_t last_offline_ms;       /**< 最近一次确认离线时间戳 */
    uint64_t last_input_refresh_ms; /**< 最近一次输入缓存刷新时间戳 */
    uint64_t last_output_req_ms;    /**< 最近一次输出变更请求时间戳 */
    uint64_t last_output_flush_ms;  /**< 最近一次输出落地时间戳 */
    uint32_t last_input_snapshot;   /**< 最近一次输入快照 */
    uint32_t last_output_snapshot;  /**< 最近一次输出快照 */
} drv_io_stats_t;

/* -------------------------------------------------------------------------
 * 名称解析 / 可读名称
 * ------------------------------------------------------------------------- */
/**
 * @brief  解析 DI 名称为句柄
 * @note   支持 `DI_XXX`（完整名称）/ `XXX`（省略前缀）两种写法。
 * @retval true=解析成功
 */
bool drv_io_try_parse_di(const char *name, io_di_t *out);

/**
 * @brief  解析 DO 名称为句柄
 * @note   支持 `DO_XXX`（完整名称）/ `XXX`（省略前缀）两种写法。
 * @retval true=解析成功
 */
bool drv_io_try_parse_do(const char *name, io_do_t *out);

/**
 * @brief  获取 DI 句柄对应的标准名称
 * @retval 返回形如 `DI_ESTOP` 的静态字符串；未知句柄返回 NULL
 */
const char *drv_io_di_name(io_di_t pin);

/**
 * @brief  获取 DO 句柄对应的标准名称
 * @retval 返回形如 `DO_WATER_PUMP` 的静态字符串；未知句柄返回 NULL
 */
const char *drv_io_do_name(io_do_t pin);

/* -------------------------------------------------------------------------
 * 基础接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  IO 子板驱动初始化配置
 * @note   board_count 须小于驱动内部上限（7）；pin_count 须不大于硬件上限（32）。
 *         di_table/do_table 为名称映射表，由调用方用 X-macro 展开后传入；
 *         允许传 NULL + 0，此时名称查找接口均返回失败/NULL。
 */
typedef struct {
    const char                *can_bus;     /**< io_exp SDK 使用的 CAN 设备名 */
    int                        can_baud;    /**< io_exp SDK 使用的 CAN 波特率 */
    int                        self_node;   /**< io_exp SDK 使用的本机节点号 */
    int                        board_count; /**< 实际使用的 IO 子板数量 */
    int                        pin_count;   /**< 每块子板的 IO 点数 */
    const drv_io_name_entry_t *di_table;    /**< DI 名称映射表 */
    size_t                     di_count;    /**< DI 表条目数 */
    const drv_io_name_entry_t *do_table;    /**< DO 名称映射表 */
    size_t                     do_count;    /**< DO 表条目数 */
} drv_io_cfg_t;

/**
 * @brief  校验 IO 子板驱动配置
 * @param  cfg  驱动配置，不可为 NULL
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t drv_io_cfg_validate(const drv_io_cfg_t *cfg);

/**
 * @brief  初始化 IO 子板驱动内部状态
 * @param  cfg  驱动配置，不可为 NULL
 * @note   仅做状态初始化，不启动后台线程；线程由 drv_io_start() 启动。
 *         本接口仅用于系统启动阶段，不用于运行期复位。
 *         若测试场景需要重复调用本接口重置内部缓冲，调用方应在其后重新注册
 *         调试输入回调、子板状态回调和 panic 回调，并再次调用 drv_io_start()。
 * @retval SW_OK / SW_ERR_PARAM（cfg 为 NULL 或参数越界）
 */
sw_err_t drv_io_init(const drv_io_cfg_t *cfg);

/**
 * @brief  启动 IO 读写后台线程（输入刷新 / 输出落地 / 在线检测）
 * @note   由 IO 驱动模块自行创建 pthread，不经过 core/scheduler。
 *         须在 drv_io_register_panic_cb() 等回调注册完成后调用。
 * @retval SW_OK / SW_ERR_HW / SW_ERR_STATE（已启动）
 */
sw_err_t drv_io_start(void);

/**
 * @brief  立即将当前输出缓冲同步刷到硬件
 * @note   正常路径由后台轮询线程异步写出。
 *          本接口主要用于 panic handler、启动安全态等需要“立即落地”的场景。
 */
sw_err_t drv_io_flush_outputs_now(void);

/**
 * @brief  设置数字输出
 * @param  pin  输出句柄
 * @param  val  true=ON，false=OFF
 */
sw_err_t drv_io_do_set(io_do_t pin, bool val);

/**
 * @brief  读取数字输入快照
 * @param  pin     输入句柄
 * @param  sample  输出采样，不可为 NULL
 * @retval SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT
 */
sw_err_t drv_io_di_read(io_di_t pin, io_di_sample_t *sample);

typedef void (*drv_io_debug_input_cb_t)(io_di_t pin, bool state);

/**
 * @brief  注册输入变化调试回调
 * @note   该回调仅用于观察 IO 变化，不参与项目正式控制逻辑。
 */
void drv_io_register_debug_input_cb(drv_io_debug_input_cb_t cb);

/* -------------------------------------------------------------------------
 * 扩展接口
 * ------------------------------------------------------------------------- */
/**
 * @brief  查询指定子板是否在线
 * @param  board_id  子板号，从 1 开始
 */
bool drv_io_board_is_online(int board_id);

/**
 * @brief  同步轮询等待所有 IO 子板就绪（启动阶段，后台线程启动前可调用）
 * @param  timeout_ms  最长等待时间（ms）
 * @retval SW_OK           所有子板在超时内就绪
 * @retval SW_ERR_TIMEOUT  超时仍有子板离线
 */
sw_err_t drv_io_wait_boards_online(uint32_t timeout_ms);

/**
 * @brief  注册子板在线状态变化回调
 * @param  cb  回调参数：board_id，offline=true 表示掉线，false 表示恢复
 */
void drv_io_register_board_error_cb(void (*cb)(int board_id, bool offline));

/**
 * @brief  注册全板离线 panic 回调
 * @note   检测到全部 IO 子板确认掉线时调用。
 *         回调应通过 drv_io_do_set 将输出缓冲设为安全态，
 *         不应自行调用 flush（flush 由 drv_io 在回调返回后无条件执行）。
 *         flush 完成后进程 abort，由 systemd 负责拉起。
 */
void drv_io_register_panic_cb(void (*cb)(void));

/**
 * @brief  设置 DI 测试覆盖值
 * @note   仅用于调试/测试，强制指定输入句柄返回固定值。
 * @param  pin    DI 句柄
 * @param  value  0=强制 OFF，1=强制 ON，其它值=清除覆盖
 */
void drv_io_set_test_override(io_di_t pin, int value);

/**
 * @brief  清除 DI 测试覆盖
 * @param  pin  DI 句柄
 */
void drv_io_clear_test_override(io_di_t pin);

/**
 * @brief  获取指定子板的 IO 运行时统计
 * @note   返回的是近似快照，仅用于诊断参考，不用于业务判断。
 * @param  board_id  子板号，从 1 开始
 * @param  out       输出统计结构体
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t drv_io_get_stats(int board_id, drv_io_stats_t *out);

/**
 * @brief  获取当前配置的 IO 子板数量
 * @retval 子板数量，不包含 0 号占位
 */
int drv_io_board_count(void);

/* -------------------------------------------------------------------------
 * 脉冲计数器接口（编码器，底层调用 io_exp provider 内部 SDK 接口）
 * ------------------------------------------------------------------------- */

/**
 * @brief  读取 DI 引脚对应的硬件脉冲计数器
 * @param  pin  DI 句柄；须为有效编码器输入引脚
 * @retval >= 0  当前计数值
 * @retval < 0 或 0x0FFFFFFF  读取失败或计数器溢出/无效
 */
int drv_io_pulse_read(io_di_t pin);

/**
 * @brief  清零 DI 引脚对应的硬件脉冲计数器（通过 CANopen SDO 对象 0x2005）
 * @param  pin  DI 句柄；须为有效编码器输入引脚
 * @retval SW_OK       清零成功
 * @retval SW_ERR_PARAM  引脚无效
 * @retval SW_ERR_COMM   SDO 写入失败
 */
sw_err_t drv_io_pulse_clear(io_di_t pin);

/* -------------------------------------------------------------------------
 * ADC 接口（底层调用 io_exp provider 内部 SDK 接口）
 * ------------------------------------------------------------------------- */

/** ADC 通道号下限（含） */
#define DRV_IO_ADC_PORT_MIN     1
/** ADC 通道号上限（含） */
#define DRV_IO_ADC_PORT_MAX     4
/** SDK 约定：子板未初始化时 ADC 读返回值 */
#define DRV_IO_ADC_ERR_NOT_INIT (-99)

/**
 * @brief  读取 ADC 原始值
 * @param  board_id  子板号，从 1 开始
 * @param  port      ADC 通道号，范围 [DRV_IO_ADC_PORT_MIN, DRV_IO_ADC_PORT_MAX]
 * @retval >= 0                     ADC 原始值
 * @retval DRV_IO_ADC_ERR_NOT_INIT  子板未初始化
 * @retval 其他负值                 参数非法或 SDO 读失败
 */
int drv_io_adc_read(int board_id, int port);

/**
 * @brief  读取 ADC 并换算为电压（mV）
 * @param  board_id  子板号，从 1 开始
 * @param  port      ADC 通道号，范围 [DRV_IO_ADC_PORT_MIN, DRV_IO_ADC_PORT_MAX]
 * @retval >= 0                     电压值（mV）
 * @retval DRV_IO_ADC_ERR_NOT_INIT  子板未初始化
 * @retval 其他负值                 参数非法或 SDO 读失败
 */
int drv_io_adc_mv(int board_id, int port);

/**
 * @brief  读取 ADC 并换算为电流（mA）
 * @param  board_id  子板号，从 1 开始
 * @param  port      ADC 通道号，范围 [DRV_IO_ADC_PORT_MIN, DRV_IO_ADC_PORT_MAX]
 * @retval >= 0                     电流值（mA）
 * @retval DRV_IO_ADC_ERR_NOT_INIT  子板未初始化
 * @retval 其他负值                 参数非法或 SDO 读失败
 */
int drv_io_adc_ma(int board_id, int port);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_IO_EXP_IO_EXP_DRIVER_H */
