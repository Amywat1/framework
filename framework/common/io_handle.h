/**
 * @file    io_handle.h
 * @brief   中立的 IO 句柄类型与编解码工具
 * @author  HUWANGWEI
 * @date    2026-04-13
 *
 * @note    本头文件位于 `common/`，供 `ports/`、`driver/`、`adapters/`
 *          共同复用，避免在边界接口层直接暴露 driver 头文件。
 *
 *          16 位句柄编码规则：
 *          - bit15：类型位，0=DI，1=DO
 *          - bit14~8：子板号
 *          - bit7~0：引脚号
 */

#ifndef COMMON_IO_HANDLE_H
#define COMMON_IO_HANDLE_H

#include <stdint.h>

#define IO_HANDLE_NULL                0U

#define IO_KIND_SHIFT                 15U
#define IO_KIND_MASK                  0x8000U
#define IO_KIND_DI                    0x0000U
#define IO_KIND_DO                    0x8000U

#define IO_HANDLE_BOARD_SHIFT         8U
#define IO_HANDLE_BOARD_MASK          0x7F00U
#define IO_HANDLE_PIN_MASK            0x00FFU

#define IO_HANDLE_MAKE(kind_, board_, pin_)                                        \
    ((uint16_t)((kind_)                                                             \
                | ((((uint16_t)(board_)) & 0x7FU) << IO_HANDLE_BOARD_SHIFT)        \
                | (((uint16_t)(pin_)) & IO_HANDLE_PIN_MASK)))

typedef struct
{
    uint16_t raw;
} io_di_t;

typedef struct
{
    uint16_t raw;
} io_do_t;

#ifdef __cplusplus
#define IO_DI(board_, pin_)    io_di_t{IO_HANDLE_MAKE(IO_KIND_DI, board_, pin_)}
#define IO_DO(board_, pin_)    io_do_t{IO_HANDLE_MAKE(IO_KIND_DO, board_, pin_)}
#else
#define IO_DI(board_, pin_)    ((io_di_t){IO_HANDLE_MAKE(IO_KIND_DI, board_, pin_)})
#define IO_DO(board_, pin_)    ((io_do_t){IO_HANDLE_MAKE(IO_KIND_DO, board_, pin_)})
#endif

static inline io_di_t io_di_make(uint16_t board_id, uint16_t pin_id)
{
    return IO_DI(board_id, pin_id);
}

static inline io_do_t io_do_make(uint16_t board_id, uint16_t pin_id)
{
    return IO_DO(board_id, pin_id);
}

static inline uint16_t io_di_raw(io_di_t pin)
{
    return pin.raw;
}

static inline uint16_t io_do_raw(io_do_t pin)
{
    return pin.raw;
}

static inline uint16_t io_handle_kind(uint16_t raw)
{
    return (uint16_t)(raw & IO_KIND_MASK);
}

static inline uint16_t io_handle_board(uint16_t raw)
{
    return (uint16_t)((raw & IO_HANDLE_BOARD_MASK) >> IO_HANDLE_BOARD_SHIFT);
}

static inline uint16_t io_handle_pin(uint16_t raw)
{
    return (uint16_t)(raw & IO_HANDLE_PIN_MASK);
}

#endif /* COMMON_IO_HANDLE_H */
