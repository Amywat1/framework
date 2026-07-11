/**
 * @file    m8_brush_ids.h
 * @brief   M8 机型刷子逻辑槽位编号。
 *
 * 与 m8_motor_id_t（MCC 电机槽位）区分开：这里是 brush_init() 的槽位顺序，
 * 供 m8_mechanism_setup.c 装配与 engine_io_m8.c 下发命令时共同引用。
 */
#ifndef MACHINES_M8_CONFIG_M8_BRUSH_IDS_H
#define MACHINES_M8_CONFIG_M8_BRUSH_IDS_H

/** @brief M8 刷子逻辑槽位（M8_BRUSH_COUNT 为总数哨兵，非有效槽位编号）。 */
enum {
    M8_BRUSH_SIDE = 0, /**< 侧刷 */
    M8_BRUSH_TOP  = 1, /**< 顶刷（与侧刷共用 VFD，靠接触器切换，二者互锁） */
    M8_BRUSH_COUNT
};

#endif /* MACHINES_M8_CONFIG_M8_BRUSH_IDS_H */
