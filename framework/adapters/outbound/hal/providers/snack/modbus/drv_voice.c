/**
 * @file    drv_voice.c
 * @brief   语音模块驱动实现（Modbus RTU，按需通讯）
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#include "drv_voice.h"

#include "framework/common/log.h"
#include "modbus/modbus-rtu.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * 寄存器地址（按厂家区分；切换厂家时改下方别名）
 * ------------------------------------------------------------------------- */
#define VOICE_VENDOR_REG_VOLUME   0x0002U /**< 模块音量（绝对值） */
#define VOICE_VENDOR_REG_PLAY     0x0004U /**< 语音播放（写入曲目编号） */
#define VOICE_VENDOR_REG_VOL_UP   0x0005U /**< 音量增加（写触发值） */
#define VOICE_VENDOR_REG_VOL_DOWN 0x0006U /**< 音量减小（写触发值） */
#define VOICE_VENDOR_REG_PAUSE    0x0009U /**< 暂停（写触发值） */
#define VOICE_VENDOR_REG_STOP     0x000AU /**< 停止播放并清空列表（写触发值） */

#define VOICE_REG_VOLUME   VOICE_VENDOR_REG_VOLUME
#define VOICE_REG_PLAY     VOICE_VENDOR_REG_PLAY
#define VOICE_REG_VOL_UP   VOICE_VENDOR_REG_VOL_UP
#define VOICE_REG_VOL_DOWN VOICE_VENDOR_REG_VOL_DOWN
#define VOICE_REG_PAUSE    VOICE_VENDOR_REG_PAUSE
#define VOICE_REG_STOP     VOICE_VENDOR_REG_STOP

/* 动作寄存器触发值（stop/pause/volume_up/volume_down 均使用此值） */
#define VOICE_CMD_TRIGGER 0x0001U

#define VOICE_MODBUS_TIMEOUT_US   100000U /**< Modbus 响应超时 100ms */
#define VOICE_COMM_FAIL_NOTIFY    3U      /**< 连续失败 N 次后通知通信丢失 */
#define VOICE_COMM_FAIL_RECONNECT 10U     /**< 连续失败 N 次后重建 Modbus 连接 */

/* -------------------------------------------------------------------------
 * 基础辅助
 * ------------------------------------------------------------------------- */
static bool voice_is_initialized(const drv_voice_t *v)
{
    return (v != NULL) && (v->serial_port != NULL) && (v->baud > 0) && (v->modbus_addr > 0);
}

/* -------------------------------------------------------------------------
 * Modbus 连接管理
 * ------------------------------------------------------------------------- */
static sw_err_t voice_mb_ctx_create(drv_voice_t *v)
{
    if (v->mb != NULL) {
        modbus_close(v->mb);
        modbus_free(v->mb);
        v->mb = NULL;
    }
    v->mb_connected = false;

    v->mb = modbus_new_rtu(v->serial_port, v->baud, 'N', 8, 1);
    if (v->mb == NULL) {
        LOG_ERROR("drv_voice[addr=%d]: modbus_new_rtu failed", v->modbus_addr);
        return SW_ERR_HW;
    }
    modbus_set_slave(v->mb, v->modbus_addr);
    modbus_set_response_timeout(v->mb, 0, VOICE_MODBUS_TIMEOUT_US);
    return SW_OK;
}

/* 重建 Modbus 上下文并重新连接，须在 mb_mutex 保护下调用 */
static sw_err_t voice_mb_reconnect_locked(drv_voice_t *v)
{
    sw_err_t ret;

    if (!voice_is_initialized(v)) {
        return SW_ERR_NOT_INIT;
    }
    ret = voice_mb_ctx_create(v);

    if (ret != SW_OK) {
        return ret;
    }
    if (modbus_connect(v->mb) < 0) {
        LOG_ERROR("drv_voice[addr=%d]: modbus_connect failed", v->modbus_addr);
        modbus_free(v->mb);
        v->mb = NULL;
        return SW_ERR_COMM;
    }
    v->mb_connected = true;
    LOG_INFO("drv_voice[addr=%d]: Modbus linked", v->modbus_addr);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 通信状态统计（须在 mb_mutex 保护下调用）
 * ------------------------------------------------------------------------- */
static void voice_on_success_locked(drv_voice_t *v, bool *notify_restored)
{
    if (!v->comm_ok) {
        v->comm_ok = true;
        if (notify_restored != NULL) {
            *notify_restored = true;
        }
    }
    v->comm_fail_count = 0U;
}

static void voice_on_failure_locked(drv_voice_t *v, bool *notify_lost, bool *need_reconnect)
{
    v->mb_connected = false;

    if (v->comm_fail_count < 0xFFFFU) {
        v->comm_fail_count++;
    }

    if ((v->comm_fail_count >= VOICE_COMM_FAIL_NOTIFY) && v->comm_ok) {
        v->comm_ok = false;
        if (notify_lost != NULL) {
            *notify_lost = true;
        }
    }

    if (v->comm_fail_count >= VOICE_COMM_FAIL_RECONNECT) {
        v->comm_fail_count = 0U;
        if (need_reconnect != NULL) {
            *need_reconnect = true;
        }
    }
}

/* -------------------------------------------------------------------------
 * Modbus 写统一执行路径
 * 加锁、重连、错误处理、事件通知均在此完成；回调在锁外触发，避免死锁。
 * ------------------------------------------------------------------------- */
static sw_err_t voice_mb_write(drv_voice_t *v, uint16_t addr, uint16_t val)
{
    int  rc;
    bool notify_lost     = false;
    bool notify_restored = false;
    bool need_reconnect  = false;
    void (*cb)(int);

    if (!voice_is_initialized(v)) {
        return SW_ERR_NOT_INIT;
    }

    (void)pthread_mutex_lock(&v->mb_mutex);

    if (!v->mb_connected && (voice_mb_reconnect_locked(v) != SW_OK)) {
        voice_on_failure_locked(v, &notify_lost, &need_reconnect);
        cb = v->event_cb;
        (void)pthread_mutex_unlock(&v->mb_mutex);
        if (notify_lost && (cb != NULL)) {
            cb(DRV_VOICE_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    rc = modbus_write_register(v->mb, (int)addr, (int)val);

    if (rc < 0) {
        voice_on_failure_locked(v, &notify_lost, &need_reconnect);
        if (need_reconnect && (voice_mb_reconnect_locked(v) != SW_OK)) {
            LOG_WARN("drv_voice[addr=%d]: reconnect failed", v->modbus_addr);
        }
        cb = v->event_cb;
        (void)pthread_mutex_unlock(&v->mb_mutex);
        LOG_ERROR("drv_voice[addr=%d]: write reg 0x%04X failed", v->modbus_addr, addr);
        if (notify_lost && (cb != NULL)) {
            cb(DRV_VOICE_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    voice_on_success_locked(v, &notify_restored);
    cb = v->event_cb;
    (void)pthread_mutex_unlock(&v->mb_mutex);
    if (notify_restored && (cb != NULL)) {
        cb(DRV_VOICE_EVT_COMM_RESTORED);
    }
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_voice_init(drv_voice_t *v, const char *serial_port, int baud, int modbus_addr)
{
    sw_err_t ret;

    if ((v == NULL) || (serial_port == NULL) || (modbus_addr < 1) || (modbus_addr > 247)) {
        return SW_ERR_PARAM;
    }

    (void)memset(v, 0, sizeof(*v));

    if (pthread_mutex_init(&v->mb_mutex, NULL) != 0) {
        LOG_ERROR("drv_voice_init[addr=%d]: mutex init failed", modbus_addr);
        return SW_ERR_HW;
    }

    v->serial_port = serial_port;
    v->baud        = baud;
    v->modbus_addr = modbus_addr;
    v->comm_ok     = true;

    ret = voice_mb_ctx_create(v);
    if (ret != SW_OK) {
        (void)pthread_mutex_destroy(&v->mb_mutex);
        v->serial_port = NULL; /* 清零，确保 voice_is_initialized 返回 false */
        return ret;
    }

    /* connect 失败走 defer-link，不中止 init；
     * 保留 comm_ok=true，避免首次成功时触发孤立的 COMM_RESTORED 事件；
     * COMM_LOST 由失败计数达到阈值后在操作调用点正常触发 */
    if (modbus_connect(v->mb) < 0) {
        LOG_WARN("drv_voice_init[addr=%d]: modbus_connect failed, defer link", modbus_addr);
    } else {
        v->mb_connected = true;
    }

    LOG_INFO("drv_voice_init[addr=%d] ok", modbus_addr);
    return SW_OK;
}

sw_err_t drv_voice_play(drv_voice_t *v, uint16_t track)
{
    return voice_mb_write(v, VOICE_REG_PLAY, track);
}

sw_err_t drv_voice_stop(drv_voice_t *v)
{
    return voice_mb_write(v, VOICE_REG_STOP, VOICE_CMD_TRIGGER);
}

sw_err_t drv_voice_pause(drv_voice_t *v)
{
    return voice_mb_write(v, VOICE_REG_PAUSE, VOICE_CMD_TRIGGER);
}

sw_err_t drv_voice_set_volume(drv_voice_t *v, uint16_t vol)
{
    return voice_mb_write(v, VOICE_REG_VOLUME, vol);
}

sw_err_t drv_voice_volume_up(drv_voice_t *v)
{
    return voice_mb_write(v, VOICE_REG_VOL_UP, VOICE_CMD_TRIGGER);
}

sw_err_t drv_voice_volume_down(drv_voice_t *v)
{
    return voice_mb_write(v, VOICE_REG_VOL_DOWN, VOICE_CMD_TRIGGER);
}

void drv_voice_register_event_cb(drv_voice_t *v, void (*cb)(int event_code))
{
    if (v != NULL) {
        v->event_cb = cb;
    }
}
