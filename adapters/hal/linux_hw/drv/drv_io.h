/**
 * @file    drv_io.h
 * @brief   CAN IO 子板驱动接口
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    工程内部唯一 IO 标识为强类型句柄：
 *          - `drv_io_di_t`：数字输入句柄
 *          - `drv_io_do_t`：数字输出句柄
 *
 *          句柄底层为 16 位编码，包含：
 *          - bit15：类型位，0=DI，1=DO
 *          - bit14~8：子板号
 *          - bit7~0：引脚号
 *
 *          `config/machine/m8_io_table.h` 是唯一 IO 定义总表。
 */

#ifndef DRV_IO_H
#define DRV_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "common/sw_error.h"
#include "common/sw_types.h"
#include "common/io_handle.h"

/* -------------------------------------------------------------------------
 * 句柄编码规则
 * ------------------------------------------------------------------------- */
#define DRV_IO_NULL                 IO_HANDLE_NULL
#define DRV_IO_KIND_SHIFT           IO_KIND_SHIFT
#define DRV_IO_KIND_MASK            IO_KIND_MASK
#define DRV_IO_KIND_DI              IO_KIND_DI
#define DRV_IO_KIND_DO              IO_KIND_DO
#define DRV_IO_HANDLE_BOARD_SHIFT   IO_HANDLE_BOARD_SHIFT
#define DRV_IO_HANDLE_BOARD_MASK    IO_HANDLE_BOARD_MASK
#define DRV_IO_HANDLE_PIN_MASK      IO_HANDLE_PIN_MASK
#define DRV_IO_HANDLE_MAKE(kind_, board_, pin_)  IO_HANDLE_MAKE(kind_, board_, pin_)

/* -------------------------------------------------------------------------
 * 强类型句柄
 * ------------------------------------------------------------------------- */
typedef io_di_t drv_io_di_t;
typedef io_do_t drv_io_do_t;

typedef void (*drv_io_debug_input_cb_t)(drv_io_di_t pin, bool state);

/** IO 驱动运行时统计（按子板，近似快照） */
typedef struct
{
    bool     online;                /**< 当前是否在线 */
    bool     dirty_pending;         /**< 当前是否存在未落地输出 */
    uint32_t offline_count;         /**< 确认离线次数 */
    uint32_t online_recover_count;  /**< 离线后恢复在线次数 */
    uint32_t input_refresh_count;   /**< 输入缓存刷新次数 */
    uint32_t output_request_count;  /**< 输出状态变更请求次数 */
    uint32_t output_flush_count;    /**< 输出实际写硬件次数 */
    uint32_t output_resend_count;   /**< 板卡恢复在线后的输出重发次数 */
    uint32_t last_online_ms;        /**< 最近一次恢复在线时间戳 */
    uint32_t last_offline_ms;       /**< 最近一次确认离线时间戳 */
    uint32_t last_input_refresh_ms; /**< 最近一次输入缓存刷新时间戳 */
    uint32_t last_output_req_ms;    /**< 最近一次输出变更请求时间戳 */
    uint32_t last_output_flush_ms;  /**< 最近一次输出落地时间戳 */
    uint32_t last_input_snapshot;   /**< 最近一次输入快照 */
    uint32_t last_output_snapshot;  /**< 最近一次输出快照 */
} drv_io_stats_t;

#ifdef __cplusplus
#define DRV_IO_DI(board_, pin_)    drv_io_di_t{DRV_IO_HANDLE_MAKE(DRV_IO_KIND_DI, board_, pin_)}
#define DRV_IO_DO(board_, pin_)    drv_io_do_t{DRV_IO_HANDLE_MAKE(DRV_IO_KIND_DO, board_, pin_)}
#else
#define DRV_IO_DI(board_, pin_)    ((drv_io_di_t)IO_DI(board_, pin_))
#define DRV_IO_DO(board_, pin_)    ((drv_io_do_t)IO_DO(board_, pin_))
#endif

/* -------------------------------------------------------------------------
 * 通过唯一总表生成 DI / DO 常量
 * ------------------------------------------------------------------------- */
#define DRV_IO_DI_DEF(name, board, pin, desc) \
    static const drv_io_di_t DI_##name = DRV_IO_DI(board, pin);
#include "config/machine/m8_io_table.h"
#undef DRV_IO_DI_DEF

#define DRV_IO_DO_DEF(name, board, pin, desc) \
    static const drv_io_do_t DO_##name = DRV_IO_DO(board, pin);
#include "config/machine/m8_io_table.h"
#undef DRV_IO_DO_DEF

/* -------------------------------------------------------------------------
 * 基础构造 / 拆解
 * ------------------------------------------------------------------------- */
static inline drv_io_di_t drv_io_di_make(uint16_t board_id, uint16_t pin_id)
{
    return (drv_io_di_t)io_di_make(board_id, pin_id);
}

static inline drv_io_do_t drv_io_do_make(uint16_t board_id, uint16_t pin_id)
{
    return (drv_io_do_t)io_do_make(board_id, pin_id);
}

static inline uint16_t drv_io_di_raw(drv_io_di_t pin)
{
    return io_di_raw((io_di_t)pin);
}

static inline uint16_t drv_io_do_raw(drv_io_do_t pin)
{
    return io_do_raw((io_do_t)pin);
}

static inline uint16_t drv_io_handle_kind(uint16_t raw)
{
    return io_handle_kind(raw);
}

static inline uint16_t drv_io_handle_board(uint16_t raw)
{
    return io_handle_board(raw);
}

static inline uint16_t drv_io_handle_pin(uint16_t raw)
{
    return io_handle_pin(raw);
}

/* -------------------------------------------------------------------------
 * 名称解析 / 可读名称
 * ------------------------------------------------------------------------- */
/**
 * @brief  解析 DI 名称为句柄
 * @note   支持 `DI_XXX` / `XXX` / `M8_DI_XXX` 三种写法。
 * @retval true=解析成功
 */
bool drv_io_try_parse_di(const char *name, drv_io_di_t *out);

/**
 * @brief  解析 DO 名称为句柄
 * @note   支持 `DO_XXX` / `XXX` / `M8_DO_XXX` 三种写法。
 * @retval true=解析成功
 */
bool drv_io_try_parse_do(const char *name, drv_io_do_t *out);

/**
 * @brief  获取 DI 句柄对应的标准名称
 * @retval 返回形如 `DI_ESTOP` 的静态字符串；未知句柄返回 NULL
 */
const char *drv_io_di_name(drv_io_di_t pin);

/**
 * @brief  获取 DO 句柄对应的标准名称
 * @retval 返回形如 `DO_WATER_PUMP` 的静态字符串；未知句柄返回 NULL
 */
const char *drv_io_do_name(drv_io_do_t pin);

/* -------------------------------------------------------------------------
 * 基础接口
 * ------------------------------------------------------------------------- */
/**
 * @brief  初始化 IO 子板驱动内部状态
 * @note   这里只做状态初始化，不再内部自建线程。
 *          IO 轮询线程由 bootstrap 配合 scheduler 统一注册和启动。
 *          本接口仅用于系统启动阶段初始化，不用于运行期复位。
 *          若测试场景需要重复调用本接口重置内部缓冲，调用方应在其后重新注册
 *          调试输入回调、子板状态回调和 panic 回调。
 */
sw_err_t drv_io_init(void);

/**
 * @brief  IO 轮询线程入口
 * @param  arg  线程参数，当前固定传 NULL
 * @return 线程退出值，无业务语义
 */
void *drv_io_poll_loop(void *arg);

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
sw_err_t drv_io_do_set(drv_io_do_t pin, bool val);

/**
 * @brief  读取数字输入缓存
 * @param  pin  输入句柄
 * @retval true=ON，false=OFF
 */
bool drv_io_di_read(drv_io_di_t pin);

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
 * @brief  注册子板在线状态变化回调
 * @param  cb  回调参数：board_id，offline=true 表示掉线，false 表示恢复
 */
void drv_io_register_board_error_cb(void (*cb)(int board_id, bool offline));

/**
 * @brief  注册全板离线 panic 回调
 * @note   检测到全部 IO 子板确认掉线时，drv_io 会先调用该回调准备安全态，
 *         然后执行 flush + abort，由 systemd 负责拉起进程。
 */
void drv_io_register_panic_cb(void (*cb)(void));

/**
 * @brief  设置 DI 测试覆盖值
 * @note   仅用于调试/测试，强制指定输入句柄返回固定值。
 * @param  pin    DI 句柄
 * @param  value  0=强制 OFF，1=强制 ON，其它值=清除覆盖
 */
void drv_io_set_test_override(drv_io_di_t pin, int value);

/**
 * @brief  清除 DI 测试覆盖
 * @param  pin  DI 句柄
 */
void drv_io_clear_test_override(drv_io_di_t pin);

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

#ifdef __cplusplus
}
#endif

#endif /* DRV_IO_H */
