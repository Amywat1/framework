/**
 * @file    drv_io_def.h
 * @brief   IO 定义总表（X-macro）
 * @author  胡望伟
 * @date    2026-04-12
 *
 * @note    本文件故意不使用 include guard。
 *          使用方式：调用方先定义 `DRV_IO_DI_DEF` / `DRV_IO_DO_DEF`，
 *          再 `#include` 本文件以展开生成所需代码。
 *          新增或修改 IO 时，只维护这一份总表即可。
 */

#ifdef DRV_IO_DI_DEF
DRV_IO_DI_DEF(GANTRY_REAR_LIMIT, 1, 3,  "行走后限位")
DRV_IO_DI_DEF(GANTRY_FWD_LIMIT,  1, 4,  "行走前限位")
DRV_IO_DI_DEF(TOP_LIFT_DOWN,     1, 7,  "顶刷下限位")
DRV_IO_DI_DEF(TOP_LIFT_UP,       1, 8,  "顶刷上限位")
DRV_IO_DI_DEF(ENCODER_PULSE,     1, 9,  "码盘脉冲（龙门位置计数）")
DRV_IO_DI_DEF(ESTOP,             1, 13, "急停按钮（常闭，低电平有效）")
#endif

#ifdef DRV_IO_DO_DEF
DRV_IO_DO_DEF(ENTRY_GREEN1,    1, 1,  "入口绿灯 1")
DRV_IO_DO_DEF(ENTRY_RED,       1, 2,  "入口红灯")
DRV_IO_DO_DEF(ENTRY_GREEN2,    1, 3,  "入口绿灯 2")
DRV_IO_DO_DEF(ENTRY_YELLOW,    1, 4,  "入口黄灯")
DRV_IO_DO_DEF(ROD_EXTEND,      1, 5,  "电动推杆伸出")
DRV_IO_DO_DEF(ROD_RETRACT,     1, 6,  "电动推杆缩回")
DRV_IO_DO_DEF(TOP_LIFT_ENA,    1, 7,  "顶刷升降步进—使能（ENA）")
DRV_IO_DO_DEF(TOP_LIFT_DIR,    1, 8,  "顶刷升降步进—方向（DIR）")
DRV_IO_DO_DEF(WATER_PUMP,      1, 10, "水泵启动")
DRV_IO_DO_DEF(SIDE_BRUSH_FWD,  1, 13, "刷子 VFD—正转")
DRV_IO_DO_DEF(SIDE_BRUSH_REV,  1, 14, "刷子 VFD—反转")
DRV_IO_DO_DEF(SIDE_BRUSH_RST,  1, 15, "刷子 VFD—复位")
DRV_IO_DO_DEF(GANTRY_FWD,      1, 16, "龙门 VFD—前进")
DRV_IO_DO_DEF(GANTRY_REV,      1, 17, "龙门 VFD—后退")
DRV_IO_DO_DEF(GANTRY_RST,      1, 18, "龙门 VFD—复位")
DRV_IO_DO_DEF(WATER_CURTAIN,   1, 19, "清水水帘阀")
DRV_IO_DO_DEF(WATER_FOAM,      1, 20, "泡沫+预洗液阀")
DRV_IO_DO_DEF(WATER_BRUSH,     1, 21, "侧刷冲水阀")
DRV_IO_DO_DEF(WATER_HIGHPRES,  1, 22, "高压冲洗阀")
DRV_IO_DO_DEF(WATER_SPARE1,    1, 23, "备用水阀 1")
DRV_IO_DO_DEF(WATER_SPARE2,    1, 24, "备用水阀 2")
DRV_IO_DO_DEF(TOP_LIFT_PUL,    1, 26, "顶刷升降步进—脉冲（PUL，H26）")
DRV_IO_DO_DEF(PARAM_SEL,       1, 27, "参数选择（H27）")
DRV_IO_DO_DEF(SIDE_BRUSH_ACT,  1, 28, "接触器 2—侧刷接 VFD（H28）")
DRV_IO_DO_DEF(TOP_BRUSH_ACT,   1, 29, "接触器 1—顶刷接 VFD（H29）")
#endif
