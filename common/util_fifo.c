/**
 * @file    util_fifo.c
 * @brief   通用环形缓冲区实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "util_fifo.h"

/* -------------------------------------------------------------------------
 * 内部辅助：判断 n 是否为 2 的幂
 * ------------------------------------------------------------------------- */
static bool is_power_of_two(uint32_t n)
{
    return (n != 0U) && ((n & (n - 1U)) == 0U);
}

sw_err_t util_fifo_init(util_fifo_t *p_fifo, uint8_t *p_buf, uint32_t size)
{
    if ((p_fifo == NULL) || (p_buf == NULL) || !is_power_of_two(size)) {
        return SW_ERR_PARAM;
    }

    p_fifo->p_buf = p_buf;
    p_fifo->size  = size;
    p_fifo->head  = 0U;
    p_fifo->tail  = 0U;

    return SW_OK;
}

sw_err_t util_fifo_put(util_fifo_t *p_fifo, uint8_t byte)
{
    if (util_fifo_free(p_fifo) == 0U) {
        return SW_ERR_OVERFLOW;
    }

    p_fifo->p_buf[p_fifo->tail & (p_fifo->size - 1U)] = byte;
    p_fifo->tail++;

    return SW_OK;
}

sw_err_t util_fifo_get(util_fifo_t *p_fifo, uint8_t *p_byte)
{
    if (p_byte == NULL) {
        return SW_ERR_PARAM;
    }

    if (util_fifo_used(p_fifo) == 0U) {
        return SW_ERR_TIMEOUT; /* 队列为空，复用 TIMEOUT 表示"暂无数据" */
    }

    *p_byte = p_fifo->p_buf[p_fifo->head & (p_fifo->size - 1U)];
    p_fifo->head++;

    return SW_OK;
}

uint32_t util_fifo_write(util_fifo_t *p_fifo, const uint8_t *p_data, uint32_t len)
{
    uint32_t i;
    uint32_t written = 0U;

    if ((p_fifo == NULL) || (p_data == NULL)) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        if (util_fifo_put(p_fifo, p_data[i]) != SW_OK) {
            break;
        }
        written++;
    }

    return written;
}

uint32_t util_fifo_read(util_fifo_t *p_fifo, uint8_t *p_data, uint32_t len)
{
    uint32_t i;
    uint32_t read_cnt = 0U;

    if ((p_fifo == NULL) || (p_data == NULL)) {
        return 0U;
    }

    for (i = 0U; i < len; i++) {
        if (util_fifo_get(p_fifo, &p_data[i]) != SW_OK) {
            break;
        }
        read_cnt++;
    }

    return read_cnt;
}

uint32_t util_fifo_used(const util_fifo_t *p_fifo)
{
    return p_fifo->tail - p_fifo->head;
}

uint32_t util_fifo_free(const util_fifo_t *p_fifo)
{
    return p_fifo->size - util_fifo_used(p_fifo);
}

void util_fifo_flush(util_fifo_t *p_fifo)
{
    p_fifo->head = 0U;
    p_fifo->tail = 0U;
}
