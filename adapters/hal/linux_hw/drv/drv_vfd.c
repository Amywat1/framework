/**
 * @file    drv_vfd.c
 * @brief   变频器驱动实现（Modbus RTU + IO 数字量控制）
 * @author  HUWANGWEI
 * @date    2026-04-08
 */

#include "drv_vfd.h"

#include "common/log.h"
#include "common/time_util.h"
#include "modbus/modbus-rtu.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Modbus 寄存器地址（按厂家区分；切换厂家时改下方别名）
 * ------------------------------------------------------------------------- */
#define VFD_SHIHLIN_REG_FREQ_SET   0x2001U
#define VFD_SHIHLIN_REG_STATUS     0x2100U
#define VFD_SHIHLIN_REG_FAULT_CODE 0x2102U
#define VFD_SHIHLIN_REG_CURRENT    0x2104U

#define VFD_REG_FREQ_SET   VFD_SHIHLIN_REG_FREQ_SET
#define VFD_REG_STATUS     VFD_SHIHLIN_REG_STATUS
#define VFD_REG_FAULT_CODE VFD_SHIHLIN_REG_FAULT_CODE
#define VFD_REG_CURRENT    VFD_SHIHLIN_REG_CURRENT

#define VFD_FAULT_RESET_PULSE_MS  200U
#define VFD_MONITOR_POLL_MS       20U    /* monitor worker 轮询周期 */
#define VFD_SLOW_POLL_MS          2000U  /* 故障码 / 电流慢速轮询间隔 */
#define VFD_MONITOR_INST_MAX      4U     /* monitor worker 支持的最大 VFD 实例数 */
#define VFD_MODBUS_TIMEOUT_US     500000U /* Modbus 响应超时 500ms */
#define VFD_COMM_FAIL_RECONNECT   50U    /* 连续失败 N 次后重建 Modbus 连接 */
#define VFD_COMM_FAIL_NOTIFY      3U     /* 连续失败 N 次后通知上层 */
#define VFD_DIR_SWITCH_DELAY_MS   500U   /* 方向切换等待时间，留余量给电机减速 */

#define VFD_BUS_PORT_MAX      4U
#define VFD_BUS_PORT_PATH_MAX 64U

/* 同一串口上挂载多个 VFD 时共享一把总线互斥锁，通过 ref_count 追踪引用 */
typedef struct {
    char            port_path[VFD_BUS_PORT_PATH_MAX];
    int             baud;
    pthread_mutex_t mutex;
    bool            mutex_inited;
    uint8_t         ref_count;
    bool            in_use;
} vfd_bus_port_t;

static vfd_bus_port_t  s_bus_ports[VFD_BUS_PORT_MAX];
/* 保护全局表 s_bus_ports / s_monitor_list / s_monitor_running */
static pthread_mutex_t s_global_mutex   = PTHREAD_MUTEX_INITIALIZER;

static bool          s_monitor_running  = false;
static drv_vfd_t    *s_monitor_list[VFD_MONITOR_INST_MAX];
static size_t        s_monitor_count    = 0U;

/* -------------------------------------------------------------------------
 * 总线端口表管理
 * ------------------------------------------------------------------------- */
static sw_err_t vfd_bus_port_bind(const char *serial_port, int baud, void **out_lock)
{
    size_t   i;
    size_t   free_idx = VFD_BUS_PORT_MAX;
    sw_err_t ret      = SW_OK;

    if ((serial_port == NULL) || (out_lock == NULL)) {
        return SW_ERR_PARAM;
    }

    (void)pthread_mutex_lock(&s_global_mutex);

    for (i = 0U; i < VFD_BUS_PORT_MAX; i++) {
        if (s_bus_ports[i].in_use && (strcmp(s_bus_ports[i].port_path, serial_port) == 0)) {
            if (s_bus_ports[i].baud != baud) {
                LOG_ERROR("drv_vfd: port %s baud mismatch %d vs %d", serial_port, baud, s_bus_ports[i].baud);
                ret = SW_ERR_PARAM;
                goto out;
            }
            if (s_bus_ports[i].ref_count < 0xFFU) {
                s_bus_ports[i].ref_count++;
            }
            *out_lock = &s_bus_ports[i].mutex;
            goto out;
        }
        if (!s_bus_ports[i].in_use && (free_idx == VFD_BUS_PORT_MAX)) {
            free_idx = i;
        }
    }

    if (free_idx >= VFD_BUS_PORT_MAX) {
        LOG_ERROR("drv_vfd: bus port table full");
        ret = SW_ERR_HW;
        goto out;
    }

    (void)memset(s_bus_ports[free_idx].port_path, 0, sizeof(s_bus_ports[free_idx].port_path));
    (void)strncpy(s_bus_ports[free_idx].port_path, serial_port, VFD_BUS_PORT_PATH_MAX - 1U);
    s_bus_ports[free_idx].port_path[VFD_BUS_PORT_PATH_MAX - 1U] = '\0';
    s_bus_ports[free_idx].baud                                   = baud;
    s_bus_ports[free_idx].ref_count                              = 1U;
    s_bus_ports[free_idx].mutex_inited                           = false;

    /* 先初始化 mutex，成功后才标记 in_use=true，避免并发调用拿到未初始化的 mutex */
    if (pthread_mutex_init(&s_bus_ports[free_idx].mutex, NULL) != 0) {
        s_bus_ports[free_idx].port_path[0] = '\0';
        s_bus_ports[free_idx].baud         = 0;
        s_bus_ports[free_idx].ref_count    = 0U;
        LOG_ERROR("drv_vfd: pthread_mutex_init failed for %s", serial_port);
        ret = SW_ERR_HW;
        goto out;
    }
    s_bus_ports[free_idx].mutex_inited = true;
    s_bus_ports[free_idx].in_use       = true;
    *out_lock                          = &s_bus_ports[free_idx].mutex;

out:
    (void)pthread_mutex_unlock(&s_global_mutex);
    return ret;
}

static void vfd_bus_port_unbind(void *bus_lock)
{
    size_t i;

    if (bus_lock == NULL) {
        return;
    }

    (void)pthread_mutex_lock(&s_global_mutex);

    for (i = 0U; i < VFD_BUS_PORT_MAX; i++) {
        if (!s_bus_ports[i].in_use || ((void *)&s_bus_ports[i].mutex != bus_lock)) {
            continue;
        }
        if (s_bus_ports[i].ref_count > 0U) {
            s_bus_ports[i].ref_count--;
        }
        if (s_bus_ports[i].ref_count == 0U) {
            if (s_bus_ports[i].mutex_inited) {
                (void)pthread_mutex_destroy(&s_bus_ports[i].mutex);
            }
            s_bus_ports[i].in_use       = false;
            s_bus_ports[i].mutex_inited = false;
            s_bus_ports[i].baud         = 0;
            s_bus_ports[i].port_path[0] = '\0';
        }
        break;
    }

    (void)pthread_mutex_unlock(&s_global_mutex);
}

/* -------------------------------------------------------------------------
 * 基础辅助
 * ------------------------------------------------------------------------- */
static bool vfd_is_initialized(const drv_vfd_t *vfd)
{
    return (vfd != NULL) && (vfd->serial_port != NULL) && (vfd->baud > 0) && (vfd->modbus_addr > 0) &&
           (vfd->do_set != NULL);
}

static void vfd_do_set(drv_vfd_t *vfd, io_do_t pin, bool val)
{
    if ((vfd != NULL) && (vfd->do_set != NULL)) {
        (void)vfd->do_set(pin, val);
    }
}

/* 将 vfd 注册进 monitor_list，供 monitor worker 轮询 */
static sw_err_t vfd_monitor_list_add(drv_vfd_t *vfd)
{
    sw_err_t ret = SW_OK;

    (void)pthread_mutex_lock(&s_global_mutex);
    if (s_monitor_count >= VFD_MONITOR_INST_MAX) {
        LOG_ERROR("drv_vfd: monitor list full");
        ret = SW_ERR_HW;
    } else {
        s_monitor_list[s_monitor_count] = vfd;
        s_monitor_count++;
    }
    (void)pthread_mutex_unlock(&s_global_mutex);
    return ret;
}

/* -------------------------------------------------------------------------
 * Modbus 连接管理
 * ------------------------------------------------------------------------- */

/* 释放旧 Modbus 上下文并按当前配置新建，不发起 connect（调用方决定连接时机）*/
static sw_err_t mb_ctx_create(drv_vfd_t *vfd)
{
    if (vfd->mb != NULL) {
        modbus_close(vfd->mb);
        modbus_free(vfd->mb);
        vfd->mb = NULL;
    }
    vfd->mb_connected = false;

    vfd->mb = modbus_new_rtu(vfd->serial_port, vfd->baud, 'N', 8, 1);
    if (vfd->mb == NULL) {
        LOG_ERROR("drv_vfd[addr=%d]: modbus_new_rtu failed", vfd->modbus_addr);
        return SW_ERR_HW;
    }
    modbus_set_slave(vfd->mb, vfd->modbus_addr);
    modbus_set_response_timeout(vfd->mb, 0, VFD_MODBUS_TIMEOUT_US);
    return SW_OK;
}

/* 重建 Modbus 上下文并重新连接，在总线锁保护下调用 */
static sw_err_t mb_reconnect_locked(drv_vfd_t *vfd)
{
    sw_err_t ret;

    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    ret = mb_ctx_create(vfd);
    if (ret != SW_OK) {
        return ret;
    }

    if (modbus_connect(vfd->mb) < 0) {
        LOG_ERROR("drv_vfd[addr=%d]: modbus_connect failed", vfd->modbus_addr);
        modbus_free(vfd->mb);
        vfd->mb = NULL;
        return SW_ERR_COMM;
    }

    vfd->mb_connected = true;
    LOG_INFO("drv_vfd[addr=%d]: Modbus linked", vfd->modbus_addr);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * Modbus 通信状态统计
 * ------------------------------------------------------------------------- */
static void mb_on_success_locked(drv_vfd_t *vfd, bool *notify_restored)
{
    if (!vfd->comm_ok) {
        vfd->comm_ok = true;
        if (notify_restored != NULL) {
            *notify_restored = true;
        }
    }
    vfd->comm_fail_count = 0U;
}

static void mb_on_failure_locked(drv_vfd_t *vfd, bool *notify_lost, bool *need_reconnect)
{
    vfd->mb_connected = false;

    if (vfd->comm_fail_count < 0xFFFFU) {
        vfd->comm_fail_count++;
    }

    /* 失败次数达到 NOTIFY 阈值且之前通信正常时，向上通知通信丢失 */
    if ((vfd->comm_fail_count >= VFD_COMM_FAIL_NOTIFY) && vfd->comm_ok) {
        vfd->comm_ok = false;
        if (notify_lost != NULL) {
            *notify_lost = true;
        }
    }

    /* 失败次数达到 RECONNECT 阈值时，请求重建连接并重置计数，避免无限累加 */
    if (vfd->comm_fail_count >= VFD_COMM_FAIL_RECONNECT) {
        vfd->comm_fail_count = 0U;
        if (need_reconnect != NULL) {
            *need_reconnect = true;
        }
    }
}

/* -------------------------------------------------------------------------
 * Modbus 读写统一执行路径
 * ------------------------------------------------------------------------- */

/* 操作描述子：is_write=true 写 wval；false 读结果存入 rval */
typedef struct {
    bool     is_write;
    uint16_t addr;
    uint16_t wval;
    uint16_t rval;
} mb_op_t;

/* 执行单次 Modbus 读/写：加锁、重连、错误处理、事件通知均在此完成。
 * event_cb 在锁外回调，避免上层在回调中再次调用驱动时死锁。 */
static sw_err_t mb_execute(drv_vfd_t *vfd, mb_op_t *op)
{
    int              rc;
    bool             notify_lost     = false;
    bool             notify_restored = false;
    bool             need_reconnect  = false;
    pthread_mutex_t *bus_mtx;
    void           (*cb)(int);

    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    bus_mtx = (pthread_mutex_t *)vfd->bus_lock;
    (void)pthread_mutex_lock(bus_mtx);

    /* mb_connected 为 false 时重连；用标志位区分已连接与连接失败两种状态 */
    if (!vfd->mb_connected && mb_reconnect_locked(vfd) != SW_OK) {
        mb_on_failure_locked(vfd, &notify_lost, &need_reconnect);
        cb = vfd->event_cb;
        (void)pthread_mutex_unlock(bus_mtx);
        if (notify_lost && (cb != NULL)) {
            cb(DRV_VFD_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    if (op->is_write) {
        rc = modbus_write_register(vfd->mb, (int)op->addr, (int)op->wval);
    } else {
        uint16_t buf = 0U;
        rc = modbus_read_registers(vfd->mb, (int)op->addr, 1, &buf);
        if (rc >= 0) {
            op->rval = buf;
        }
    }

    if (rc < 0) {
        mb_on_failure_locked(vfd, &notify_lost, &need_reconnect);
        if (need_reconnect && (mb_reconnect_locked(vfd) != SW_OK)) {
            LOG_WARN("drv_vfd[addr=%d]: reconnect failed", vfd->modbus_addr);
        }
        cb = vfd->event_cb;
        (void)pthread_mutex_unlock(bus_mtx);
        LOG_ERROR("drv_vfd[addr=%d]: Modbus %s reg 0x%04X failed",
                  vfd->modbus_addr, op->is_write ? "write" : "read", op->addr);
        if (notify_lost && (cb != NULL)) {
            cb(DRV_VFD_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    mb_on_success_locked(vfd, &notify_restored);
    cb = vfd->event_cb;
    (void)pthread_mutex_unlock(bus_mtx);
    if (notify_restored && (cb != NULL)) {
        cb(DRV_VFD_EVT_COMM_RESTORED);
    }
    return SW_OK;
}

static sw_err_t mb_write_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t val)
{
    mb_op_t op = { true, addr, val, 0U };
    return mb_execute(vfd, &op);
}

static sw_err_t mb_read_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t *p_val)
{
    sw_err_t ret;
    mb_op_t  op = { false, addr, 0U, 0U };

    if (p_val == NULL) {
        return SW_ERR_PARAM;
    }
    ret = mb_execute(vfd, &op);
    if (ret == SW_OK) {
        *p_val = op.rval;
    }
    return ret;
}

/* -------------------------------------------------------------------------
 * 故障状态缓存更新（worker 和实时读共用，避免重复逻辑）
 * ------------------------------------------------------------------------- */
static void vfd_update_fault_state(drv_vfd_t *vfd, uint16_t code)
{
    bool          fire_detected = false;
    bool          fire_cleared  = false;
    bool          now_fault     = (code != 0U);
    void        (*cb)(int);

    (void)pthread_mutex_lock(&vfd->rst_mutex);
    vfd->cached_fault_code = code;
    if (now_fault && !vfd->fault_active) {
        vfd->fault_active = true;
        fire_detected = true;
    } else if (!now_fault && vfd->fault_active) {
        vfd->fault_active = false;
        fire_cleared = true;
    }
    cb = vfd->event_cb;
    (void)pthread_mutex_unlock(&vfd->rst_mutex);

    if (cb != NULL) {
        if (fire_detected) {
            cb(DRV_VFD_EVT_FAULT_DETECTED);
        } else if (fire_cleared) {
            cb(DRV_VFD_EVT_FAULT_CLEARED);
        }
    }
}

/* -------------------------------------------------------------------------
 * 速度 IO 操作（仅在 spd_io_ready 时调用）
 * ------------------------------------------------------------------------- */
static void vfd_spd_io_clear(drv_vfd_t *vfd)
{
    if (vfd->spd_io_ready) {
        vfd_do_set(vfd, vfd->pin_spd1, false);
        vfd_do_set(vfd, vfd->pin_spd2, false);
    }
}

static void vfd_spd_io_apply(drv_vfd_t *vfd, uint8_t abs_gear)
{
    uint8_t spd_state;

    if ((abs_gear == 0U) || (abs_gear > (uint8_t)VFD_GEAR_MAX)) {
        LOG_ERROR("drv_vfd: vfd_spd_io_apply 非法挡位 abs_gear=%u", (unsigned)abs_gear);
        return;
    }
    spd_state = vfd->spd_cfg[abs_gear - 1U];
    vfd_do_set(vfd, vfd->pin_spd1, (spd_state & 0x01U) != 0U);
    vfd_do_set(vfd, vfd->pin_spd2, (spd_state & 0x02U) != 0U);
}

/* 在 rst_mutex 保护下设置速度 IO 和方向 IO，并更新 gear 字段。
 * 调用方须已持有 rst_mutex。 */
static void vfd_apply_gear_locked(drv_vfd_t *vfd, drv_vfd_gear_t gear)
{
    int abs_gear = (gear < 0) ? -(int)gear : (int)gear;

    /* 速度 IO 先于方向 IO 输出，避免方向已切换但速度仍为旧挡的瞬态；
     * 未配置速度 IO（spd_io_ready=false）时跳过，仅做方向 IO 控制 */
    if (vfd->spd_io_ready) {
        vfd_spd_io_apply(vfd, (uint8_t)abs_gear);
    }
    if (gear > 0) {
        if (vfd->pin_rev.raw != IO_HANDLE_NULL) {
            vfd_do_set(vfd, vfd->pin_rev, false);
        }
        vfd_do_set(vfd, vfd->pin_fwd, true);
    } else {
        vfd_do_set(vfd, vfd->pin_fwd, false);
        if (vfd->pin_rev.raw != IO_HANDLE_NULL) {
            vfd_do_set(vfd, vfd->pin_rev, true);
        }
    }
    vfd->gear = gear;
}

/* -------------------------------------------------------------------------
 * Monitor worker：负责 RST 脉冲计时、方向切换延迟，以及慢速状态轮询（故障码、电流）
 * ------------------------------------------------------------------------- */
static void vfd_monitor_tick(uint32_t now_ms)
{
    size_t     i;
    size_t     count;
    drv_vfd_t *list_snap[VFD_MONITOR_INST_MAX];

    /* 在全局锁内完整复制指针列表，防止并发 drv_vfd_init 写 s_monitor_list 时的数据竞争 */
    (void)pthread_mutex_lock(&s_global_mutex);
    count = s_monitor_count;
    for (i = 0U; i < count; i++) {
        list_snap[i] = s_monitor_list[i];
    }
    (void)pthread_mutex_unlock(&s_global_mutex);

    for (i = 0U; i < count; i++) {
        drv_vfd_t *vfd = list_snap[i];

        if (vfd == NULL) {
            continue;
        }

        /* RST 脉冲到期拉低 + 方向切换延迟到期执行，共用 rst_mutex；
         * 同时取 gear/monitor_mask 快照，供锁外慢速轮询使用，避免数据竞争 */
        drv_vfd_gear_t         gear_snap;
        drv_vfd_monitor_mask_t mask_snap;

        (void)pthread_mutex_lock(&vfd->rst_mutex);
        if (vfd->rst_active && (time_elapsed_ms(vfd->rst_start_ms, now_ms) >= VFD_FAULT_RESET_PULSE_MS)) {
            vfd_do_set(vfd, vfd->pin_rst, false);
            vfd->rst_active = false;
        }
        if ((vfd->pending_gear != VFD_GEAR_STOP) &&
            (time_elapsed_ms(vfd->dir_change_start_ms, now_ms) >= VFD_DIR_SWITCH_DELAY_MS)) {
            vfd_apply_gear_locked(vfd, vfd->pending_gear);
            vfd->pending_gear = VFD_GEAR_STOP;
        }
        gear_snap = vfd->gear;
        mask_snap = vfd->monitor_mask;
        (void)pthread_mutex_unlock(&vfd->rst_mutex);

        /* 慢速轮询：用绝对时间差判断，避免 Modbus 超时导致间隔偏慢 */
        /* TODO: 当 FAULT 和 CURRENT 均启用时，可合并为一次 modbus_read_registers
         * 读取 0x2102~0x2104（3 个寄存器），将最大阻塞时间从 1s 降至 500ms */
        if (time_elapsed_ms(vfd->last_slow_poll_ms, now_ms) >= VFD_SLOW_POLL_MS) {
            vfd->last_slow_poll_ms = now_ms;

            if ((mask_snap & DRV_VFD_MON_FAULT) != 0U) {
                uint16_t code = 0U;

                if (mb_read_reg(vfd, VFD_REG_FAULT_CODE, &code) == SW_OK) {
                    vfd_update_fault_state(vfd, code);
                }
            }

            /* 仅运行中才采集电流，停止时保留上次缓存值 */
            if (((mask_snap & DRV_VFD_MON_CURRENT) != 0U) && (gear_snap != VFD_GEAR_STOP)) {
                uint16_t   cur = 0U;
                void     (*cb)(int);

                if (mb_read_reg(vfd, VFD_REG_CURRENT, &cur) == SW_OK) {
                    (void)pthread_mutex_lock(&vfd->rst_mutex);
                    vfd->cached_current = cur;
                    cb = vfd->event_cb;
                    (void)pthread_mutex_unlock(&vfd->rst_mutex);
                    if (cb != NULL) {
                        cb(DRV_VFD_EVT_CURRENT_UPDATE);
                    }
                }
            }
        }
    }
}

static void *vfd_monitor_worker(void *arg)
{
    struct timespec poll_ts;

    (void)arg;
    poll_ts.tv_sec  = 0;
    poll_ts.tv_nsec = (long)VFD_MONITOR_POLL_MS * 1000000L;

    for (;;) {
        vfd_monitor_tick(time_util_get_ms());
        (void)nanosleep(&poll_ts, NULL);
    }

    return NULL;
}

/* 保证 monitor worker 线程仅启动一次 */
static sw_err_t vfd_monitor_start(void)
{
    pthread_t tid;
    sw_err_t  ret = SW_OK;

    (void)pthread_mutex_lock(&s_global_mutex);
    if (!s_monitor_running) {
        if (pthread_create(&tid, NULL, vfd_monitor_worker, NULL) != 0) {
            LOG_ERROR("drv_vfd: monitor worker create failed");
            ret = SW_ERR_HW;
        } else {
            (void)pthread_detach(tid);
            s_monitor_running = true;
        }
    }
    (void)pthread_mutex_unlock(&s_global_mutex);
    return ret;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_vfd_init(drv_vfd_t        *vfd,
                      const char       *serial_port,
                      int               baud,
                      int               modbus_addr,
                      io_do_t           pin_fwd,
                      io_do_t           pin_rev,
                      io_do_t           pin_rst,
                      drv_vfd_do_set_fn do_set)
{
    sw_err_t ret;

    if ((vfd == NULL) || (serial_port == NULL) || (do_set == NULL)) {
        return SW_ERR_PARAM;
    }

    (void)memset(vfd, 0, sizeof(*vfd));

    ret = vfd_bus_port_bind(serial_port, baud, &vfd->bus_lock);
    if (ret != SW_OK) {
        return ret;
    }

    vfd->pin_fwd           = pin_fwd;
    vfd->pin_rev           = pin_rev;
    vfd->pin_rst           = pin_rst;
    /* pin_spd1/spd2/spd_cfg/spd_io_ready 由 memset 零初始化，表示速度 IO 尚未配置 */
    vfd->gear              = VFD_GEAR_STOP;
    vfd->event_cb          = NULL;
    vfd->comm_fail_count   = 0U;
    vfd->comm_ok           = true;
    vfd->serial_port       = serial_port;
    vfd->baud              = baud;
    vfd->modbus_addr       = modbus_addr;
    vfd->do_set            = do_set;
    vfd->mb_connected      = false;
    vfd->last_slow_poll_ms = 0U;
    vfd->monitor_mask      = DRV_VFD_MON_ALL;

    if (pthread_mutex_init(&vfd->rst_mutex, NULL) != 0) {
        LOG_ERROR("drv_vfd_init[addr=%d]: rst_mutex init failed", modbus_addr);
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock    = NULL;
        vfd->serial_port = NULL;
        return SW_ERR_HW;
    }

    /* 创建 Modbus 上下文（mb_ctx_create 与 mb_reconnect_locked 共用，避免重复代码）*/
    ret = mb_ctx_create(vfd);
    if (ret != SW_OK) {
        (void)pthread_mutex_destroy(&vfd->rst_mutex);
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock    = NULL;
        vfd->serial_port = NULL;
        return ret;
    }

    /* connect 失败时走 defer-link，不中止 init；后续读写时自动重连 */
    if (modbus_connect(vfd->mb) < 0) {
        LOG_WARN("drv_vfd_init[addr=%d]: modbus_connect failed, defer link", modbus_addr);
        vfd->comm_ok = false;
    } else {
        vfd->mb_connected = true;
    }

    /* 上电安全状态：所有控制输出关断 */
    vfd_do_set(vfd, pin_fwd, false);
    if (pin_rev.raw != IO_HANDLE_NULL) {
        vfd_do_set(vfd, pin_rev, false);
    }
    vfd_do_set(vfd, pin_rst, false);

    ret = vfd_monitor_list_add(vfd);
    if (ret != SW_OK) {
        (void)pthread_mutex_destroy(&vfd->rst_mutex);
        modbus_close(vfd->mb);
        modbus_free(vfd->mb);
        vfd->mb          = NULL;
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock    = NULL;
        vfd->serial_port = NULL;
        return ret;
    }

    ret = vfd_monitor_start();
    if (ret != SW_OK) {
        (void)pthread_mutex_lock(&s_global_mutex);
        s_monitor_count--;
        (void)pthread_mutex_unlock(&s_global_mutex);
        (void)pthread_mutex_destroy(&vfd->rst_mutex);
        modbus_close(vfd->mb);
        modbus_free(vfd->mb);
        vfd->mb          = NULL;
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock    = NULL;
        vfd->serial_port = NULL;
        return ret;
    }

    LOG_INFO("drv_vfd_init[addr=%d] ok", modbus_addr);
    return SW_OK;
}

sw_err_t drv_vfd_config_speed_io(drv_vfd_t     *vfd,
                                  io_do_t        pin_spd1,
                                  io_do_t        pin_spd2,
                                  const uint8_t  spd_cfg[VFD_GEAR_MAX])
{
    uint8_t i;

    if (!vfd_is_initialized(vfd) || (spd_cfg == NULL)) {
        return SW_ERR_PARAM;
    }
    if ((pin_spd1.raw == IO_HANDLE_NULL) || (pin_spd2.raw == IO_HANDLE_NULL)) {
        LOG_ERROR("drv_vfd_config_speed_io[addr=%d]: invalid speed IO pins", vfd->modbus_addr);
        return SW_ERR_PARAM;
    }

    /* 校验：(0,0) 与停止态冲突，禁止作为速度挡配置 */
    for (i = 0U; i < (uint8_t)VFD_GEAR_MAX; i++) {
        if (spd_cfg[i] == 0U) {
            LOG_ERROR("drv_vfd_config_speed_io[addr=%d]: gear %u uses VFD_SPD_IO(0,0), conflicts with stop",
                      vfd->modbus_addr, (unsigned)(i + 1U));
            return SW_ERR_PARAM;
        }
    }

    vfd->pin_spd1 = pin_spd1;
    vfd->pin_spd2 = pin_spd2;
    for (i = 0U; i < (uint8_t)VFD_GEAR_MAX; i++) {
        vfd->spd_cfg[i] = spd_cfg[i];
    }

    vfd_do_set(vfd, pin_spd1, false);
    vfd_do_set(vfd, pin_spd2, false);

    vfd->spd_io_ready = true;
    LOG_INFO("drv_vfd_config_speed_io[addr=%d] ok", vfd->modbus_addr);
    return SW_OK;
}

sw_err_t drv_vfd_run(drv_vfd_t *vfd, drv_vfd_gear_t gear)
{
    int abs_gear_int;

    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    /* 用 int 中间值计算绝对值，避免 int8_t 对 INT8_MIN 取负时溢出 */
    abs_gear_int = (gear < 0) ? -(int)gear : (int)gear;

    if (abs_gear_int > VFD_GEAR_MAX) {
        return SW_ERR_PARAM;
    }
    if ((gear < 0) && (vfd->pin_rev.raw == IO_HANDLE_NULL)) {
        LOG_ERROR("drv_vfd_run[addr=%d]: reverse not supported", vfd->modbus_addr);
        return SW_ERR_PARAM;
    }

    (void)pthread_mutex_lock(&vfd->rst_mutex);

    if (gear == VFD_GEAR_STOP) {
        /* 停止：立即关断所有 IO，取消待处理的方向切换 */
        vfd_spd_io_clear(vfd);
        vfd_do_set(vfd, vfd->pin_fwd, false);
        if (vfd->pin_rev.raw != IO_HANDLE_NULL) {
            vfd_do_set(vfd, vfd->pin_rev, false);
        }
        vfd->gear         = VFD_GEAR_STOP;
        vfd->pending_gear = VFD_GEAR_STOP;
        (void)pthread_mutex_unlock(&vfd->rst_mutex);
        return SW_OK;
    }

    /* 方向切换等待中，根据新指令方向决策 */
    if (vfd->pending_gear != VFD_GEAR_STOP) {
        if ((vfd->pending_gear > 0) == (gear > 0)) {
            /* 与待处理方向相同：更新目标挡位，继续等待，不重置计时器 */
            vfd->pending_gear = gear;
            (void)pthread_mutex_unlock(&vfd->rst_mutex);
            return SW_OK;
        }
        /* 与待处理方向相反（即与原运行方向相同）：
         * 电机正在减速，同向安全，取消等待并直接执行 */
        vfd->pending_gear = VFD_GEAR_STOP;
    }

    if ((vfd->gear != VFD_GEAR_STOP) && ((vfd->gear > 0) != (gear > 0))) {
        /* 运行中切换方向：立即停止 IO，设置 pending，非阻塞返回；
         * monitor worker 在延迟到期后自动应用 pending_gear */
        vfd_spd_io_clear(vfd);
        vfd_do_set(vfd, vfd->pin_fwd, false);
        if (vfd->pin_rev.raw != IO_HANDLE_NULL) {
            vfd_do_set(vfd, vfd->pin_rev, false);
        }
        vfd->gear                = VFD_GEAR_STOP;
        vfd->pending_gear        = gear;
        vfd->dir_change_start_ms = time_util_get_ms();
        (void)pthread_mutex_unlock(&vfd->rst_mutex);
        return SW_OK;
    }

    /* 同向换挡或从停止起步：立即执行 */
    vfd_apply_gear_locked(vfd, gear);

    (void)pthread_mutex_unlock(&vfd->rst_mutex);
    return SW_OK;
}

sw_err_t drv_vfd_set_freq(drv_vfd_t *vfd, uint16_t freq_hz)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }
    return mb_write_reg(vfd, VFD_REG_FREQ_SET, freq_hz);
}

sw_err_t drv_vfd_fault_reset(drv_vfd_t *vfd)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    (void)pthread_mutex_lock(&vfd->rst_mutex);

    /* 复位前关断所有运行输出，取消待处理的方向切换 */
    vfd_spd_io_clear(vfd);
    vfd_do_set(vfd, vfd->pin_fwd, false);
    if (vfd->pin_rev.raw != IO_HANDLE_NULL) {
        vfd_do_set(vfd, vfd->pin_rev, false);
    }
    vfd->gear         = VFD_GEAR_STOP;
    vfd->pending_gear = VFD_GEAR_STOP;

    vfd_do_set(vfd, vfd->pin_rst, true);
    vfd->rst_start_ms = time_util_get_ms();
    vfd->rst_active   = true;

    (void)pthread_mutex_unlock(&vfd->rst_mutex);

    return SW_OK;
}

drv_vfd_state_t drv_vfd_get_state(drv_vfd_t *vfd)
{
    drv_vfd_gear_t gear;

    if (vfd == NULL) {
        return DRV_VFD_STATE_STOPPED;
    }
    (void)pthread_mutex_lock(&vfd->rst_mutex);
    gear = vfd->gear;
    (void)pthread_mutex_unlock(&vfd->rst_mutex);
    if (gear > 0) {
        return DRV_VFD_STATE_FWD;
    }
    if (gear < 0) {
        return DRV_VFD_STATE_REV;
    }
    return DRV_VFD_STATE_STOPPED;
}

sw_err_t drv_vfd_get_fault_code(drv_vfd_t *vfd, uint16_t *p_code)
{
    sw_err_t ret;
    uint16_t code = 0U;

    if ((vfd == NULL) || (p_code == NULL)) {
        return SW_ERR_PARAM;
    }
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    ret = mb_read_reg(vfd, VFD_REG_FAULT_CODE, &code);
    if (ret == SW_OK) {
        *p_code = code;
        /* 同步缓存和 fault_active，保持与 monitor worker 维护的状态一致 */
        vfd_update_fault_state(vfd, code);
    }
    return ret;
}

sw_err_t drv_vfd_read_current(drv_vfd_t *vfd, uint16_t *p_current)
{
    if ((vfd == NULL) || (p_current == NULL)) {
        return SW_ERR_PARAM;
    }
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }
    return mb_read_reg(vfd, VFD_REG_CURRENT, p_current);
}

sw_err_t drv_vfd_read_status(drv_vfd_t *vfd, uint16_t *p_status)
{
    if ((vfd == NULL) || (p_status == NULL)) {
        return SW_ERR_PARAM;
    }
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }
    return mb_read_reg(vfd, VFD_REG_STATUS, p_status);
}

void drv_vfd_register_event_cb(drv_vfd_t *vfd, void (*cb)(int event_code))
{
    if (vfd != NULL) {
        (void)pthread_mutex_lock(&vfd->rst_mutex);
        vfd->event_cb = cb;
        (void)pthread_mutex_unlock(&vfd->rst_mutex);
    }
}

uint16_t drv_vfd_get_cached_fault_code(drv_vfd_t *vfd)
{
    uint16_t val;

    if (vfd == NULL) {
        return 0U;
    }
    (void)pthread_mutex_lock(&vfd->rst_mutex);
    val = vfd->cached_fault_code;
    (void)pthread_mutex_unlock(&vfd->rst_mutex);
    return val;
}

uint16_t drv_vfd_get_cached_current(drv_vfd_t *vfd)
{
    uint16_t val;

    if (vfd == NULL) {
        return 0U;
    }
    (void)pthread_mutex_lock(&vfd->rst_mutex);
    val = vfd->cached_current;
    (void)pthread_mutex_unlock(&vfd->rst_mutex);
    return val;
}

sw_err_t drv_vfd_set_monitor_mask(drv_vfd_t *vfd, drv_vfd_monitor_mask_t mask)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }
    (void)pthread_mutex_lock(&vfd->rst_mutex);
    vfd->monitor_mask = mask;
    (void)pthread_mutex_unlock(&vfd->rst_mutex);
    return SW_OK;
}
