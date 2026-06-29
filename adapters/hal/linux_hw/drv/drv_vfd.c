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
#define VFD_SHIHLIN_REG_FREQ_SET 0x2001U
#define VFD_SHIHLIN_REG_STATUS 0x2100U
#define VFD_SHIHLIN_REG_FAULT_CODE 0x2102U
#define VFD_SHIHLIN_REG_CURRENT 0x2104U

#define VFD_REG_FREQ_SET VFD_SHIHLIN_REG_FREQ_SET
#define VFD_REG_STATUS VFD_SHIHLIN_REG_STATUS
#define VFD_REG_FAULT_CODE VFD_SHIHLIN_REG_FAULT_CODE
#define VFD_REG_CURRENT VFD_SHIHLIN_REG_CURRENT

#define VFD_FAULT_RESET_PULSE_MS 200U
#define VFD_RST_POLL_MS          20U
#define VFD_SLOW_POLL_MS         2000U  /* 故障码 / 电流慢速轮询间隔 */
#define VFD_RST_REGISTRY_MAX     4U
#define VFD_MODBUS_TIMEOUT_US 500000U /* 500ms */
#define VFD_COMM_FAIL_RECONNECT 50U   /* 连续失败 N 次后重建 Modbus 连接 */
#define VFD_COMM_FAIL_NOTIFY 3U       /* 连续失败 N 次后通知上层 */

#define VFD_BUS_PORT_MAX 4U
#define VFD_BUS_PORT_PATH_MAX 64U

typedef struct
{
    char port_path[VFD_BUS_PORT_PATH_MAX];
    int baud;
    pthread_mutex_t mutex;
    bool mutex_inited;
    uint8_t ref_count;
    bool in_use;
} vfd_bus_port_t;

static vfd_bus_port_t s_bus_ports[VFD_BUS_PORT_MAX];

static bool       s_rst_worker_running;
static drv_vfd_t *s_vfd_registry[VFD_RST_REGISTRY_MAX];
static size_t     s_vfd_registry_count;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */
static sw_err_t vfd_bus_port_bind(const char *serial_port, int baud, void **out_lock)
{
    size_t i;
    size_t free_idx = VFD_BUS_PORT_MAX;

    if ((serial_port == NULL) || (out_lock == NULL))
    {
        return SW_ERR_PARAM;
    }

    for (i = 0U; i < VFD_BUS_PORT_MAX; i++)
    {
        if (s_bus_ports[i].in_use && (strcmp(s_bus_ports[i].port_path, serial_port) == 0))
        {
            if (s_bus_ports[i].baud != baud)
            {
                LOG_ERROR("drv_vfd: port %s baud mismatch %d vs %d", serial_port, baud, s_bus_ports[i].baud);
                return SW_ERR_PARAM;
            }
            if (s_bus_ports[i].ref_count < 0xFFU)
            {
                s_bus_ports[i].ref_count++;
            }
            *out_lock = &s_bus_ports[i].mutex;
            return SW_OK;
        }
        if (!s_bus_ports[i].in_use && (free_idx == VFD_BUS_PORT_MAX))
        {
            free_idx = i;
        }
    }

    if (free_idx >= VFD_BUS_PORT_MAX)
    {
        LOG_ERROR("drv_vfd: bus port table full");
        return SW_ERR_HW;
    }

    (void)memset(s_bus_ports[free_idx].port_path, 0, sizeof(s_bus_ports[free_idx].port_path));
    (void)strncpy(s_bus_ports[free_idx].port_path, serial_port, VFD_BUS_PORT_PATH_MAX - 1U);
    s_bus_ports[free_idx].port_path[VFD_BUS_PORT_PATH_MAX - 1U] = '\0';
    s_bus_ports[free_idx].baud = baud;
    s_bus_ports[free_idx].ref_count = 1U;
    s_bus_ports[free_idx].mutex_inited = false;

    /* 先初始化 mutex，成功后才标记 in_use=true，避免并发调用拿到未初始化的 mutex */
    if (pthread_mutex_init(&s_bus_ports[free_idx].mutex, NULL) != 0)
    {
        s_bus_ports[free_idx].port_path[0] = '\0';
        s_bus_ports[free_idx].baud = 0;
        s_bus_ports[free_idx].ref_count = 0U;
        LOG_ERROR("drv_vfd: pthread_mutex_init failed for %s", serial_port);
        return SW_ERR_HW;
    }
    s_bus_ports[free_idx].mutex_inited = true;
    s_bus_ports[free_idx].in_use = true;
    *out_lock = &s_bus_ports[free_idx].mutex;
    return SW_OK;
}

static void vfd_bus_port_unbind(void *bus_lock)
{
    size_t i;

    if (bus_lock == NULL)
    {
        return;
    }

    for (i = 0U; i < VFD_BUS_PORT_MAX; i++)
    {
        if (!s_bus_ports[i].in_use || ((void *)&s_bus_ports[i].mutex != bus_lock))
        {
            continue;
        }
        if (s_bus_ports[i].ref_count > 0U)
        {
            s_bus_ports[i].ref_count--;
        }
        if (s_bus_ports[i].ref_count == 0U)
        {
            if (s_bus_ports[i].mutex_inited)
            {
                (void)pthread_mutex_destroy(&s_bus_ports[i].mutex);
            }
            s_bus_ports[i].in_use = false;
            s_bus_ports[i].mutex_inited = false;
            s_bus_ports[i].baud = 0;
            s_bus_ports[i].port_path[0] = '\0';
        }
        return;
    }
}

static bool vfd_is_initialized(const drv_vfd_t *vfd)
{
    return (vfd != NULL) && (vfd->serial_port != NULL) && (vfd->baud > 0) && (vfd->modbus_addr > 0) &&
           (vfd->do_set != NULL);
}

static void vfd_do_set(drv_vfd_t *vfd, io_do_t pin, bool val)
{
    if ((vfd != NULL) && (vfd->do_set != NULL))
    {
        (void)vfd->do_set(pin, val);
    }
}

static sw_err_t vfd_rst_registry_add(drv_vfd_t *vfd)
{
    if (s_vfd_registry_count >= VFD_RST_REGISTRY_MAX)
    {
        LOG_ERROR("drv_vfd: rst registry full");
        return SW_ERR_HW;
    }
    s_vfd_registry[s_vfd_registry_count] = vfd;
    s_vfd_registry_count++;
    return SW_OK;
}

static void vfd_rst_worker_tick(uint32_t now_ms)
{
    size_t i;

    for (i = 0U; i < s_vfd_registry_count; i++)
    {
        drv_vfd_t *vfd = s_vfd_registry[i];
        bool       do_slow_poll;

        if (vfd == NULL)
        {
            continue;
        }

        /* RST 脉冲处理（加锁保护与 drv_vfd_fault_reset 的竞争）*/
        (void)pthread_mutex_lock(&vfd->rst_mutex);
        if (vfd->rst_active && (time_elapsed_ms(vfd->rst_start_ms, now_ms) >= VFD_FAULT_RESET_PULSE_MS))
        {
            vfd_do_set(vfd, vfd->pin_rst, false);
            vfd->rst_active = false;
        }
        (void)pthread_mutex_unlock(&vfd->rst_mutex);

        /* 慢速轮询：每 VFD_SLOW_POLL_MS 执行一次 Modbus 状态采集 */
        vfd->slow_poll_ms += VFD_RST_POLL_MS;
        do_slow_poll = (vfd->slow_poll_ms >= VFD_SLOW_POLL_MS);
        if (do_slow_poll)
        {
            uint16_t code = 0U;

            vfd->slow_poll_ms = 0U;

            /* 轮询故障码，状态变化时触发 event_cb */
            if (mb_read_reg(vfd, VFD_REG_FAULT_CODE, &code) == SW_OK)
            {
                bool now_fault = (code != 0U);

                vfd->cached_fault_code = code;
                if (now_fault && !vfd->fault_active)
                {
                    vfd->fault_active = true;
                    if (vfd->event_cb != NULL)
                    {
                        vfd->event_cb(DRV_VFD_EVT_FAULT_DETECTED);
                    }
                }
                else if (!now_fault && vfd->fault_active)
                {
                    vfd->fault_active = false;
                    if (vfd->event_cb != NULL)
                    {
                        vfd->event_cb(DRV_VFD_EVT_FAULT_CLEARED);
                    }
                }
            }

            /* 仅运行中才采集电流 */
            if (vfd->state != DRV_VFD_STATE_STOPPED)
            {
                uint16_t cur = 0U;

                if (mb_read_reg(vfd, VFD_REG_CURRENT, &cur) == SW_OK)
                {
                    vfd->cached_current = cur;
                    if (vfd->event_cb != NULL)
                    {
                        vfd->event_cb(DRV_VFD_EVT_CURRENT_UPDATE);
                    }
                }
            }
        }
    }
}

static void *vfd_rst_worker(void *arg)
{
    struct timespec poll_ts;

    (void)arg;
    poll_ts.tv_sec  = 0;
    poll_ts.tv_nsec = (long)VFD_RST_POLL_MS * 1000000L;

    for (;;)
    {
        vfd_rst_worker_tick(time_util_get_ms());
        (void)nanosleep(&poll_ts, NULL);
    }
}

static sw_err_t vfd_rst_worker_ensure(void)
{
    pthread_t tid;

    if (s_rst_worker_running)
    {
        return SW_OK;
    }

    if (pthread_create(&tid, NULL, vfd_rst_worker, NULL) != 0)
    {
        LOG_ERROR("drv_vfd: rst worker create failed");
        return SW_ERR_HW;
    }
    (void)pthread_detach(tid);
    s_rst_worker_running = true;
    return SW_OK;
}

static void vfd_bus_lock(drv_vfd_t *vfd)
{
    pthread_mutex_t *lock;

    if (vfd == NULL)
    {
        return;
    }
    lock = (pthread_mutex_t *)vfd->bus_lock;
    if (lock != NULL)
    {
        (void)pthread_mutex_lock(lock);
    }
}

static void vfd_bus_unlock(drv_vfd_t *vfd)
{
    pthread_mutex_t *lock;

    if (vfd == NULL)
    {
        return;
    }
    lock = (pthread_mutex_t *)vfd->bus_lock;
    if (lock != NULL)
    {
        (void)pthread_mutex_unlock(lock);
    }
}

static sw_err_t mb_reconnect_locked(drv_vfd_t *vfd)
{
    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }

    /* 统一先释放旧上下文再重建，避免 close 不 free 造成内存泄漏 */
    if (vfd->mb != NULL)
    {
        modbus_close(vfd->mb);
        modbus_free(vfd->mb);
        vfd->mb = NULL;
    }
    vfd->mb_connected = false;

    vfd->mb = modbus_new_rtu(vfd->serial_port, vfd->baud, 'N', 8, 1);
    if (vfd->mb == NULL)
    {
        LOG_ERROR("drv_vfd[addr=%d]: modbus_new_rtu failed", vfd->modbus_addr);
        return SW_ERR_HW;
    }
    modbus_set_slave(vfd->mb, vfd->modbus_addr);
    modbus_set_response_timeout(vfd->mb, 0, VFD_MODBUS_TIMEOUT_US);

    if (modbus_connect(vfd->mb) < 0)
    {
        LOG_ERROR("drv_vfd[addr=%d]: modbus_connect failed", vfd->modbus_addr);
        modbus_free(vfd->mb);
        vfd->mb = NULL;
        return SW_ERR_COMM;
    }

    vfd->mb_connected = true;
    LOG_INFO("drv_vfd[addr=%d]: Modbus linked", vfd->modbus_addr);
    return SW_OK;
}

/* 通过 mb_connected 标志判断连接状态，避免直接调 modbus_connect 无法区分已连接和连接失败 */
static sw_err_t mb_ensure_linked_locked(drv_vfd_t *vfd)
{
    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }
    if (vfd->mb_connected)
    {
        return SW_OK;
    }
    return mb_reconnect_locked(vfd);
}

static void mb_on_success_locked(drv_vfd_t *vfd, bool *notify_restored)
{
    if (!vfd->comm_ok)
    {
        vfd->comm_ok = true;
        if (notify_restored != NULL)
        {
            *notify_restored = true;
        }
    }
    vfd->comm_fail_count = 0U;
}

static void mb_on_failure_locked(drv_vfd_t *vfd, bool *notify_lost, bool *need_reconnect)
{
    vfd->mb_connected = false;

    if (vfd->comm_fail_count < 0xFFFFU)
    {
        vfd->comm_fail_count++;
    }

    if ((vfd->comm_fail_count >= VFD_COMM_FAIL_NOTIFY) && vfd->comm_ok)
    {
        vfd->comm_ok = false;
        if (notify_lost != NULL)
        {
            *notify_lost = true;
        }
    }

    if (vfd->comm_fail_count >= VFD_COMM_FAIL_RECONNECT)
    {
        vfd->comm_fail_count = 0U;
        if (need_reconnect != NULL)
        {
            *need_reconnect = true;
        }
    }
}

static sw_err_t mb_write_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t val)
{
    int rc;
    bool notify_lost = false;
    bool notify_restored = false;
    bool need_reconnect = false;

    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }

    vfd_bus_lock(vfd);

    if (mb_ensure_linked_locked(vfd) != SW_OK)
    {
        mb_on_failure_locked(vfd, &notify_lost, &need_reconnect);
        vfd_bus_unlock(vfd);
        if (notify_lost && (vfd->event_cb != NULL))
        {
            vfd->event_cb(DRV_VFD_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    rc = modbus_write_register(vfd->mb, (int)addr, (int)val);
    if (rc < 0)
    {
        mb_on_failure_locked(vfd, &notify_lost, &need_reconnect);
        if (need_reconnect)
        {
            (void)mb_reconnect_locked(vfd);
        }
        vfd_bus_unlock(vfd);
        LOG_ERROR("drv_vfd[addr=%d]: Modbus write reg 0x%04X failed", vfd->modbus_addr, addr);
        if (notify_lost && (vfd->event_cb != NULL))
        {
            vfd->event_cb(DRV_VFD_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    mb_on_success_locked(vfd, &notify_restored);
    vfd_bus_unlock(vfd);
    if (notify_restored && (vfd->event_cb != NULL))
    {
        vfd->event_cb(DRV_VFD_EVT_COMM_RESTORED);
    }
    return SW_OK;
}

static sw_err_t mb_read_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t *p_val)
{
    uint16_t buf = 0U;
    int rc;
    bool notify_lost = false;
    bool notify_restored = false;
    bool need_reconnect = false;

    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }

    vfd_bus_lock(vfd);

    if (mb_ensure_linked_locked(vfd) != SW_OK)
    {
        mb_on_failure_locked(vfd, &notify_lost, &need_reconnect);
        vfd_bus_unlock(vfd);
        if (notify_lost && (vfd->event_cb != NULL))
        {
            vfd->event_cb(DRV_VFD_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    rc = modbus_read_registers(vfd->mb, (int)addr, 1, &buf);
    if (rc < 0)
    {
        mb_on_failure_locked(vfd, &notify_lost, &need_reconnect);
        if (need_reconnect)
        {
            (void)mb_reconnect_locked(vfd);
        }
        vfd_bus_unlock(vfd);
        if (notify_lost && (vfd->event_cb != NULL))
        {
            vfd->event_cb(DRV_VFD_EVT_COMM_LOST);
        }
        return SW_ERR_COMM;
    }

    mb_on_success_locked(vfd, &notify_restored);
    vfd_bus_unlock(vfd);
    if (notify_restored && (vfd->event_cb != NULL))
    {
        vfd->event_cb(DRV_VFD_EVT_COMM_RESTORED);
    }
    *p_val = buf;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_vfd_init(drv_vfd_t *vfd,
                      const char *serial_port,
                      int baud,
                      int modbus_addr,
                      io_do_t pin_fwd,
                      bool has_rev,
                      io_do_t pin_rev,
                      io_do_t pin_rst,
                      drv_vfd_do_set_fn do_set)
{
    sw_err_t ret;

    if ((vfd == NULL) || (serial_port == NULL) || (do_set == NULL))
    {
        return SW_ERR_PARAM;
    }

    (void)memset(vfd, 0, sizeof(*vfd));

    ret = vfd_bus_port_bind(serial_port, baud, &vfd->bus_lock);
    if (ret != SW_OK)
    {
        return ret;
    }

    vfd->pin_fwd         = pin_fwd;
    vfd->pin_rev         = pin_rev;
    vfd->has_rev         = has_rev;
    vfd->pin_rst         = pin_rst;
    vfd->state           = DRV_VFD_STATE_STOPPED;
    vfd->event_cb        = NULL;
    vfd->comm_fail_count = 0U;
    vfd->comm_ok         = true;
    vfd->serial_port     = serial_port;
    vfd->baud            = baud;
    vfd->modbus_addr     = modbus_addr;
    vfd->do_set          = do_set;
    vfd->mb_connected    = false;
    vfd->slow_poll_ms    = 0U;

    if (pthread_mutex_init(&vfd->rst_mutex, NULL) != 0)
    {
        LOG_ERROR("drv_vfd_init[addr=%d]: rst_mutex init failed", modbus_addr);
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock = NULL;
        return SW_ERR_HW;
    }

    vfd->mb = modbus_new_rtu(serial_port, baud, 'N', 8, 1);
    if (vfd->mb == NULL)
    {
        LOG_ERROR("drv_vfd_init[addr=%d]: modbus_new_rtu failed", modbus_addr);
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock = NULL;
        return SW_ERR_HW;
    }

    modbus_set_slave(vfd->mb, modbus_addr);
    modbus_set_response_timeout(vfd->mb, 0, VFD_MODBUS_TIMEOUT_US);

    if (modbus_connect(vfd->mb) < 0)
    {
        LOG_WARN("drv_vfd_init[addr=%d]: modbus_connect failed, defer link", modbus_addr);
        vfd->comm_ok = false;
    }
    else
    {
        vfd->mb_connected = true;
    }

    /* 上电安全状态：所有控制输出关断 */
    vfd_do_set(vfd, pin_fwd, false);
    if (has_rev)
    {
        vfd_do_set(vfd, pin_rev, false);
    }
    vfd_do_set(vfd, pin_rst, false);

    ret = vfd_rst_registry_add(vfd);
    if (ret != SW_OK)
    {
        (void)pthread_mutex_destroy(&vfd->rst_mutex);
        modbus_close(vfd->mb);
        modbus_free(vfd->mb);
        vfd->mb = NULL;
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock = NULL;
        return ret;
    }

    ret = vfd_rst_worker_ensure();
    if (ret != SW_OK)
    {
        s_vfd_registry_count--;
        (void)pthread_mutex_destroy(&vfd->rst_mutex);
        modbus_close(vfd->mb);
        modbus_free(vfd->mb);
        vfd->mb = NULL;
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock = NULL;
        return ret;
    }

    LOG_INFO("drv_vfd_init[addr=%d] ok", modbus_addr);
    return SW_OK;
}

sw_err_t drv_vfd_run_fwd(drv_vfd_t *vfd, uint16_t freq_hz)
{
    sw_err_t ret;

    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }
    if (freq_hz == 0U)
    {
        return drv_vfd_stop(vfd);
    }

    ret = mb_write_reg(vfd, VFD_REG_FREQ_SET, freq_hz);
    if (ret != SW_OK)
    {
        return ret;
    }

    if (vfd->has_rev)
    {
        vfd_do_set(vfd, vfd->pin_rev, false);
    }
    vfd_do_set(vfd, vfd->pin_fwd, true);

    vfd->state = DRV_VFD_STATE_FWD;
    return SW_OK;
}

sw_err_t drv_vfd_run_rev(drv_vfd_t *vfd, uint16_t freq_hz)
{
    sw_err_t ret;

    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }
    if (!vfd->has_rev)
    {
        LOG_ERROR("drv_vfd_run_rev: this VFD instance does not support reverse");
        return SW_ERR_PARAM;
    }
    if (freq_hz == 0U)
    {
        return drv_vfd_stop(vfd);
    }

    ret = mb_write_reg(vfd, VFD_REG_FREQ_SET, freq_hz);
    if (ret != SW_OK)
    {
        return ret;
    }

    vfd_do_set(vfd, vfd->pin_fwd, false);
    vfd_do_set(vfd, vfd->pin_rev, true);

    vfd->state = DRV_VFD_STATE_REV;
    return SW_OK;
}

sw_err_t drv_vfd_stop(drv_vfd_t *vfd)
{
    if (vfd == NULL)
    {
        return SW_ERR_PARAM;
    }

    vfd_do_set(vfd, vfd->pin_fwd, false);
    if (vfd->has_rev)
    {
        vfd_do_set(vfd, vfd->pin_rev, false);
    }

    vfd->state = DRV_VFD_STATE_STOPPED;
    return SW_OK;
}

sw_err_t drv_vfd_fault_reset(drv_vfd_t *vfd)
{
    if (vfd == NULL)
    {
        return SW_ERR_PARAM;
    }

    vfd_do_set(vfd, vfd->pin_rst, true);

    /* 加锁保证 rst_start_ms 和 rst_active 对 worker 线程的可见顺序 */
    (void)pthread_mutex_lock(&vfd->rst_mutex);
    vfd->rst_start_ms = time_util_get_ms();
    vfd->rst_active   = true;
    (void)pthread_mutex_unlock(&vfd->rst_mutex);

    vfd->state = DRV_VFD_STATE_STOPPED;
    return SW_OK;
}

drv_vfd_state_t drv_vfd_get_state(const drv_vfd_t *vfd)
{
    if (vfd == NULL)
    {
        return DRV_VFD_STATE_STOPPED;
    }
    return vfd->state;
}

sw_err_t drv_vfd_get_fault_code(drv_vfd_t *vfd, uint16_t *p_code)
{
    if (vfd == NULL || p_code == NULL)
    {
        return SW_ERR_PARAM;
    }
    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }
    return mb_read_reg(vfd, VFD_REG_FAULT_CODE, p_code);
}

sw_err_t drv_vfd_read_current(drv_vfd_t *vfd, uint16_t *p_current)
{
    if ((vfd == NULL) || (p_current == NULL))
    {
        return SW_ERR_PARAM;
    }
    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }
    return mb_read_reg(vfd, VFD_REG_CURRENT, p_current);
}

sw_err_t drv_vfd_read_status(drv_vfd_t *vfd, uint16_t *p_status)
{
    if ((vfd == NULL) || (p_status == NULL))
    {
        return SW_ERR_PARAM;
    }
    if (!vfd_is_initialized(vfd))
    {
        return SW_ERR_NOT_INIT;
    }
    return mb_read_reg(vfd, VFD_REG_STATUS, p_status);
}

void drv_vfd_register_event_cb(drv_vfd_t *vfd, void (*cb)(int event_code))
{
    if (vfd != NULL)
    {
        vfd->event_cb = cb;
    }
}

uint16_t drv_vfd_get_cached_fault_code(const drv_vfd_t *vfd)
{
    return (vfd != NULL) ? vfd->cached_fault_code : 0U;
}

uint16_t drv_vfd_get_cached_current(const drv_vfd_t *vfd)
{
    return (vfd != NULL) ? vfd->cached_current : 0U;
}
