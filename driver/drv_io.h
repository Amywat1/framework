/**
 * @file    drv_io.h
 * @brief   CAN IO 子板驱动接口（封装 io_exp SDK，统一使用图纸 DO/DI 编号）
 * @author  胡望伟
 * @date    2026-04-07
 *
 * @note    IO 地址编码规则：io_id = board_id × 100 + pin（pin 从 1 开始）
 *          M8 共 1 块子板（board_id = 1），引脚范围 101-132。
 *          修改引脚分配时只需更新下方枚举，驱动实现无需变动。
 */

#ifndef DRV_IO_H
#define DRV_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * IO 地址编解码（内部使用）
 * ------------------------------------------------------------------------- */
#define DRV_IO_BOARD_VAL        100
#define DRV_IO_BOARD_ID(io_id)  ((io_id) / DRV_IO_BOARD_VAL)
#define DRV_IO_PIN_ID(io_id)    ((io_id) % DRV_IO_BOARD_VAL)
#define DRV_IO_NULL             0

/* -------------------------------------------------------------------------
 * 数字输入引脚（对应图纸 DI 编号，board 1）
 * ------------------------------------------------------------------------- */
typedef enum
{
    DI_GANTRY_REAR_LIMIT  = 103,    /* 行走后限位 */
    DI_GANTRY_FWD_LIMIT   = 104,    /* 行走前限位 */
    DI_TOP_LIFT_DOWN      = 107,    /* 顶刷下限位 */
    DI_TOP_LIFT_UP        = 108,    /* 顶刷上限位 */
    DI_ENCODER_PULSE      = 109,    /* 码盘脉冲（龙门位置计数）*/
    DI_ESTOP              = 113,    /* 急停按钮（常闭，低电平有效）*/
} drv_io_di_t;

/* -------------------------------------------------------------------------
 * 数字输出引脚（对应图纸 DO/H 编号，board 1）
 * ------------------------------------------------------------------------- */
typedef enum
{
    DO_ENTRY_GREEN1       = 101,    /* 入口绿灯 1 */
    DO_ENTRY_RED          = 102,    /* 入口红灯 */
    DO_ENTRY_GREEN2       = 103,    /* 入口绿灯 2 */
    DO_ENTRY_YELLOW       = 104,    /* 入口黄灯 */
    DO_ROD_EXTEND         = 105,    /* 电动推杆伸出 */
    DO_ROD_RETRACT        = 106,    /* 电动推杆缩回 */
    DO_TOP_LIFT_ENA       = 107,    /* 顶刷升降步进 — 使能（ENA）*/
    DO_TOP_LIFT_DIR       = 108,    /* 顶刷升降步进 — 方向（DIR）*/
    DO_WATER_PUMP         = 110,    /* 水泵启动 */
    DO_SIDE_BRUSH_FWD     = 113,    /* 刷子 VFD — 正转 */
    DO_SIDE_BRUSH_REV     = 114,    /* 刷子 VFD — 反转 */
    DO_SIDE_BRUSH_RST     = 115,    /* 刷子 VFD — 复位 */
    DO_GANTRY_FWD         = 116,    /* 龙门 VFD — 前进 */
    DO_GANTRY_REV         = 117,    /* 龙门 VFD — 后退 */
    DO_GANTRY_RST         = 118,    /* 龙门 VFD — 复位 */
    DO_WATER_CURTAIN      = 119,    /* 清水水帘阀 */
    DO_WATER_FOAM         = 120,    /* 泡沫+预洗液阀 */
    DO_WATER_BRUSH        = 121,    /* 侧刷冲水阀 */
    DO_WATER_HIGHPRES     = 122,    /* 高压冲洗阀 */
    DO_WATER_SPARE1       = 123,    /* 备用水阀 1 */
    DO_WATER_SPARE2       = 124,    /* 备用水阀 2 */
    DO_TOP_LIFT_PUL       = 126,    /* 顶刷升降步进 — 脉冲（PUL，H26）*/
    DO_PARAM_SEL          = 127,    /* 参数选择（H27）*/
    DO_SIDE_BRUSH_ACT     = 128,    /* 接触器 2 — 侧刷接 VFD（H28）*/
    DO_TOP_BRUSH_ACT      = 129,    /* 接触器 1 — 顶刷接 VFD（H29）*/
} drv_io_do_t;

/* -------------------------------------------------------------------------
 * 基础接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化 IO 子板驱动并启动后台读写线程
 * @retval SW_OK / SW_ERR_HW
 */
sw_err_t drv_io_init(void);

/**
 * @brief  设置数字输出（写入输出缓冲，由后台线程同步到子板）
 * @param  pin  输出引脚（drv_io_do_t）
 * @param  val  true=ON / false=OFF
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t drv_io_do_set(drv_io_do_t pin, bool val);

/**
 * @brief  读取数字输入（读取后台线程维护的输入缓冲）
 * @param  pin  输入引脚（drv_io_di_t）
 * @retval true=ON / false=OFF
 */
bool drv_io_di_read(drv_io_di_t pin);

/**
 * @brief  注册输入变化回调（任意输入引脚变化时触发）
 * @param  cb  回调函数，参数：io_id（board×100+pin）和新状态
 */
void drv_io_register_input_cb(void (*cb)(int io_id, bool state));

/* -------------------------------------------------------------------------
 * 扩展接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  查询指定子板是否在线
 * @param  board_id  子板 ID（1-based）
 * @retval true=在线
 */
bool drv_io_board_is_online(int board_id);

/**
 * @brief  注册子板在线状态变化回调
 * @param  cb  回调函数：board_id，offline=true 表示掉线，false 表示恢复
 */
void drv_io_register_board_error_cb(void (*cb)(int board_id, bool offline));

/**
 * @brief  设置 IO 测试覆盖值（调试用，强制指定引脚返回固定值）
 * @param  io_id  IO 地址（board×100+pin）
 * @param  value  0=强制 OFF，1=强制 ON，其它值=清除覆盖
 */
void drv_io_set_test_override(int io_id, int value);

/**
 * @brief  清除 IO 测试覆盖
 * @param  io_id  IO 地址
 */
void drv_io_clear_test_override(int io_id);

#ifdef __cplusplus
}
#endif

#endif /* DRV_IO_H */
