/**
 * @file    m8_io_config.h
 * @brief   M8 机型 IO 子板硬件连接参数
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    引脚定义见 m8_io_table.h / m8_io_pins.h。
 */

#ifndef CONFIG_MACHINE_M8_IO_CONFIG_H
#define CONFIG_MACHINE_M8_IO_CONFIG_H

#define CFG_IO_CAN_BUS              "can0"
#define CFG_IO_CAN_BAUD             1000000
#define CFG_IO_SELF_NODE            0x10
#define CFG_IO_BOARD_COUNT          1       /* M8 共 1 块 IO 子板 */

#endif /* CONFIG_MACHINE_M8_IO_CONFIG_H */
