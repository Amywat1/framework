/**
 * @file    order_book.h
 * @brief   内存订单簿（FIFO，掉电丢弃）
 */

#ifndef DOMAIN_JOB_ORDER_BOOK_H
#define DOMAIN_JOB_ORDER_BOOK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/job/job_types.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  初始化空订单簿并复位编号
 */
void order_book_init(void);

/**
 * @brief  将一笔订单追加到队尾
 * @param  order    入参；order_id 为 0 时由本模块分配
 * @param  out_id   实际入簿的 order_id，可为 NULL
 * @retval SW_OK           已入簿
 * @retval SW_ERR_PARAM    order 为空
 * @retval SW_ERR_OVERFLOW 已达 profile 上限
 */
sw_err_t order_book_enqueue(const job_desc_t *order, uint64_t *out_id);

/**
 * @brief  按订单号取消未放行订单
 * @retval SW_OK        已删除
 * @retval SW_ERR_PARAM 订单号为 0
 * @retval SW_ERR_STATE 簿中无此单
 */
sw_err_t order_book_cancel(uint64_t order_id);

/**
 * @brief  弹出队首
 * @retval SW_OK        已写入 out
 * @retval SW_ERR_PARAM out 为空
 * @retval SW_ERR_STATE 簿空
 */
sw_err_t order_book_pop_front(job_desc_t *out);

/**
 * @brief  窥视队首，不弹出
 */
sw_err_t order_book_peek_front(job_desc_t *out);

/**
 * @brief  清空全部未放行订单
 */
void order_book_clear(void);

/**
 * @brief  当前未放行订单数
 */
unsigned order_book_count(void);

/**
 * @brief  测试复位
 */
void order_book_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_JOB_ORDER_BOOK_H */
