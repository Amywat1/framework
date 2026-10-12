/**
 * @file    order_book.c
 * @brief   内存订单簿实现
 */

#include "domain/job/order_book.h"

#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/job/machine_profile.h"

#include <pthread.h>
#include <string.h>

static job_desc_t      s_items[JOB_ORDER_QUEUE_CAP];
static unsigned        s_head;
static unsigned        s_count;
static uint64_t        s_next_order_id = 1U;
static pthread_mutex_t s_mu;
static pthread_once_t  s_mu_once = PTHREAD_ONCE_INIT;

static void book_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void book_lock(void)
{
    (void)pthread_once(&s_mu_once, book_mu_init);
    pthread_mutex_lock(&s_mu);
}

static void book_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

void order_book_init(void)
{
    book_lock();
    memset(s_items, 0, sizeof(s_items));
    s_head          = 0U;
    s_count         = 0U;
    s_next_order_id = 1U;
    book_unlock();
}

sw_err_t order_book_enqueue(const job_desc_t *order, uint64_t *out_id)
{
    machine_profile_t profile;
    unsigned          tail;
    job_desc_t        item;

    if (order == NULL) {
        return SW_ERR_PARAM;
    }

    profile = machine_profile_get();
    book_lock();
    if (order->order_id != 0U) {
        for (unsigned i = 0U; i < s_count; i++) {
            unsigned idx = (s_head + i) % JOB_ORDER_QUEUE_CAP;

            if (s_items[idx].order_id == order->order_id) {
                if (out_id != NULL) {
                    *out_id = order->order_id;
                }
                book_unlock();
                return SW_OK;
            }
        }
    }
    if (s_count >= (unsigned)profile.max_queued_orders) {
        book_unlock();
        return SW_ERR_OVERFLOW;
    }

    item = *order;
    if (item.order_id == 0U) {
        item.order_id = s_next_order_id++;
    }
    if (item.enqueued_ms == 0U) {
        item.enqueued_ms = time_util_get_ms();
    }
    if (item.correlation_id == 0U) {
        item.correlation_id = item.order_id;
    }
    item.job_id = 0U;

    tail              = (s_head + s_count) % JOB_ORDER_QUEUE_CAP;
    s_items[tail]     = item;
    s_count++;
    if (out_id != NULL) {
        *out_id = item.order_id;
    }
    book_unlock();
    return SW_OK;
}

sw_err_t order_book_cancel(uint64_t order_id)
{
    unsigned i;
    unsigned src;
    unsigned dst;

    if (order_id == 0U) {
        return SW_ERR_PARAM;
    }

    book_lock();
    for (i = 0U; i < s_count; i++) {
        unsigned idx = (s_head + i) % JOB_ORDER_QUEUE_CAP;

        if (s_items[idx].order_id == order_id) {
            for (src = i + 1U, dst = i; src < s_count; src++, dst++) {
                s_items[(s_head + dst) % JOB_ORDER_QUEUE_CAP] =
                    s_items[(s_head + src) % JOB_ORDER_QUEUE_CAP];
            }
            s_count--;
            book_unlock();
            return SW_OK;
        }
    }
    book_unlock();
    return SW_ERR_STATE;
}

sw_err_t order_book_pop_front(job_desc_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    book_lock();
    if (s_count == 0U) {
        book_unlock();
        return SW_ERR_STATE;
    }
    *out   = s_items[s_head];
    s_head = (s_head + 1U) % JOB_ORDER_QUEUE_CAP;
    s_count--;
    book_unlock();
    return SW_OK;
}

sw_err_t order_book_peek_front(job_desc_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    book_lock();
    if (s_count == 0U) {
        book_unlock();
        return SW_ERR_STATE;
    }
    *out = s_items[s_head];
    book_unlock();
    return SW_OK;
}

void order_book_clear(void)
{
    book_lock();
    s_head  = 0U;
    s_count = 0U;
    memset(s_items, 0, sizeof(s_items));
    book_unlock();
}

unsigned order_book_count(void)
{
    unsigned count;

    book_lock();
    count = s_count;
    book_unlock();
    return count;
}

void order_book_reset_for_test(void)
{
    order_book_init();
}
