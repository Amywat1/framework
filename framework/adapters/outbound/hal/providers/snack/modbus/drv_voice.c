/**
 * @file    drv_voice.c
 * @brief   语音模块驱动实现（Modbus RTU，按需通讯）
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#include "drv_voice.h"

#include "drv_modbus_link.h"
#include "framework/common/log.h"

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
 * Modbus 写 + 通信丢失/恢复通知
 * 实际收发与失败重连交给 drv_modbus_link；本文件只在其结果之上维护
 * comm_ok/notify_fail_count 这层业务通知语义，在 notify_mutex 保护下更新，
 * 回调在锁外触发，避免死锁。
 * ------------------------------------------------------------------------- */
static sw_err_t voice_mb_write(drv_voice_t *v, uint16_t addr, uint16_t val)
{
    sw_err_t ret;
    bool     notify_lost     = false;
    bool     notify_restored = false;
    void   (*cb)(int) = NULL;

    if (!drv_modbus_link_is_ready(&v->link)) {
        return SW_ERR_NOT_INIT;
    }

    ret = drv_modbus_link_write_reg(&v->link, addr, val);

    (void)pthread_mutex_lock(&v->notify_mutex);
    if (ret == SW_OK) {
        if (!v->comm_ok) {
            v->comm_ok      = true;
            notify_restored = true;
        }
        v->notify_fail_count = 0U;
    } else if (ret == SW_ERR_COMM) {
        if (v->notify_fail_count < 0xFFFFU) {
            v->notify_fail_count++;
        }
        if ((v->notify_fail_count >= VOICE_COMM_FAIL_NOTIFY) && v->comm_ok) {
            v->comm_ok  = false;
            notify_lost = true;
        }
    }
    cb = v->event_cb;
    (void)pthread_mutex_unlock(&v->notify_mutex);

    if (ret == SW_ERR_COMM) {
        LOG_ERROR("drv_voice[addr=%d]: write reg 0x%04X failed", v->link.modbus_addr, addr);
    }
    if (notify_restored && (cb != NULL)) {
        cb(DRV_VOICE_EVT_COMM_RESTORED);
    }
    if (notify_lost && (cb != NULL)) {
        cb(DRV_VOICE_EVT_COMM_LOST);
    }
    return ret;
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

    if (pthread_mutex_init(&v->notify_mutex, NULL) != 0) {
        LOG_ERROR("drv_voice_init[addr=%d]: mutex init failed", modbus_addr);
        return SW_ERR_HW;
    }

    ret = drv_modbus_link_init(&v->link, serial_port, baud, modbus_addr,
                               VOICE_MODBUS_TIMEOUT_US, VOICE_COMM_FAIL_RECONNECT);
    if (ret != SW_OK) {
        (void)pthread_mutex_destroy(&v->notify_mutex);
        return ret;
    }

    /* comm_ok 保留 true，避免首次成功时触发孤立的 COMM_RESTORED 事件；
     * COMM_LOST 由失败计数达到阈值后在操作调用点正常触发；
     * notify_fail_count 已被上面的 memset 清零 */
    v->comm_ok = true;

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
