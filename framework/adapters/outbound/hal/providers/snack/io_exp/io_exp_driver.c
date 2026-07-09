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
 *          - 输入变化调试通知
 *          - 全板离线时的安全停机联动
 */

#include "framework/adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"

#include "framework/common/log.h"
#include "framework/common/time_util.h"
#include "io_exp/demo.h"
#include "io_exp/slave.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

sw_err_t io_exp_driver_sdk_init(const char *can_bus, int can_baud, int self_node, int board_count)
{
    return (io_init(can_bus, can_baud, self_node, board_count) == 0) ? SW_OK : SW_ERR_HW;
}

void io_exp_driver_set_log_api(int (*cb)(const char *fmt, ...))
{
    io_logApi_set(cb);
}

/* -------------------------------------------------------------------------
 * 内部常量
 * ------------------------------------------------------------------------- */
#define IO_BOARD_MAX        7U             /* 最大子板数，含 0 号占位 */
#define IO_PIN_COUNT_MAX    32U            /* 每块子板 IO 点数上限，仅用于静态数组维度 */
#define IO_RW_STACK_BYTES   (16U * 1024U)  /* io_rw 线程栈 */
#define IO_UPDATE_FREQ_MS   30U            /* 输入/输出缓冲刷新周期（ms） */
#define IO_CHECK_OFFLINE_MS 300U           /* 全部在线时的在线检测间隔（ms） */
#define IO_CHECK_ONLINE_MS  2000U          /* 存在掉线子板时的重连检测间隔（ms） */
#define IO_OFFLINE_CNT      3U             /* 连续无响应次数达到该值后判定掉线 */
#define IO_ONLINE_CNT       IO_OFFLINE_CNT /* 连续响应次数达到该值后确认上线（对称防抖）*/

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static volatile unsigned int s_input_buf[IO_BOARD_MAX]    = {0};
static volatile unsigned int s_output_buf[IO_BOARD_MAX]   = {0};
static volatile bool         s_output_dirty[IO_BOARD_MAX] = {false};
static volatile bool         s_board_online[IO_BOARD_MAX] = {0};
static pthread_mutex_t       s_output_mutex               = PTHREAD_MUTEX_INITIALIZER;
static drv_io_stats_t        s_stats[IO_BOARD_MAX]        = {{0}};
static bool                  s_seen_online[IO_BOARD_MAX]  = {false};

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
static bool s_io_rw_started                                 = false;

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
    offline_cnt[id] = 0U;

    if (s_board_online[id]) {
        online_cnt[id] = 0U;
        return;
    }

    ++online_cnt[id];
    if (online_cnt[id] < (uint8_t)IO_ONLINE_CNT) {
        return;
    }

    /* 连续在线次数达到阈值：确认上线 */
    {
        uint64_t now_ms    = time_util_get_ms();
        bool     recovered = s_seen_online[id];

        online_cnt[id]             = 0U;
        offline_confirmed[id]      = false; /* 连续确认后才解除已确认离线状态 */
        s_board_online[id]         = true;
        s_stats[id].online         = true;
        s_stats[id].last_online_ms = now_ms;
        if (recovered) {
            s_stats[id].online_recover_count++;
        } else {
            s_seen_online[id] = true;
        }

        /* 子板离线期间硬件输出可能丢失，恢复后强制重发 */
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

        s_board_online[id]    = false;
        offline_confirmed[id] = true;
        s_stats[id].online    = false;
        s_stats[id].offline_count++;
        s_stats[id].last_offline_ms = now_ms;

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

/* 单块在线子板的输入刷新 + 输出落地 */
static void poll_rw_board(int id)
{
    unsigned int prev    = s_input_buf[id];
    unsigned int out_val = 0U;
    uint64_t     now_ms;
    bool         dirty = false;

    /* SDO 读写一次约 1~2ms；读失败时 SDK 会保留上次值 */
    s_input_buf[id]    = (unsigned int)io_read_input_s(id);
    now_ms             = time_util_get_ms();
    s_stats[id].online = true;
    s_stats[id].input_refresh_count++;
    s_stats[id].last_input_refresh_ms = now_ms;
    s_stats[id].last_input_snapshot   = s_input_buf[id];

    pthread_mutex_lock(&s_output_mutex);
    dirty   = s_output_dirty[id];
    out_val = s_output_buf[id];
    if (dirty) {
        s_output_dirty[id] = false;
    }
    s_stats[id].dirty_pending = s_output_dirty[id];
    pthread_mutex_unlock(&s_output_mutex);

    if (dirty) {
        io_write_all_s(id, (int)out_val);
        now_ms = time_util_get_ms();
        s_stats[id].output_flush_count++;
        s_stats[id].last_output_flush_ms = now_ms;
        s_stats[id].last_output_snapshot = out_val;
        s_stats[id].dirty_pending        = false;
    }

    /* 输入变化调试通知，仅用于观察，不参与正式业务判断 */
    if (s_debug_input_cb != NULL) {
        unsigned int changed = prev ^ s_input_buf[id];
        for (int bit = 0; bit < s_pin_count; ++bit) {
            if (((changed >> bit) & 1U) != 0U) {
                io_di_t pin       = io_di_make((uint16_t)id, (uint16_t)(bit + 1));
                bool    new_state = (bool)((s_input_buf[id] >> bit) & 1U);
                s_debug_input_cb(pin, new_state);
            }
        }
    }
}

/* 全板确认离线时的安全停机处理 */
static void poll_all_offline_panic(void)
{
    LOG_ERROR("drv_io: all boards offline, safe stop and abort");

    /* panic_cb（如 m8_assert_safe_outputs）通过 drv_io_do_set 把缓冲设为安全态，
     * 不负责 flush；flush 由驱动在此处统一执行，保证一定能写到硬件。*/
    if (s_panic_cb != NULL) {
        s_panic_cb();
    }

    /* 快照 panic_cb 写入的安全态缓冲，无条件写入全部子板（不受 s_board_online 限制）。
     * 若离线判定为误判且 CAN 仍可达，此次写入将硬件置于安全态；
     * 若 CAN 真的断开，写入失败无副作用。*/
    {
        unsigned int snapshot[IO_BOARD_MAX] = {0U};
        pthread_mutex_lock(&s_output_mutex);
        for (int i = 1; i <= s_board_count; ++i) {
            snapshot[i] = s_output_buf[i];
        }
        pthread_mutex_unlock(&s_output_mutex);
        for (int i = 1; i <= s_board_count; ++i) {
            io_write_all_s(i, (int)snapshot[i]);
        }
    }

    sleep(2);
    abort();
}

/* -------------------------------------------------------------------------
 * IO 读写后台线程（主循环）
 * ------------------------------------------------------------------------- */
static void *drv_io_poll_loop(void *arg)
{
    uint16_t loop_cnt                        = 0U;
    uint8_t  offline_cnt[IO_BOARD_MAX]       = {0U};
    uint8_t  online_cnt[IO_BOARD_MAX]        = {0U};
    bool     offline_confirmed[IO_BOARD_MAX] = {false};
    int      check_interval_ms               = IO_CHECK_OFFLINE_MS;

    (void)arg;

    while (1) {
        int  online_count            = 0;
        int  offline_confirmed_count = 0;
        bool any_recovering          = false;
        int  check_loops             = check_interval_ms / IO_UPDATE_FREQ_MS;

        if (check_loops < 1) {
            check_loops = 1;
        }

        ++loop_cnt;

        for (int i = 1; i <= s_board_count; ++i) {
            if ((loop_cnt % (uint16_t)check_loops) == 0U) {
                poll_check_board(i, online_cnt, offline_cnt, offline_confirmed);
            }

            if (s_board_online[i]) {
                poll_rw_board(i);
                ++online_count;
            } else if (offline_confirmed[i]) {
                ++offline_confirmed_count;
                if (online_cnt[i] > 0U) {
                    any_recovering = true; /* 正在恢复中，暂缓 panic */
                }
            }

            usleep((unsigned int)(IO_UPDATE_FREQ_MS * 1000) / (unsigned int)s_board_count);
        }

        if (offline_confirmed_count == s_board_count && !any_recovering) {
            poll_all_offline_panic();
        }

        /* 有子板掉线则不允许设备启动，加大检测周期为了减少日志输出 */
        check_interval_ms = (online_count == s_board_count) ? IO_CHECK_OFFLINE_MS : IO_CHECK_ONLINE_MS;
    }

    return NULL; /* 不可达：while(1) 永不退出，满足编译器对非 void 函数的返回要求 */
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_io_init(const drv_io_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->board_count <= 0) || (cfg->board_count >= (int)IO_BOARD_MAX) || (cfg->pin_count <= 0)
        || (cfg->pin_count > (int)IO_PIN_COUNT_MAX)) {
        return SW_ERR_PARAM;
    }

    s_board_count = cfg->board_count;
    s_pin_count   = cfg->pin_count;
    s_di_table    = cfg->di_table;
    s_di_count    = cfg->di_count;
    s_do_table    = cfg->do_table;
    s_do_count    = cfg->do_count;

    memset((void *)s_input_buf, 0, sizeof(s_input_buf));
    memset((void *)s_output_buf, 0, sizeof(s_output_buf));
    memset((void *)s_output_dirty, 0, sizeof(s_output_dirty));
    memset((void *)s_board_online, 0, sizeof(s_board_online));
    memset(s_stats, 0, sizeof(s_stats));
    memset(s_seen_online, 0, sizeof(s_seen_online));
    memset(s_test_enable, 0, sizeof(s_test_enable));
    memset(s_test_value, 0, sizeof(s_test_value));

    /* 仅在系统启动阶段调用：这里会清空已注册回调，不作为运行期 reset 接口使用。 */
    s_debug_input_cb = NULL;
    s_board_error_cb = NULL;
    s_panic_cb       = NULL;
    s_io_rw_started  = false;

    LOG_INFO("drv_io init ok, board_count=%d pin_count=%d", s_board_count, s_pin_count);
    return SW_OK;
}

sw_err_t drv_io_start(void)
{
    pthread_attr_t attr;
    pthread_t      tid;

    if (s_io_rw_started) {
        return SW_ERR_STATE;
    }

    pthread_attr_init(&attr);
    (void)pthread_attr_setstacksize(&attr, (size_t)IO_RW_STACK_BYTES);

    if (pthread_create(&tid, &attr, drv_io_poll_loop, NULL) != 0) {
        pthread_attr_destroy(&attr);
        LOG_ERROR("drv_io_start: pthread_create failed");
        return SW_ERR_HW;
    }

    pthread_detach(tid);
    pthread_attr_destroy(&attr);
    s_io_rw_started = true;
    LOG_INFO("drv_io: io_rw thread started");
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
    unsigned int snapshot[IO_BOARD_MAX] = {0U};
    bool         online[IO_BOARD_MAX]   = {false};

    /* 快照输出缓冲和在线状态，缩短持锁时间 */
    pthread_mutex_lock(&s_output_mutex);
    for (int i = 1; i <= s_board_count; ++i) {
        snapshot[i] = s_output_buf[i];
        online[i]   = s_board_online[i];
        if (online[i]) {
            s_output_dirty[i]        = false;
            s_stats[i].dirty_pending = false;
        }
    }
    pthread_mutex_unlock(&s_output_mutex);

    /* 只对在线子板执行写操作，避免 CAN 超时阻塞 */
    for (int i = 1; i <= s_board_count; ++i) {
        if (online[i]) {
            uint64_t now_ms = time_util_get_ms();
            io_write_all_s(i, (int)snapshot[i]);
            s_stats[i].output_flush_count++;
            s_stats[i].last_output_flush_ms = now_ms;
            s_stats[i].last_output_snapshot = snapshot[i];
        }
    }

    return SW_OK;
}

bool drv_io_di_read(io_di_t pin)
{
    uint16_t raw      = io_di_raw(pin);
    int      board_id = (int)io_handle_board(raw);
    int      pin_id   = (int)io_handle_pin(raw);

    if (!drv_io_is_valid_di_raw(raw)) {
        return false;
    }

    /* 测试覆盖优先 */
    if (s_test_enable[board_id][pin_id]) {
        return s_test_value[board_id][pin_id];
    }

    return (bool)((s_input_buf[board_id] >> (pin_id - 1)) & 1U);
}

void drv_io_register_debug_input_cb(drv_io_debug_input_cb_t cb)
{
    s_debug_input_cb = cb;
}

bool drv_io_board_is_online(int board_id)
{
    if ((board_id <= 0) || (board_id > s_board_count)) {
        return false;
    }

    return s_board_online[board_id];
}

#define DRV_IO_WAIT_POLL_MS 50U /* 子板就绪轮询间隔（ms）*/

sw_err_t drv_io_wait_boards_online(uint32_t timeout_ms)
{
    uint64_t start_ms = time_util_get_ms();

    while (time_elapsed_ms(start_ms, time_util_get_ms()) < timeout_ms) {
        bool all_online = true;

        for (int i = 1; i <= s_board_count; i++) {
            if (io_online_get(i) <= 0) {
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
        s_test_value[board_id][pin_id]  = (bool)value;
        s_test_enable[board_id][pin_id] = true;
    } else {
        s_test_enable[board_id][pin_id] = false;
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

    s_test_enable[board_id][pin_id] = false;
}

sw_err_t drv_io_get_stats(int board_id, drv_io_stats_t *out)
{
    if ((board_id <= 0) || (board_id > s_board_count) || (out == NULL)) {
        return SW_ERR_PARAM;
    }

    /* 无锁近似快照：stats 只用于调试诊断，允许读到瞬时变化中的值。 */
    *out = s_stats[board_id];
    return SW_OK;
}

int drv_io_board_count(void)
{
    return s_board_count;
}

int drv_io_pulse_read(io_di_t pin)
{
    uint16_t raw      = io_di_raw(pin);
    int      board_id = (int)io_handle_board(raw);
    int      pin_id   = (int)io_handle_pin(raw);

    if (!drv_io_is_valid_di_raw(raw)) {
        return -1;
    }

    return io_pluse_read(board_id, pin_id);
}

sw_err_t drv_io_pulse_clear(io_di_t pin)
{
    uint16_t raw      = io_di_raw(pin);
    int      board_id = (int)io_handle_board(raw);
    int      pin_id   = (int)io_handle_pin(raw);
    int      data     = 0;

    if (!drv_io_is_valid_di_raw(raw)) {
        return SW_ERR_PARAM;
    }

    return (io_SDO_write(board_id, 0x2005, pin_id, &data) >= 0) ? SW_OK : SW_ERR_COMM;
}
