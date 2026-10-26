/**
 * @file    wash_quota.c
 * @brief   离线开洗次数授权实现
 */

#include "application/wash_quota.h"

#include "common/log.h"
#include "common/sw_mutex.h"
#include "domain/ports/outbound/storage/param_kv.h"

#include <pthread.h>
#include <stdint.h>

static pthread_mutex_t s_mu;
static pthread_once_t  s_mu_once = PTHREAD_ONCE_INIT;
static bool            s_inited;
static bool            s_monitorable;
static uint8_t         s_remaining;
static uint32_t        s_grant_serial;

static void quota_mu_init(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mu);
}

static void quota_lock(void)
{
    (void)pthread_once(&s_mu_once, quota_mu_init);
    pthread_mutex_lock(&s_mu);
}

static void quota_unlock(void)
{
    pthread_mutex_unlock(&s_mu);
}

/**
 * @brief 将内存中的剩余次数与序号写入参数存储
 *
 * @retval SW_OK            已落盘
 * @retval SW_ERR_NOT_INIT  存储未注册（调用方保持内存态）
 * @retval 其它             写入或保存失败
 */
static sw_err_t persist_locked(void)
{
    sw_err_t set_left = param_kv_set_int(PARAM_KEY_WASH_QUOTA_REMAINING, (int)s_remaining);
    sw_err_t set_ser  = param_kv_set_int(PARAM_KEY_WASH_QUOTA_SERIAL, (int)s_grant_serial);
    sw_err_t saved;

    if ((set_left != SW_OK) || (set_ser != SW_OK)) {
        LOG_WARN("wash_quota: persist set failed left=%d ser=%d",
                 (int)set_left,
                 (int)set_ser);
        return (set_left != SW_OK) ? set_left : set_ser;
    }
    saved = param_kv_save();
    if (saved != SW_OK) {
        LOG_WARN("wash_quota: persist save failed ret=%d", (int)saved);
        return saved;
    }
    return SW_OK;
}

static uint8_t clamp_remaining(int raw)
{
    if (raw < 0) {
        return 0U;
    }
    if (raw > (int)WASH_QUOTA_REMAINING_MAX) {
        return (uint8_t)WASH_QUOTA_REMAINING_MAX;
    }
    return (uint8_t)raw;
}

static uint32_t load_serial(int raw)
{
    if (raw < 0) {
        return 0U;
    }
    return (uint32_t)raw;
}

sw_err_t wash_quota_init(void)
{
    quota_lock();
    s_monitorable  = false;
    s_remaining    = clamp_remaining(param_kv_get_int(PARAM_KEY_WASH_QUOTA_REMAINING, 0));
    s_grant_serial = load_serial(param_kv_get_int(PARAM_KEY_WASH_QUOTA_SERIAL, 0));
    s_inited       = true;
    quota_unlock();
    LOG_INFO("wash_quota: init remaining=%u serial=%u",
             (unsigned)s_remaining,
             (unsigned)s_grant_serial);
    return SW_OK;
}

void wash_quota_set_monitorable(bool monitorable)
{
    quota_lock();
    s_monitorable = monitorable;
    quota_unlock();
}

bool wash_quota_is_monitorable(void)
{
    bool monitorable;

    quota_lock();
    monitorable = s_monitorable;
    quota_unlock();
    return monitorable;
}

sw_err_t wash_quota_apply_grant(uint8_t remaining, uint32_t serial)
{
    uint8_t  prev_left;
    uint32_t prev_serial;
    sw_err_t persisted;

    quota_lock();
    if (serial <= s_grant_serial) {
        quota_unlock();
        return SW_ERR_STATE;
    }
    prev_left      = s_remaining;
    prev_serial    = s_grant_serial;
    s_remaining    = remaining;
    s_grant_serial = serial;
    persisted      = persist_locked();
    if ((persisted != SW_OK) && (persisted != SW_ERR_NOT_INIT)) {
        s_remaining    = prev_left;
        s_grant_serial = prev_serial;
        quota_unlock();
        return persisted;
    }
    quota_unlock();
    LOG_INFO("wash_quota: grant remaining=%u serial=%u",
             (unsigned)remaining,
             (unsigned)serial);
    return SW_OK;
}

sw_err_t wash_quota_set_remaining(uint8_t remaining)
{
    wash_quota_info_t info = {0};

    wash_quota_get_info(&info);
    if (info.remaining == remaining) {
        return SW_OK;
    }
    if (info.grant_serial == UINT32_MAX) {
        return SW_ERR_STATE;
    }
    return wash_quota_apply_grant(remaining, info.grant_serial + 1U);
}

sw_err_t wash_quota_check_start(bool *need_consume)
{
    sw_err_t ret;

    if (need_consume == NULL) {
        return SW_ERR_PARAM;
    }

    quota_lock();
    if (!s_inited) {
        *need_consume = false;
        quota_unlock();
        return SW_OK;
    }
    if (s_monitorable) {
        *need_consume = false;
        ret           = SW_OK;
    } else if (s_remaining > 0U) {
        *need_consume = true;
        ret           = SW_OK;
    } else {
        *need_consume = false;
        ret           = SW_ERR_STATE;
    }
    quota_unlock();
    return ret;
}

sw_err_t wash_quota_consume_one(void)
{
    sw_err_t ret = SW_ERR_STATE;

    quota_lock();
    if (s_remaining > 0U) {
        sw_err_t persisted;

        /* 作业已启动，落盘失败也不回退内存，避免本次运行内同一份额被重复放行 */
        s_remaining--;
        persisted = persist_locked();
        if ((persisted != SW_OK) && (persisted != SW_ERR_NOT_INIT)) {
            quota_unlock();
            return persisted;
        }
        ret = SW_OK;
        LOG_INFO("wash_quota: consume remaining=%u", (unsigned)s_remaining);
    }
    quota_unlock();
    return ret;
}

void wash_quota_get_info(wash_quota_info_t *info)
{
    if (info == NULL) {
        return;
    }
    quota_lock();
    info->monitorable  = s_monitorable;
    info->remaining    = s_remaining;
    info->grant_serial = s_grant_serial;
    quota_unlock();
}

void wash_quota_reset_for_test(void)
{
    quota_lock();
    s_inited       = true;
    s_monitorable  = true;
    s_remaining    = 0U;
    s_grant_serial = 0U;
    quota_unlock();
}
