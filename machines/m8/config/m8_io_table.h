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
DRV_IO_DI_DEF(REAR_WHEEL_LOCK1,     1, 2,  "后轮锁止1")
DRV_IO_DI_DEF(REAR_WHEEL_LOCK2,     1, 3,  "后轮锁止2")
DRV_IO_DI_DEF(OVERHEIGHT_DETECT,    1, 4,  "超高检测")
DRV_IO_DI_DEF(REAR_LOCK_HOME,       1, 9,  "后轮锁原点")
DRV_IO_DI_DEF(BUMPER_LEFT,          1, 10, "左防撞胶条")
DRV_IO_DI_DEF(BUMPER_RIGHT,         1, 11, "右防撞胶条")
DRV_IO_DI_DEF(GANTRY_ENCODER_PULSE, 1, 12, "行走码盘")
DRV_IO_DI_DEF(GANTRY_FWD_LIMIT,     1, 13, "龙门前限位")
DRV_IO_DI_DEF(GANTRY_REV_LIMIT,     1, 14, "龙门后限位")
DRV_IO_DI_DEF(LIFT_UP_LIMIT,        1, 15, "升降上限位")
DRV_IO_DI_DEF(LIFT_DOWN_LIMIT,      1, 16, "升降下限位")
DRV_IO_DI_DEF(TOP_BRUSH_COLLISION,  1, 17, "顶刷防撞")
DRV_IO_DI_DEF(BUMPER_ROD_LEFT,      1, 18, "左防撞杆")
DRV_IO_DI_DEF(BUMPER_ROD_RIGHT,     1, 19, "右防撞杆")
DRV_IO_DI_DEF(HEIGHT_CONTROL,       1, 20, "高度控制")
DRV_IO_DI_DEF(ESTOP,                1, 21, "急停按钮")
DRV_IO_DI_DEF(HALL_FEEDBACK1,       1, 22, "霍尔反馈1")
DRV_IO_DI_DEF(HALL_FEEDBACK2,       1, 23, "霍尔反馈2")
DRV_IO_DI_DEF(LIFT_ALARM_FEEDBACK,  1, 24, "顶刷升降报警反馈")
DRV_IO_DI_DEF(SIDE_BRUSH_OVERLOAD,  1, 25, "侧刷过载")
DRV_IO_DI_DEF(TOP_BRUSH_OVERLOAD,   1, 26, "顶刷过载")
DRV_IO_DI_DEF(WATER_PUMP_OVERLOAD,  1, 27, "水泵过载")
DRV_IO_DI_DEF(GANTRY_ALARM,         1, 28, "行走报警反馈")
DRV_IO_DI_DEF(FAN_ALARM,            1, 29, "风机报警反馈")
DRV_IO_DI_DEF(SIDE_BRUSH_ALARM,     1, 30, "侧刷报警反馈")
#endif

#ifdef DRV_IO_DO_DEF
DRV_IO_DO_DEF(ENTRY_GREEN1,         1, 1,  "入口绿灯 1")
DRV_IO_DO_DEF(ENTRY_RED,            1, 2,  "入口红灯")
DRV_IO_DO_DEF(ENTRY_GREEN2,         1, 3,  "入口绿灯 2")
DRV_IO_DO_DEF(ENTRY_YELLOW,         1, 4,  "入口黄灯")
DRV_IO_DO_DEF(ROD_EXTEND,           1, 5,  "电动推杆伸出")
DRV_IO_DO_DEF(ROD_RETRACT,          1, 6,  "电动推杆缩回")
DRV_IO_DO_DEF(WATER_PUMP,           1, 10, "水泵启动")
DRV_IO_DO_DEF(SIDE_BRUSH_FWD,       1, 11, "侧刷正转")
DRV_IO_DO_DEF(SIDE_BRUSH_REV,       1, 12, "侧刷反转")
DRV_IO_DO_DEF(SIDE_BRUSH_RST,       1, 13, "侧刷复位")
DRV_IO_DO_DEF(GANTRY_FWD,           1, 14, "龙门前进")
DRV_IO_DO_DEF(GANTRY_REV,           1, 15, "龙门后退")
DRV_IO_DO_DEF(GANTRY_RST,           1, 16, "龙门复位")
DRV_IO_DO_DEF(WATER_CURTAIN,        1, 17, "清水水帘阀")
DRV_IO_DO_DEF(WATER_FOAM,           1, 18, "泡沫+预洗液阀")
DRV_IO_DO_DEF(WATER_BRUSH,          1, 19, "侧刷冲水阀")
DRV_IO_DO_DEF(WATER_HIGHPRES,       1, 20, "高压冲洗阀")
DRV_IO_DO_DEF(WATER_SPARE1,         1, 21, "备用水阀 1")
DRV_IO_DO_DEF(WATER_SPARE2,         1, 22, "备用水阀 2")
DRV_IO_DO_DEF(FAN_START,            1, 23, "风机启动")
DRV_IO_DO_DEF(FAN_RESET,            1, 24, "风机复位")
DRV_IO_DO_DEF(GANTRY_HIGH_SPEED,    1, 25, "龙门行走高速")
DRV_IO_DO_DEF(PARAM_SEL,            1, 27, "参数选择(H27)")
DRV_IO_DO_DEF(SIDE_BRUSH_ACT,       1, 28, "接触器 1—侧刷接")
DRV_IO_DO_DEF(TOP_BRUSH_ACT,        1, 29, "接触器 2—顶刷接")
#endif
