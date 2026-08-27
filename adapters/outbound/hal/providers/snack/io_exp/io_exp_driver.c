/**
 * @file    io_exp_driver.c
 * @brief   io_exp CAN IO 子板 provider 实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    本文件负责：
 *          - IO 子板在线检测
 *          - 输入缓存刷新
 *          - 输出缓冲写出
 *          - 由本文件唯一 worker 串行执行所有 Snack SDK 调用
 *          - 输入变化调试通知
 *          - 全板离线时的安全停机联动
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"

#include "adapters/outbound/hal/components/adc_gate/hal_adc_gate.h"
#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "io_exp/demo.h"
#include "io_exp/slave.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int io_exp_sdk_log(const char *fmt, ...)
{
    char    buf[512];
    va_list va;

    if (fmt == NULL) {
        return 0;
    }

    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    LOG_INFO("io_exp: %s", buf);
    return 0;
}

sw_err_t io_exp_driver_sdk_init(const char *can_bus, int can_baud, int self_node, int board_count)
{
    if ((can_bus == NULL) || (board_count <= 0)) {
        return SW_ERR_PARAM;
    }

    io_logApi_set(io_exp_sdk_log);
    return (io_init(can_bus, can_baud, self_node, board_count) == 0) ? SW_OK : SW_ERR_HW;
}

/* -------------------------------------------------------------------------
 * 内部常量
 * ------------------------------------------------------------------------- */
#define IO_BOARD_MAX                  7U                /* 最大子板数，含 0 号占位 */
#define IO_PIN_COUNT_MAX              32U               /* 每块子板 IO 点数上限，仅用于静态数组维度 */
#define IO_UPDATE_FREQ_MS             30U               /* 输入/输出缓冲刷新周期（ms） */
#define IO_CHECK_PROBING_MS           IO_UPDATE_FREQ_MS /* 启动和恢复确认期间快速探测 */
#define IO_CHECK_OFFLINE_MS           300U              /* 全部在线时的在线检测间隔（ms） */
#define IO_CHECK_ONLINE_MS            2000U             /* 存在掉线子板时的重连检测间隔（ms） */
#define IO_OFFLINE_CNT                3U                /* 连续无响应次数达到该值后判定掉线 */
#define IO_ONLINE_CNT                 IO_OFFLINE_CNT    /* 连续响应次数达到该值后确认上线（对称防抖）*/
#define IO_STARTUP_SAFE_STOP_DELAY_MS 3000U             /* 上电无板时执行安全停机的等待时间 */
#define IO_PDO_BOARD_MAX              4                 /* Snack SDK：最多 4 块子板统一使用 PDO */
#define IO_TRANSACTION_TIMEOUT_MS     300U              /* 单次同步事务等待上限 */

typedef enum {
    DRV_IO_TRANSACTION_FLUSH_OUTPUTS = 0,
    DRV_IO_TRANSACTION_PULSE_READ,
    DRV_IO_TRANSACTION_PULSE_CLEAR
} drv_io_transaction_type_t;

typedef struct {
    drv_io_transaction_type_t type;
    int                       board_id;
    int                       channel;
} drv_io_transaction_t;

typedef struct {
    int value;
} drv_io_transaction_result_t;

sw_err_t drv_io_cfg_validate(const drv_io_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->can_bus == NULL) || (cfg->board_count <= 0) || (cfg->board_count >= (int)IO_BOARD_MAX)
        || (cfg->pin_count <= 0) || (cfg->pin_count > (int)IO_PIN_COUNT_MAX)) {
        return SW_ERR_PARAM;
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static unsigned int        s_input_buf[IO_BOARD_MAX]    = {0};
static unsigned int        s_output_buf[IO_BOARD_MAX]   = {0};
static bool                s_output_dirty[IO_BOARD_MAX] = {false};
static bool                s_board_online[IO_BOARD_MAX] = {0};
static io_sample_quality_t s_input_quality[IO_BOARD_MAX];
static uint32_t            s_input_sequence[IO_BOARD_MAX];
/* s_output_mutex 位于急停切断热路径：safety_cutout_execute -> 项目 cutout 实现
 * -> hal_io do_set -> 本驱动写输出缓冲。该路径由 estop_poll 线程以 SCHED_FIFO
 * 高优先级执行，而两把锁又被 SCHED_OTHER 周期任务（CAN 刷新、输入采样）竞争，
 * 故启用优先级继承。用 pthread_once 而非在 drv_io_init 中初始化：本驱动的
 * getter 类接口允许在 init 之前被调用，锁必须在首次使用前就绪。 */
static pthread_mutex_t s_input_mutex;
static pthread_mutex_t s_output_mutex;
static pthread_mutex_t s_worker_mutex;
static pthread_mutex_t s_adc_mutex;
static pthread_cond_t  s_worker_cond;
static bool            s_worker_cond_monotonic;
static pthread_once_t  s_mutex_once = PTHREAD_ONCE_INIT;

static hal_io_stats_t s_stats[IO_BOARD_MAX]       = {{0}};
static bool           s_seen_online[IO_BOARD_MAX] = {false};

typedef struct {
    int                 raw;
    int                 millivolt;
    int                 milliamp;
    io_sample_quality_t quality;
    uint64_t            timestamp_ms;
} drv_io_adc_slot_t;

static drv_io_adc_slot_t s_adc[IO_BOARD_MAX][DRV_IO_ADC_PORT_MAX + 1];

static void drv_io_mutex_init_once(void)
{
    pthread_condattr_t attr;
    int                ret;

    (void)sw_mutex_init_prio_inherit(&s_input_mutex);
    (void)sw_mutex_init_prio_inherit(&s_output_mutex);
    (void)sw_mutex_init_prio_inherit(&s_worker_mutex);
    (void)sw_mutex_init_prio_inherit(&s_adc_mutex);

    s_worker_cond_monotonic = false;
    ret                     = pthread_condattr_init(&attr);
    if (ret == 0) {
        if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0) {
            if (pthread_cond_init(&s_worker_cond, &attr) == 0) {
                s_worker_cond_monotonic = true;
            }
        }
        (void)pthread_condattr_destroy(&attr);
    }
    if (!s_worker_cond_monotonic) {
        (void)pthread_cond_init(&s_worker_cond, NULL);
        LOG_WARN("drv_io: worker cond CLOCK_MONOTONIC unavailable, fallback REALTIME");
    }
}

/** @brief 确保互斥量与条件变量已初始化（幂等，所有加锁点入口调用）*/
static void drv_io_mutexes_ready(void)
{
    (void)pthread_once(&s_mutex_once, drv_io_mutex_init_once);
}

/* 运行时配置（由 drv_io_init 写入，后续只读） */
static int                        s_board_count = 0;
static int                        s_pin_count   = 0;
static const drv_io_name_entry_t *s_di_table    = NULL;
static size_t                     s_di_count    = 0U;
static const drv_io_name_entry_t *s_do_table    = NULL;
static size_t                     s_do_count    = 0U;

/* 调试测试覆盖：索引直接使用 pin_id，因此第二维保留 0 号位不用 */
static bool s_test_enable[IO_BOARD_MAX][IO_PIN_COUNT_MAX + 1U] = {{false}};
static bool s_test_value[IO_BOARD_MAX][IO_PIN_COUNT_MAX + 1U]  = {{false}};

/* 调试 / 状态回调 */
static drv_io_debug_input_cb_t s_debug_input_cb             = NULL;
static void (*s_board_error_cb)(int board_id, bool offline) = NULL;
static void (*s_panic_cb)(void)                             = NULL;
static bool                    s_io_rw_started              = false;
static drv_io_transport_mode_t s_transport_mode             = DRV_IO_TRANSPORT_SDO;

/* worker 独占的轮询状态。 */
static uint16_t s_poll_loop_count;
static uint8_t  s_poll_offline_count[IO_BOARD_MAX];
static uint8_t  s_poll_online_count[IO_BOARD_MAX];
static bool     s_poll_offline_confirmed[IO_BOARD_MAX];
static int      s_poll_check_interval_ms;
static uint64_t s_poll_startup_ms;
static bool     s_poll_all_offline_action_done;

typedef enum {
    DRV_IO_MAILBOX_IDLE = 0,
    DRV_IO_MAILBOX_PENDING,
    DRV_IO_MAILBOX_EXECUTING,
    DRV_IO_MAILBOX_DONE
} drv_io_mailbox_state_t;

static pthread_t                   s_worker;
static bool                        s_worker_created;
static bool                        s_stop_requested;
static drv_io_mailbox_state_t      s_mailbox_state;
static bool                        s_mailbox_waiter_gone;
static drv_io_transaction_t        s_mailbox_job;
static drv_io_transaction_result_t s_mailbox_result;
static sw_err_t                    s_mailbox_err;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */
static bool drv_io_is_valid_di_raw(uint16_t raw)
{
    int board_id = (int)io_handle_board(raw);
    int pin_id   = (int)io_handle_pin(raw);

    return (io_handle_kind(raw) == IO_KIND_DI) && (board_id > 0) && (board_id <= s_board_count) && (pin_id > 0)
           && (pin_id <= s_pin_count);
}

static bool drv_io_is_valid_do_raw(uint16_t raw)
{
    int board_id = (int)io_handle_board(raw);
    int pin_id   = (int)io_handle_pin(raw);

    return (io_handle_kind(raw) == IO_KIND_DO) && (board_id > 0) && (board_id <= s_board_count) && (pin_id > 0)
           && (pin_id <= s_pin_count);
}

static bool drv_io_name_matches(const char *input, const char *canonical)
{
    if ((input == NULL) || (canonical == NULL)) {
        return false;
    }

    if (strcmp(input, canonical) == 0) {
        return true;
    }

    /* 允许省略 DI_/DO_ 前缀，只输入核心名字 */
    if (strlen(canonical) <= 3U) {
        return false;
    }
    if ((strncmp(canonical, "DI_", 3) != 0) && (strncmp(canonical, "DO_", 3) != 0)) {
        return false;
    }
    return strcmp(input, canonical + 3) == 0;
}

static bool drv_io_find_name(const drv_io_name_entry_t *table, size_t table_size, const char *name, uint16_t *out_raw)
{
    if ((table == NULL) || (name == NULL) || (out_raw == NULL)) {
        return false;
    }

    for (size_t i = 0; i < table_size; ++i) {
        if (drv_io_name_matches(name, table[i].name)) {
            *out_raw = table[i].raw;
            return true;
        }
    }

    return false;
}

static const char *drv_io_find_canonical_name(const drv_io_name_entry_t *table, size_t table_size, uint16_t raw)
{
    if (table == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < table_size; ++i) {
        if (table[i].raw == raw) {
            return table[i].name;
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * 名称解析 / 可读名称
 * ------------------------------------------------------------------------- */
bool drv_io_try_parse_di(const char *name, io_di_t *out)
{
    uint16_t raw = IO_HANDLE_NULL;

    if (!drv_io_find_name(s_di_table, s_di_count, name, &raw)) {
        return false;
    }

    if (out != NULL) {
        out->raw = raw;
    }

    return true;
}

bool drv_io_try_parse_do(const char *name, io_do_t *out)
{
    uint16_t raw = IO_HANDLE_NULL;

    if (!drv_io_find_name(s_do_table, s_do_count, name, &raw)) {
        return false;
    }

    if (out != NULL) {
        out->raw = raw;
    }

    return true;
}

const char *drv_io_di_name(io_di_t pin)
{
    return drv_io_find_canonical_name(s_di_table, s_di_count, io_di_raw(pin));
}

const char *drv_io_do_name(io_do_t pin)
{
    return drv_io_find_canonical_name(s_do_table, s_do_count, io_do_raw(pin));
}

/* -------------------------------------------------------------------------
 * IO 读写后台线程（内部辅助）
 * ------------------------------------------------------------------------- */

/* 检测到在线时的单板状态处理（含上线防抖） */
static void poll_handle_detected_online(int id, uint8_t online_cnt[], uint8_t offline_cnt[], bool offline_confirmed[])
{
    bool already_online;

    offline_cnt[id] = 0U;

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_input_mutex);
    already_online = s_board_online[id];
    pthread_mutex_unlock(&s_input_mutex);
    if (already_online) {
        online_cnt[id] = 0U;
        return;
    }

    ++online_cnt[id];
    if (online_cnt[id] < (uint8_t)IO_ONLINE_CNT) {
        return;
    }

    /* 连续在线次数达到阈值：确认上线 */
    {
        uint64_t now_ms = time_util_get_ms();
        bool     recovered;

        online_cnt[id]        = 0U;
        offline_confirmed[id] = false; /* 连续确认后才解除已确认离线状态 */
        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_input_mutex);
        recovered                  = s_seen_online[id];
        s_board_online[id]         = true;
        s_input_quality[id]        = IO_SAMPLE_QUALITY_PROBING;
        s_stats[id].online         = true;
        s_stats[id].last_online_ms = now_ms;
        if (recovered) {
            s_stats[id].online_recover_count++;
        } else {
            s_seen_online[id] = true;
        }
        pthread_mutex_unlock(&s_input_mutex);

        /* 子板离线期间硬件输出可能丢失，恢复后强制重发 */
        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_output_mutex);
        s_output_dirty[id]        = true;
        s_stats[id].dirty_pending = true;
        pthread_mutex_unlock(&s_output_mutex);

        if (s_board_error_cb != NULL) {
            s_board_error_cb(id, false);
        }
        LOG_INFO("drv_io: board %d online, force resend outputs", id);
    }
}

/* 检测到离线时的单板状态处理（含下线防抖） */
static void poll_handle_detected_offline(int id, uint8_t online_cnt[], uint8_t offline_cnt[], bool offline_confirmed[])
{
    online_cnt[id] = 0U;

    if (offline_cnt[id] >= (uint8_t)IO_OFFLINE_CNT) {
        return; /* 已达阈值，避免重复触发回调和计数 */
    }

    ++offline_cnt[id];
    if (offline_cnt[id] < (uint8_t)IO_OFFLINE_CNT) {
        return;
    }

    /* 连续离线次数达到阈值：确认下线（已确认则不重复触发回调，
     * 仅在抖动重置计数后重新积累到阈值时会到达此处）*/
    if (!offline_confirmed[id]) {
        uint64_t now_ms = time_util_get_ms();

        offline_confirmed[id] = true;
        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_input_mutex);
        s_board_online[id]  = false;
        s_input_quality[id] = IO_SAMPLE_QUALITY_OFFLINE;
        s_stats[id].online  = false;
        s_stats[id].offline_count++;
        s_stats[id].last_offline_ms = now_ms;
        pthread_mutex_unlock(&s_input_mutex);

        if (s_board_error_cb != NULL) {
            s_board_error_cb(id, true);
        }
        LOG_ERROR("drv_io: board %d offline", id);
    }
}

/* 单板在线状态轮询：根据 SDK 探测结果分发到上线/下线处理 */
static void poll_check_board(int id, uint8_t online_cnt[], uint8_t offline_cnt[], bool offline_confirmed[])
{
    if (io_online_get(id) > 0) {
        poll_handle_detected_online(id, online_cnt, offline_cnt, offline_confirmed);
    } else {
        poll_handle_detected_offline(id, online_cnt, offline_cnt, offline_confirmed);
    }
}

static sw_err_t snack_read_input(int board_id, unsigned int *out)
{
    int raw;

    if (s_transport_mode == DRV_IO_TRANSPORT_PDO) {
        *out = io_read_input(board_id);
        return SW_OK;
    }
    raw = io_read_input_s(board_id);
    if (raw < 0) {
        return SW_ERR_COMM;
    }
    *out = (unsigned int)raw;
    return SW_OK;
}

static sw_err_t snack_write_output(int board_id, unsigned int value)
{
    int ret;

    if (s_transport_mode == DRV_IO_TRANSPORT_PDO) {
        ret = io_write_all(board_id, (int)value);
    } else {
        ret = io_write_all_s(board_id, (int)value);
    }
    return (ret >= 0) ? SW_OK : SW_ERR_COMM;
}

/* 单块在线子板的输入刷新 + 输出落地 */
static void poll_rw_board(int id)
{
    unsigned int prev;
    unsigned int input    = 0U;
    unsigned int out_val  = 0U;
    uint64_t     now_ms;
    bool         dirty    = false;
    bool         input_ok;

    input_ok = (snack_read_input(id, &input) == SW_OK);
    now_ms   = time_util_get_ms();
    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_input_mutex);
    prev = s_input_buf[id];
    if (input_ok) {
        s_input_buf[id]     = input;
        s_input_quality[id] = IO_SAMPLE_QUALITY_VALID;
        s_input_sequence[id]++;
        s_stats[id].online = true;
        s_stats[id].input_refresh_count++;
        s_stats[id].last_input_refresh_ms = now_ms;
        s_stats[id].last_input_snapshot   = input;
    } else if (s_input_quality[id] == IO_SAMPLE_QUALITY_VALID) {
        s_input_quality[id] = IO_SAMPLE_QUALITY_STALE;
    }
    pthread_mutex_unlock(&s_input_mutex);

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_output_mutex);
    dirty   = s_output_dirty[id];
    out_val = s_output_buf[id];
    if (dirty) {
        s_output_dirty[id] = false;
    }
    s_stats[id].dirty_pending = s_output_dirty[id];
    pthread_mutex_unlock(&s_output_mutex);

    if (dirty) {
        sw_err_t write_ret = snack_write_output(id, out_val);

        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_output_mutex);
        if (write_ret == SW_OK) {
            now_ms = time_util_get_ms();
            s_stats[id].output_flush_count++;
            s_stats[id].last_output_flush_ms = now_ms;
            s_stats[id].last_output_snapshot = out_val;
        } else {
            s_output_dirty[id] = true;
        }
        s_stats[id].dirty_pending = s_output_dirty[id];
        pthread_mutex_unlock(&s_output_mutex);
    }

    /* 输入变化调试通知，仅用于观察，不参与正式业务判断 */
    if (input_ok && (s_debug_input_cb != NULL)) {
        unsigned int changed = prev ^ input;
        for (int bit = 0; bit < s_pin_count; ++bit) {
            if (((changed >> bit) & 1U) != 0U) {
                io_di_t pin       = io_di_make((uint16_t)id, (uint16_t)(bit + 1));
                bool    new_state = (bool)((input >> bit) & 1U);
                s_debug_input_cb(pin, new_state);
            }
        }
    }
}

/* 全板确认离线时的安全停机处理 */
static void poll_all_offline_safe_stop_and_abort(void)
{
    LOG_ERROR("drv_io: all boards offline, safe stop then abort");

    /* panic_cb（项目 assert_safe_outputs 实现）通过 drv_io_do_set 把缓冲设为安全态，
     * 不负责 flush；flush 由驱动在此处统一执行，保证一定能写到硬件。*/
    if (s_panic_cb != NULL) {
        s_panic_cb();
    }

    /* 快照 panic_cb 写入的安全态缓冲，无条件写入全部子板（不受 s_board_online 限制）。
     * 若离线判定为误判且 CAN 仍可达，此次写入将硬件置于安全态；
     * 若 CAN 真的断开，写入失败无副作用。*/
    {
        unsigned int snapshot[IO_BOARD_MAX] = {0U};
        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_output_mutex);
        for (int i = 1; i <= s_board_count; ++i) {
            snapshot[i] = s_output_buf[i];
        }
        pthread_mutex_unlock(&s_output_mutex);
        for (int i = 1; i <= s_board_count; ++i) {
            (void)snack_write_output(i, snapshot[i]);
        }
    }
    abort();
}

/**
 * @brief  将某子板全部 ADC 槽位置为给定质量（掉线时）
 */
static void poll_adc_mark_board(int board_id, io_sample_quality_t quality)
{
    int port;

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_adc_mutex);
    for (port = DRV_IO_ADC_PORT_MIN; port <= DRV_IO_ADC_PORT_MAX; ++port) {
        s_adc[board_id][port].quality = quality;
    }
    pthread_mutex_unlock(&s_adc_mutex);
}

/**
 * @brief  按门控采集在线子板的 ADC，写入快照（锁外 SDO）
 */
static void poll_adc_board(int board_id)
{
    int      port;
    uint64_t now_ms;

    for (port = DRV_IO_ADC_PORT_MIN; port <= DRV_IO_ADC_PORT_MAX; ++port) {
        int raw;
        int millivolt;
        int milliamp;

        if (!hal_adc_gate_is_needed(board_id, port)) {
            continue;
        }

        raw       = io_adc_read(board_id, port);
        millivolt = io_adc_mV(board_id, port);
        milliamp  = io_adc_mA(board_id, port);
        now_ms    = time_util_get_ms();

        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_adc_mutex);
        if (raw < 0) {
            if (s_adc[board_id][port].quality == IO_SAMPLE_QUALITY_VALID) {
                s_adc[board_id][port].quality = IO_SAMPLE_QUALITY_STALE;
            }
        } else {
            s_adc[board_id][port].raw           = raw;
            s_adc[board_id][port].millivolt     = (millivolt < 0) ? 0 : millivolt;
            s_adc[board_id][port].milliamp      = (milliamp < 0) ? 0 : milliamp;
            s_adc[board_id][port].quality       = IO_SAMPLE_QUALITY_VALID;
            s_adc[board_id][port].timestamp_ms  = now_ms;
        }
        pthread_mutex_unlock(&s_adc_mutex);
    }
}

/* -------------------------------------------------------------------------
 * worker 周期入口
 * ------------------------------------------------------------------------- */
static void drv_io_poll_tick(void)
{
    int  online_count            = 0;
    int  offline_confirmed_count = 0;
    bool fast_probe_needed       = false;
    int  check_loops             = s_poll_check_interval_ms / IO_UPDATE_FREQ_MS;

    if (check_loops < 1) {
        check_loops = 1;
    }

    ++s_poll_loop_count;
    for (int i = 1; i <= s_board_count; ++i) {
        if ((s_poll_loop_count % (uint16_t)check_loops) == 0U) {
            poll_check_board(i, s_poll_online_count, s_poll_offline_count, s_poll_offline_confirmed);
        }

        if (drv_io_board_is_online(i)) {
            poll_rw_board(i);
            poll_adc_board(i);
            ++online_count;
        } else if (s_poll_offline_confirmed[i]) {
            poll_adc_mark_board(i, IO_SAMPLE_QUALITY_OFFLINE);
            ++offline_confirmed_count;
            if (s_poll_online_count[i] > 0U) {
                fast_probe_needed = true;
            }
        } else {
            fast_probe_needed = true;
        }
    }

    if (offline_confirmed_count == s_board_count) {
        if (!s_poll_all_offline_action_done
            && (time_elapsed_ms(s_poll_startup_ms, time_util_get_ms()) >= IO_STARTUP_SAFE_STOP_DELAY_MS)) {
            s_poll_all_offline_action_done = true;
            poll_all_offline_safe_stop_and_abort();
        }
    } else {
        s_poll_all_offline_action_done = false;
    }

    if (online_count == s_board_count) {
        s_poll_check_interval_ms = IO_CHECK_OFFLINE_MS;
    } else if (fast_probe_needed) {
        s_poll_check_interval_ms = IO_CHECK_PROBING_MS;
    } else {
        s_poll_check_interval_ms = IO_CHECK_ONLINE_MS;
    }
}

static sw_err_t drv_io_flush_outputs_backend(void)
{
    unsigned int snapshot[IO_BOARD_MAX] = {0U};
    sw_err_t     first_error            = SW_OK;

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_output_mutex);
    for (int i = 1; i <= s_board_count; ++i) {
        snapshot[i] = s_output_buf[i];
    }
    pthread_mutex_unlock(&s_output_mutex);

    for (int i = 1; i <= s_board_count; ++i) {
        sw_err_t ret = snack_write_output(i, snapshot[i]);

        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_output_mutex);
        if (ret == SW_OK) {
            uint64_t now_ms = time_util_get_ms();

            /* 写出期间业务线程可能提交了新目标态，只能清除本次快照对应的 dirty。 */
            if (s_output_buf[i] == snapshot[i]) {
                s_output_dirty[i] = false;
            }
            s_stats[i].output_flush_count++;
            s_stats[i].last_output_flush_ms = now_ms;
            s_stats[i].last_output_snapshot = snapshot[i];
        } else {
            s_output_dirty[i] = true;
            if (first_error == SW_OK) {
                first_error = ret;
            }
        }
        s_stats[i].dirty_pending = s_output_dirty[i];
        pthread_mutex_unlock(&s_output_mutex);
    }
    return first_error;
}

static sw_err_t drv_io_run_job(const drv_io_transaction_t *job, drv_io_transaction_result_t *result)
{
    if (job == NULL) {
        return SW_ERR_PARAM;
    }

    switch (job->type) {
    case DRV_IO_TRANSACTION_FLUSH_OUTPUTS:
        return drv_io_flush_outputs_backend();
    case DRV_IO_TRANSACTION_PULSE_READ:
        if (result == NULL) {
            return SW_ERR_PARAM;
        }
        result->value = io_pluse_read(job->board_id, job->channel);
        return SW_OK;
    case DRV_IO_TRANSACTION_PULSE_CLEAR: {
        int data = 0;

        return (io_SDO_write(job->board_id, 0x2005, job->channel, &data) >= 0) ? SW_OK : SW_ERR_COMM;
    }
    default:
        return SW_ERR_PARAM;
    }
}

static void drv_io_fill_wait_deadline(uint32_t timeout_ms, struct timespec *ts)
{
    if (s_worker_cond_monotonic) {
        time_util_fill_monotonic_deadline(timeout_ms, ts);
    } else {
        time_util_fill_deadline(timeout_ms, ts);
    }
}

static sw_err_t drv_io_submit_job(const drv_io_transaction_t        *job,
                                  uint32_t                           timeout_ms,
                                  drv_io_transaction_result_t       *result)
{
    struct timespec deadline;
    int             wait_ret = 0;
    sw_err_t        err;

    if ((job == NULL) || (timeout_ms == 0U)) {
        return SW_ERR_PARAM;
    }
    if (!s_io_rw_started) {
        return SW_ERR_NOT_INIT;
    }

    drv_io_mutexes_ready();
    if (s_worker_created && (pthread_equal(pthread_self(), s_worker) != 0)) {
        return drv_io_run_job(job, result);
    }

    drv_io_fill_wait_deadline(timeout_ms, &deadline);
    pthread_mutex_lock(&s_worker_mutex);
    while ((s_mailbox_state != DRV_IO_MAILBOX_IDLE) && (wait_ret != ETIMEDOUT)) {
        wait_ret = pthread_cond_timedwait(&s_worker_cond, &s_worker_mutex, &deadline);
    }
    if (s_mailbox_state != DRV_IO_MAILBOX_IDLE) {
        pthread_mutex_unlock(&s_worker_mutex);
        return SW_ERR_TIMEOUT;
    }

    s_mailbox_job         = *job;
    s_mailbox_waiter_gone = false;
    s_mailbox_state       = DRV_IO_MAILBOX_PENDING;
    pthread_cond_broadcast(&s_worker_cond);

    wait_ret = 0;
    while ((s_mailbox_state != DRV_IO_MAILBOX_DONE) && (wait_ret != ETIMEDOUT)) {
        wait_ret = pthread_cond_timedwait(&s_worker_cond, &s_worker_mutex, &deadline);
    }
    if (s_mailbox_state != DRV_IO_MAILBOX_DONE) {
        s_mailbox_waiter_gone = true;
        if (s_mailbox_state == DRV_IO_MAILBOX_PENDING) {
            s_mailbox_state = DRV_IO_MAILBOX_IDLE;
            pthread_cond_broadcast(&s_worker_cond);
        }
        pthread_mutex_unlock(&s_worker_mutex);
        return SW_ERR_TIMEOUT;
    }

    err = s_mailbox_err;
    if ((err == SW_OK) && (result != NULL)) {
        *result = s_mailbox_result;
    }
    s_mailbox_state = DRV_IO_MAILBOX_IDLE;
    pthread_cond_broadcast(&s_worker_cond);
    pthread_mutex_unlock(&s_worker_mutex);
    return err;
}

static void *drv_io_worker_fn(void *arg)
{
    uint64_t next_tick_ms;

    (void)arg;
#if defined(__linux__)
    (void)pthread_setname_np(pthread_self(), "io_exp");
#endif
    next_tick_ms = time_util_get_ms();

    for (;;) {
        bool                    do_tick = false;
        bool                    run_job = false;
        drv_io_transaction_t    job;
        drv_io_transaction_result_t job_result;
        sw_err_t                job_err;
        uint64_t                now_ms;

        pthread_mutex_lock(&s_worker_mutex);
        if (s_stop_requested) {
            pthread_mutex_unlock(&s_worker_mutex);
            return NULL;
        }

        now_ms = time_util_get_ms();
        if (now_ms >= next_tick_ms) {
            do_tick      = true;
            next_tick_ms = now_ms + IO_UPDATE_FREQ_MS;
        }
        if (s_mailbox_state == DRV_IO_MAILBOX_PENDING) {
            job                   = s_mailbox_job;
            s_mailbox_state       = DRV_IO_MAILBOX_EXECUTING;
            run_job               = true;
        }

        if (!do_tick && !run_job) {
            uint32_t        wait_ms = (next_tick_ms > now_ms) ? (uint32_t)(next_tick_ms - now_ms) : 1U;
            struct timespec wait_deadline;

            drv_io_fill_wait_deadline(wait_ms, &wait_deadline);
            (void)pthread_cond_timedwait(&s_worker_cond, &s_worker_mutex, &wait_deadline);
            pthread_mutex_unlock(&s_worker_mutex);
            continue;
        }
        pthread_mutex_unlock(&s_worker_mutex);

        if (do_tick) {
            drv_io_poll_tick();
        }
        if (run_job) {
            memset(&job_result, 0, sizeof(job_result));
            job_err = drv_io_run_job(&job, &job_result);
            pthread_mutex_lock(&s_worker_mutex);
            s_mailbox_err    = job_err;
            s_mailbox_result = job_result;
            if (s_mailbox_waiter_gone) {
                s_mailbox_state       = DRV_IO_MAILBOX_IDLE;
                s_mailbox_waiter_gone = false;
            } else {
                s_mailbox_state = DRV_IO_MAILBOX_DONE;
            }
            pthread_cond_broadcast(&s_worker_cond);
            pthread_mutex_unlock(&s_worker_mutex);
        }
    }
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_io_init(const drv_io_cfg_t *cfg)
{
    sw_err_t ret = drv_io_cfg_validate(cfg);

    if (ret != SW_OK) {
        return ret;
    }
    if (s_io_rw_started || s_worker_created) {
        return SW_ERR_STATE;
    }

    s_board_count    = cfg->board_count;
    s_pin_count      = cfg->pin_count;
    s_di_table       = cfg->di_table;
    s_di_count       = cfg->di_count;
    s_do_table       = cfg->do_table;
    s_do_count       = cfg->do_count;
    s_transport_mode = (cfg->board_count <= IO_PDO_BOARD_MAX) ? DRV_IO_TRANSPORT_PDO : DRV_IO_TRANSPORT_SDO;

    memset((void *)s_input_buf, 0, sizeof(s_input_buf));
    memset((void *)s_output_buf, 0, sizeof(s_output_buf));
    memset((void *)s_output_dirty, 0, sizeof(s_output_dirty));
    memset((void *)s_board_online, 0, sizeof(s_board_online));
    memset(s_stats, 0, sizeof(s_stats));
    memset(s_seen_online, 0, sizeof(s_seen_online));
    memset(s_input_sequence, 0, sizeof(s_input_sequence));
    for (int i = 0; i < (int)IO_BOARD_MAX; ++i) {
        s_input_quality[i]
            = (i > 0) && (i <= s_board_count) ? IO_SAMPLE_QUALITY_PROBING : IO_SAMPLE_QUALITY_UNINITIALIZED;
    }
    memset(s_test_enable, 0, sizeof(s_test_enable));
    memset(s_test_value, 0, sizeof(s_test_value));
    memset(s_adc, 0, sizeof(s_adc));

    /* 仅在系统启动阶段调用：这里会清空已注册回调，不作为运行期 reset 接口使用。 */
    s_debug_input_cb      = NULL;
    s_board_error_cb      = NULL;
    s_panic_cb            = NULL;
    s_io_rw_started       = false;
    s_mailbox_state       = DRV_IO_MAILBOX_IDLE;
    s_mailbox_waiter_gone = false;
    s_stop_requested      = false;

    s_poll_loop_count              = 0U;
    s_poll_check_interval_ms       = IO_CHECK_PROBING_MS;
    s_poll_startup_ms              = time_util_get_ms();
    s_poll_all_offline_action_done = false;
    memset(s_poll_offline_count, 0, sizeof(s_poll_offline_count));
    memset(s_poll_online_count, 0, sizeof(s_poll_online_count));
    memset(s_poll_offline_confirmed, 0, sizeof(s_poll_offline_confirmed));

    LOG_INFO("drv_io init ok, board_count=%d pin_count=%d transport=%s",
             s_board_count,
             s_pin_count,
             (s_transport_mode == DRV_IO_TRANSPORT_PDO) ? "PDO" : "SDO");
    return SW_OK;
}

sw_err_t drv_io_start(void)
{
    int ret;

    drv_io_mutexes_ready();
    if (s_io_rw_started || s_worker_created) {
        return SW_ERR_STATE;
    }

    s_stop_requested = false;
    ret              = pthread_create(&s_worker, NULL, drv_io_worker_fn, NULL);
    if (ret != 0) {
        LOG_ERROR("drv_io_start: worker create failed errno=%d", ret);
        return SW_ERR_HW;
    }
    s_worker_created = true;
    s_io_rw_started  = true;
    LOG_INFO("drv_io: worker started transport=%s", (s_transport_mode == DRV_IO_TRANSPORT_PDO) ? "PDO" : "SDO");
    return SW_OK;
}

sw_err_t drv_io_do_set(io_do_t pin, bool val)
{
    uint16_t     raw      = io_do_raw(pin);
    int          board_id = (int)io_handle_board(raw);
    int          pin_id   = (int)io_handle_pin(raw);
    int          bit      = pin_id - 1;
    unsigned int prev_out = 0U;

    if (!drv_io_is_valid_do_raw(raw)) {
        LOG_ERROR("drv_io_do_set: invalid DO raw=0x%04X", (unsigned)raw);
        return SW_ERR_PARAM;
    }

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_output_mutex);
    prev_out = s_output_buf[board_id];

    if (val) {
        s_output_buf[board_id] |= (1U << bit);
    } else {
        s_output_buf[board_id] &= ~(1U << bit);
    }

    if (s_output_buf[board_id] != prev_out) {
        uint64_t now_ms = time_util_get_ms();
        s_stats[board_id].output_request_count++;
        s_stats[board_id].last_output_req_ms   = now_ms;
        s_stats[board_id].last_output_snapshot = s_output_buf[board_id];
    }

    s_output_dirty[board_id]        = true;
    s_stats[board_id].dirty_pending = true;
    pthread_mutex_unlock(&s_output_mutex);

    return SW_OK;
}

sw_err_t drv_io_flush_outputs_now(void)
{
    drv_io_transaction_t transaction = {
        .type     = DRV_IO_TRANSACTION_FLUSH_OUTPUTS,
        .board_id = 0,
        .channel  = 0,
    };
    uint32_t timeout_ms = IO_TRANSACTION_TIMEOUT_MS + (uint32_t)s_board_count * IO_TRANSACTION_TIMEOUT_MS;

    return drv_io_submit_job(&transaction, timeout_ms, NULL);
}

sw_err_t drv_io_di_read(io_di_t pin, io_di_sample_t *sample)
{
    uint16_t raw      = io_di_raw(pin);
    int      board_id = (int)io_handle_board(raw);
    int      pin_id   = (int)io_handle_pin(raw);

    if (sample == NULL) {
        return SW_ERR_PARAM;
    }
    *sample = (io_di_sample_t){.quality = IO_SAMPLE_QUALITY_UNINITIALIZED};

    if (!drv_io_is_valid_di_raw(raw)) {
        return SW_ERR_PARAM;
    }

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_input_mutex);
    /* 测试覆盖优先 */
    if (s_test_enable[board_id][pin_id]) {
        sample->level        = s_test_value[board_id][pin_id];
        sample->quality      = IO_SAMPLE_QUALITY_VALID;
        sample->timestamp_ms = time_util_get_ms();
        sample->sequence     = s_input_sequence[board_id];
        pthread_mutex_unlock(&s_input_mutex);
        return SW_OK;
    }

    sample->level        = (bool)((s_input_buf[board_id] >> (pin_id - 1)) & 1U);
    sample->quality      = s_input_quality[board_id];
    sample->timestamp_ms = s_stats[board_id].last_input_refresh_ms;
    sample->sequence     = s_input_sequence[board_id];
    if ((sample->quality == IO_SAMPLE_QUALITY_VALID)
        && (time_elapsed_ms(sample->timestamp_ms, time_util_get_ms()) > IO_INPUT_FRESHNESS_TIMEOUT_MS)) {
        sample->quality = IO_SAMPLE_QUALITY_STALE;
    }
    pthread_mutex_unlock(&s_input_mutex);
    return SW_OK;
}

void drv_io_register_debug_input_cb(drv_io_debug_input_cb_t cb)
{
    s_debug_input_cb = cb;
}

bool drv_io_board_is_online(int board_id)
{
    bool online;

    if ((board_id <= 0) || (board_id > s_board_count)) {
        return false;
    }

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_input_mutex);
    online = s_board_online[board_id];
    pthread_mutex_unlock(&s_input_mutex);
    return online;
}

#define DRV_IO_WAIT_POLL_MS 50U /* 子板就绪轮询间隔（ms）*/

sw_err_t drv_io_wait_boards_online(uint32_t timeout_ms)
{
    uint64_t start_ms = time_util_get_ms();

    if (!s_io_rw_started) {
        return SW_ERR_NOT_INIT;
    }

    while (time_elapsed_ms(start_ms, time_util_get_ms()) < timeout_ms) {
        bool all_online = true;

        for (int i = 1; i <= s_board_count; i++) {
            if (!drv_io_board_is_online(i)) {
                all_online = false;
                break;
            }
        }

        if (all_online) {
            return SW_OK;
        }

        usleep((unsigned long)DRV_IO_WAIT_POLL_MS * 1000UL);
    }

    return SW_ERR_TIMEOUT;
}

void drv_io_register_board_error_cb(void (*cb)(int board_id, bool offline))
{
    s_board_error_cb = cb;
}

void drv_io_register_panic_cb(void (*cb)(void))
{
    s_panic_cb = cb;
}

void drv_io_set_test_override(io_di_t pin, int value)
{
    uint16_t raw      = io_di_raw(pin);
    int      board_id = (int)io_handle_board(raw);
    int      pin_id   = (int)io_handle_pin(raw);

    if (!drv_io_is_valid_di_raw(raw)) {
        return;
    }

    if ((value == 0) || (value == 1)) {
        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_input_mutex);
        s_test_value[board_id][pin_id]  = (bool)value;
        s_test_enable[board_id][pin_id] = true;
        pthread_mutex_unlock(&s_input_mutex);
    } else {
        drv_io_mutexes_ready();
        pthread_mutex_lock(&s_input_mutex);
        s_test_enable[board_id][pin_id] = false;
        pthread_mutex_unlock(&s_input_mutex);
    }
}

void drv_io_clear_test_override(io_di_t pin)
{
    uint16_t raw      = io_di_raw(pin);
    int      board_id = (int)io_handle_board(raw);
    int      pin_id   = (int)io_handle_pin(raw);

    if (!drv_io_is_valid_di_raw(raw)) {
        return;
    }

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_input_mutex);
    s_test_enable[board_id][pin_id] = false;
    pthread_mutex_unlock(&s_input_mutex);
}

sw_err_t drv_io_get_stats(int board_id, hal_io_stats_t *out)
{
    if ((board_id <= 0) || (board_id > s_board_count) || (out == NULL)) {
        return SW_ERR_PARAM;
    }

    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_input_mutex);
    *out                       = (hal_io_stats_t){0};
    out->online                = s_stats[board_id].online;
    out->offline_count         = s_stats[board_id].offline_count;
    out->online_recover_count  = s_stats[board_id].online_recover_count;
    out->input_refresh_count   = s_stats[board_id].input_refresh_count;
    out->last_online_ms        = s_stats[board_id].last_online_ms;
    out->last_offline_ms       = s_stats[board_id].last_offline_ms;
    out->last_input_refresh_ms = s_stats[board_id].last_input_refresh_ms;
    out->last_input_snapshot   = s_stats[board_id].last_input_snapshot;
    pthread_mutex_unlock(&s_input_mutex);
    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_output_mutex);
    out->dirty_pending        = s_stats[board_id].dirty_pending;
    out->output_request_count = s_stats[board_id].output_request_count;
    out->output_flush_count   = s_stats[board_id].output_flush_count;
    out->last_output_req_ms   = s_stats[board_id].last_output_req_ms;
    out->last_output_flush_ms = s_stats[board_id].last_output_flush_ms;
    out->last_output_snapshot = s_stats[board_id].last_output_snapshot;
    pthread_mutex_unlock(&s_output_mutex);
    return SW_OK;
}

int drv_io_board_count(void)
{
    return s_board_count;
}

drv_io_transport_mode_t drv_io_transport_mode(void)
{
    return s_transport_mode;
}

int drv_io_pulse_read(io_di_t pin)
{
    uint16_t                    raw      = io_di_raw(pin);
    int                         board_id = (int)io_handle_board(raw);
    int                         pin_id   = (int)io_handle_pin(raw);
    drv_io_transaction_t        transaction;
    drv_io_transaction_result_t result;

    if (!drv_io_is_valid_di_raw(raw)) {
        return -1;
    }

    transaction = (drv_io_transaction_t){
        .type     = DRV_IO_TRANSACTION_PULSE_READ,
        .board_id = board_id,
        .channel  = pin_id,
    };
    if (drv_io_submit_job(&transaction, IO_TRANSACTION_TIMEOUT_MS, &result)
        != SW_OK) {
        return -1;
    }
    return result.value;
}

sw_err_t drv_io_pulse_clear(io_di_t pin)
{
    uint16_t             raw      = io_di_raw(pin);
    int                  board_id = (int)io_handle_board(raw);
    int                  pin_id   = (int)io_handle_pin(raw);
    drv_io_transaction_t transaction;

    if (!drv_io_is_valid_di_raw(raw)) {
        return SW_ERR_PARAM;
    }

    transaction = (drv_io_transaction_t){
        .type     = DRV_IO_TRANSACTION_PULSE_CLEAR,
        .board_id = board_id,
        .channel  = pin_id,
    };
    return drv_io_submit_job(&transaction, IO_TRANSACTION_TIMEOUT_MS, NULL);
}

static bool drv_io_is_valid_adc(int board_id, int port)
{
    return (board_id > 0) && (board_id <= s_board_count) && (port >= DRV_IO_ADC_PORT_MIN)
           && (port <= DRV_IO_ADC_PORT_MAX);
}

/**
 * @brief  读取 ADC 快照（缓存，不触发 SDO）
 */
sw_err_t drv_io_adc_sample(int board_id, int port, io_adc_sample_t *sample)
{
    uint64_t now_ms;

    if ((sample == NULL) || !drv_io_is_valid_adc(board_id, port)) {
        return SW_ERR_PARAM;
    }

    now_ms = time_util_get_ms();
    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_adc_mutex);
    if ((s_adc[board_id][port].quality == IO_SAMPLE_QUALITY_VALID)
        && (time_elapsed_ms(s_adc[board_id][port].timestamp_ms, now_ms) > IO_ADC_FRESHNESS_TIMEOUT_MS)) {
        s_adc[board_id][port].quality = IO_SAMPLE_QUALITY_STALE;
    }
    sample->raw           = s_adc[board_id][port].raw;
    sample->millivolt     = s_adc[board_id][port].millivolt;
    sample->milliamp      = s_adc[board_id][port].milliamp;
    sample->quality       = s_adc[board_id][port].quality;
    sample->timestamp_ms  = s_adc[board_id][port].timestamp_ms;
    pthread_mutex_unlock(&s_adc_mutex);
    return SW_OK;
}

/** ADC 缓存快照中要取出的字段 */
typedef enum {
    DRV_IO_ADC_FIELD_RAW = 0,
    DRV_IO_ADC_FIELD_MV,
    DRV_IO_ADC_FIELD_MA
} drv_io_adc_field_t;

static int drv_io_adc_cached_field(int board_id, int port, drv_io_adc_field_t field)
{
    io_adc_sample_t sample;

    if (drv_io_adc_sample(board_id, port, &sample) != SW_OK) {
        return -1;
    }
    if ((sample.quality == IO_SAMPLE_QUALITY_UNINITIALIZED)
        || (sample.quality == IO_SAMPLE_QUALITY_PROBING)) {
        return DRV_IO_ADC_ERR_NOT_INIT;
    }
    if (sample.quality != IO_SAMPLE_QUALITY_VALID) {
        return -1;
    }
    if (field == DRV_IO_ADC_FIELD_MV) {
        return sample.millivolt;
    }
    if (field == DRV_IO_ADC_FIELD_MA) {
        return sample.milliamp;
    }
    return sample.raw;
}

int drv_io_adc_read(int board_id, int port)
{
    return drv_io_adc_cached_field(board_id, port, DRV_IO_ADC_FIELD_RAW);
}

int drv_io_adc_mv(int board_id, int port)
{
    return drv_io_adc_cached_field(board_id, port, DRV_IO_ADC_FIELD_MV);
}

int drv_io_adc_ma(int board_id, int port)
{
    return drv_io_adc_cached_field(board_id, port, DRV_IO_ADC_FIELD_MA);
}

#ifdef SNACK_IO_ADAPTER_UNIT_TEST
sw_err_t drv_io_reset_for_test(void)
{
    drv_io_mutexes_ready();
    pthread_mutex_lock(&s_worker_mutex);
    if (s_worker_created) {
        s_stop_requested = true;
        pthread_cond_broadcast(&s_worker_cond);
        pthread_mutex_unlock(&s_worker_mutex);
        (void)pthread_join(s_worker, NULL);
        pthread_mutex_lock(&s_worker_mutex);
        s_worker_created = false;
    }
    s_io_rw_started       = false;
    s_stop_requested      = false;
    s_mailbox_state       = DRV_IO_MAILBOX_IDLE;
    s_mailbox_waiter_gone = false;
    pthread_mutex_unlock(&s_worker_mutex);
    return SW_OK;
}
#endif
