/**
 * @file    m8_io_table.h
 * @brief   M8 机型 IO 点位总表（X-macro）
 * @author  HUWANGWEI
 * @date    2026-04-12
 *
 * @note    本文件故意不使用 include guard。
 *          使用方式：调用方先定义 `DRV_IO_DI_DEF` / `DRV_IO_DO_DEF`，
 *          再 `#include` 本文件以展开生成所需代码。
 *          新增或修改 M8 机型 IO 时，只维护这一份总表即可。
 */

#ifdef DRV_IO_DI_DEF
DRV_IO_DI_DEF(FRONT_WHEEL_LIMIT,    1, 1,  "前轮到位")
DRV_IO_DI_DEF(REAR_WHEEL_LOCK,      1, 2,  "后轮锁止")
DRV_IO_DI_DEF(TOP_BRUSH_COLLISION,  1, 3,  "顶刷防撞")
DRV_IO_DI_DEF(BUMPER_ROD_LEFT,      1, 4,  "左防撞杆")
DRV_IO_DI_DEF(BUMPER_ROD_RIGHT,     1, 5,  "右防撞杆")
DRV_IO_DI_DEF(ESTOP,                1, 6,  "急停按钮")
DRV_IO_DI_DEF(RELEASE_LOCK,         1, 7,  "释放后轮锁止")

DRV_IO_DI_DEF(REAR_LOCK_HOME,       1, 9,  "后轮锁原点")
DRV_IO_DI_DEF(BUMPER_LEFT,          1, 10, "左防撞胶条")
DRV_IO_DI_DEF(BUMPER_RIGHT,         1, 11, "右防撞胶条")
DRV_IO_DI_DEF(GANTRY_ENCODER_PULSE, 1, 12, "行走码盘")
DRV_IO_DI_DEF(GANTRY_REV_LIMIT,     1, 13, "龙门后限位")
DRV_IO_DI_DEF(GANTRY_FWD_LIMIT,     1, 14, "龙门前限位")
DRV_IO_DI_DEF(LIFT_UP_LIMIT,        1, 15, "升降上限位")
DRV_IO_DI_DEF(LIFT_DOWN_LIMIT,      1, 16, "升降下限位")
DRV_IO_DI_DEF(SWITCH_SIDE_BRUSH,    1, 17, "切换侧刷信号")
DRV_IO_DI_DEF(SWITCH_TOP_BRUSH,     1, 18, "切换顶刷信号")
DRV_IO_DI_DEF(SIDE_BRUSH_OVERLOAD,  1, 19, "侧刷过载")
DRV_IO_DI_DEF(TOP_BRUSH_OVERLOAD,   1, 20, "顶刷过载")
DRV_IO_DI_DEF(WATER_PUMP_OVERLOAD,  1, 21, "水泵过载")
DRV_IO_DI_DEF(GANTRY_ALARM,         1, 22, "行走报警反馈")
DRV_IO_DI_DEF(FAN_ALARM,            1, 23, "风机报警反馈")
DRV_IO_DI_DEF(BRUSH_ALARM,          1, 24, "毛刷报警反馈")
#endif

#ifdef DRV_IO_DO_DEF
DRV_IO_DO_DEF(ENTRY_GREEN,          1, 1,  "入口绿灯")
DRV_IO_DO_DEF(ENTRY_RED,            1, 2,  "入口红灯")
DRV_IO_DO_DEF(LED_STEP_1,           1, 3,  "步骤指示灯1")
DRV_IO_DO_DEF(LED_STEP_2,           1, 4,  "步骤指示灯2")
DRV_IO_DO_DEF(LED_STEP_3,           1, 5,  "步骤指示灯3")
DRV_IO_DO_DEF(FLOODLIGHT,           1, 6,  "照明灯")
DRV_IO_DO_DEF(TOP_BRUSH_UP,         1, 7,  "顶刷上升")
DRV_IO_DO_DEF(TOP_BRUSH_DOWN,       1, 8,  "顶刷下降")
DRV_IO_DO_DEF(GANTRY_FWD,           1, 9,  "龙门前进")
DRV_IO_DO_DEF(GANTRY_REV,           1, 10, "龙门后退")
DRV_IO_DO_DEF(GANTRY_HIGH_SPEED,    1, 11, "龙门行走高速")
DRV_IO_DO_DEF(GANTRY_RST,           1, 12, "龙门复位")
DRV_IO_DO_DEF(FAN_START,            1, 13, "风机启动")
DRV_IO_DO_DEF(FAN_RESET,            1, 14, "风机复位")
DRV_IO_DO_DEF(BRUSH_FWD,            1, 15, "刷子正转")
DRV_IO_DO_DEF(BRUSH_REV,            1, 16, "刷子反转")
DRV_IO_DO_DEF(WATER_CURTAIN,        1, 17, "清水水帘阀")
DRV_IO_DO_DEF(WATER_TOP_FOAM,       1, 18, "顶部泡沫阀")
DRV_IO_DO_DEF(WATER_TOP,            1, 19, "顶部冲水阀")
DRV_IO_DO_DEF(WATER_BUTTOM,         1, 20, "底部冲水阀")
DRV_IO_DO_DEF(WATER_BUTTOM_FOAM,    1, 21, "底部泡沫阀")
DRV_IO_DO_DEF(SUBMERSIBLE_PUMP,     1, 22, "潜水泵")
DRV_IO_DO_DEF(SIDE_BRUSH_ACT,       1, 23, "侧刷动作")
DRV_IO_DO_DEF(TOP_BRUSH_ACT,        1, 24, "顶刷动作")
DRV_IO_DO_DEF(WATER_PUMP,           1, 25, "水泵启动")
DRV_IO_DO_DEF(BRUSH_HIGH_SPEED,     1, 26, "刷子高速")
DRV_IO_DO_DEF(PARAM_SEL,            1, 27, "参数选择")
DRV_IO_DO_DEF(BRUSH_RST,            1, 28, "刷子复位")
DRV_IO_DO_DEF(ROD_EXTEND,           1, 29, "电动推杆伸出")
DRV_IO_DO_DEF(ROD_RETRACT,          1, 30, "电动推杆缩回")
#endif
