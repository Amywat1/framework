/**
 * @file    util_fifo.h
 * @brief   通用环形缓冲区（字节级 FIFO）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    并发约定：本实现**不含任何内部锁**。
 *          - 单线程使用：直接调用即可。
 *          - 多线程使用：调用方必须自行加锁保护同一个 util_fifo_t 实例。
 *          head/tail 为自由增长计数器（依赖 size 为 2 的幂 + 无符号回绕），
 *          当前未声明为 volatile/atomic，因此**不可**当作无锁单生产者单消费者
 *          队列跨线程使用；若后续确有此需求，需先将两个下标改为原子类型
 *          并补充内存序约束，再更新本说明。
 */

#ifndef UTIL_FIFO_H
#define UTIL_FIFO_H

#include "common/sw_error.h"
#include "common/sw_types.h"

/* -------------------------------------------------------------------------
 * FIFO 控制块（调用方负责提供 buf 内存，大小必须为 2 的幂）
 * ------------------------------------------------------------------------- */
typedef struct {
    uint8_t *p_buf; /* 缓冲区指针 */
    uint32_t size;  /* 缓冲区大小（字节，必须为 2 的幂）*/
    uint32_t head;  /* 读指针 */
    uint32_t tail;  /* 写指针 */
} util_fifo_t;

/* -------------------------------------------------------------------------
 * 接口声明
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化 FIFO
 * @param  p_fifo  FIFO 控制块指针
 * @param  p_buf   缓冲区指针（大小必须为 2 的幂）
 * @param  size    缓冲区大小（字节）
 * @retval SW_OK / SW_ERR_PARAM
 */
sw_err_t util_fifo_init(util_fifo_t *p_fifo, uint8_t *p_buf, uint32_t size);

/**
 * @brief  写入一个字节
 * @retval SW_OK / SW_ERR_OVERFLOW
 */
sw_err_t util_fifo_put(util_fifo_t *p_fifo, uint8_t byte);

/**
 * @brief  读取一个字节
 * @retval SW_OK / SW_ERR_TIMEOUT（队列为空）
 */
sw_err_t util_fifo_get(util_fifo_t *p_fifo, uint8_t *p_byte);

/**
 * @brief  批量写入数据
 * @param  p_data  源数据指针
 * @param  len     写入长度
 * @retval 实际写入字节数
 */
uint32_t util_fifo_write(util_fifo_t *p_fifo, const uint8_t *p_data, uint32_t len);

/**
 * @brief  批量读取数据
 * @param  p_data  目标缓冲区指针
 * @param  len     期望读取长度
 * @retval 实际读取字节数
 */
uint32_t util_fifo_read(util_fifo_t *p_fifo, uint8_t *p_data, uint32_t len);

/**
 * @brief  获取当前可读字节数
 */
uint32_t util_fifo_used(const util_fifo_t *p_fifo);

/**
 * @brief  获取当前剩余可写字节数
 */
uint32_t util_fifo_free(const util_fifo_t *p_fifo);

/**
 * @brief  清空 FIFO
 */
void util_fifo_flush(util_fifo_t *p_fifo);

#endif /* UTIL_FIFO_H */
