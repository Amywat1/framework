/**
 * @file    drv_modbus_link.c
 * @brief   Modbus RTU 链路层实现（连接管理 + 总线锁共享 + 失败重连）
 * @author  HUWANGWEI
 * @date    2026-07-07
 */

#include "common/log.h"
#include "common/sw_mutex.h"
#include "drv_modbus_link_internal.h"
#include "modbus/modbus-rtu.h"
#include "modbus/modbus.h"

#include <pthread.h>
#include <string.h>

#define LINK_BUS_PORT_MAX      4U
#define LINK_BUS_PORT_PATH_MAX 64U

/* 同一串口上挂载多个实例时共享一把总线互斥锁，通过 ref_count 追踪引用 */
typedef struct {
    char            port_path[LINK_BUS_PORT_PATH_MAX];
    int             baud;
    pthread_mutex_t mutex;
    bool            mutex_inited;
    uint8_t         ref_count;
    bool            in_use;
} link_bus_port_t;

static link_bus_port_t s_bus_ports[LINK_BUS_PORT_MAX];

/* 本锁只保护总线端口表的分配与释放（init/deinit），不在急停链路上。
 * 切断禁止走 Modbus，本文件登记为非 RT。仍启用优先级继承，使本文件内的锁
 * 处理方式一致。用 pthread_once 是因为静态初始化过的互斥量无法再调用
 * pthread_mutex_init（POSIX 未定义行为）。 */
static pthread_mutex_t s_global_mutex;
static pthread_once_t  s_global_mutex_once = PTHREAD_ONCE_INIT;

static void link_global_mutex_init_once(void)
{
    (void)sw_mutex_init_prio_inherit(&s_global_mutex);
}

/** @brief 确保总线表锁已初始化（幂等，所有加锁点入口调用）*/
static void link_global_mutex_ready(void)
{
    (void)pthread_once(&s_global_mutex_once, link_global_mutex_init_once);
}

/* -------------------------------------------------------------------------
 * 总线端口表管理
 * ------------------------------------------------------------------------- */
static sw_err_t link_bus_port_bind(const char *serial_port, int baud, void **out_lock)
{
    size_t   i;
    size_t   free_idx = LINK_BUS_PORT_MAX;
    sw_err_t ret      = SW_OK;

    if ((serial_port == NULL) || (out_lock == NULL)) {
        return SW_ERR_PARAM;
    }

    link_global_mutex_ready();
    (void)pthread_mutex_lock(&s_global_mutex);

    for (i = 0U; i < LINK_BUS_PORT_MAX; i++) {
        if (s_bus_ports[i].in_use && (strcmp(s_bus_ports[i].port_path, serial_port) == 0)) {
            if (s_bus_ports[i].baud != baud) {
                LOG_ERROR("drv_modbus_link: port %s baud mismatch %d vs %d", serial_port, baud, s_bus_ports[i].baud);
                ret = SW_ERR_PARAM;
                goto out;
            }
            if (s_bus_ports[i].ref_count < 0xFFU) {
                s_bus_ports[i].ref_count++;
            }
            *out_lock = &s_bus_ports[i].mutex;
            goto out;
        }
        if (!s_bus_ports[i].in_use && (free_idx == LINK_BUS_PORT_MAX)) {
            free_idx = i;
        }
    }

    if (free_idx >= LINK_BUS_PORT_MAX) {
        LOG_ERROR("drv_modbus_link: bus port table full");
        ret = SW_ERR_HW;
        goto out;
    }

    (void)memset(s_bus_ports[free_idx].port_path, 0, sizeof(s_bus_ports[free_idx].port_path));
    (void)strncpy(s_bus_ports[free_idx].port_path, serial_port, LINK_BUS_PORT_PATH_MAX - 1U);
    s_bus_ports[free_idx].port_path[LINK_BUS_PORT_PATH_MAX - 1U] = '\0';
    s_bus_ports[free_idx].baud                                   = baud;
    s_bus_ports[free_idx].ref_count                              = 1U;
    s_bus_ports[free_idx].mutex_inited                           = false;

    /* 切断路径禁止走 Modbus（见 safety_port.h）。本锁只序列化业务帧，不在急停
     * 链路上。仍启用优先级继承：同一文件内的锁统一处理，避免后来者按锁逐个
     * 判断 RT 可达性。 */
    if (!sw_mutex_init_prio_inherit(&s_bus_ports[free_idx].mutex)) {
        LOG_WARN("drv_modbus_link: 优先级继承不可用，已退化为默认互斥量 (%s)", serial_port);
    }
    s_bus_ports[free_idx].mutex_inited = true;
    s_bus_ports[free_idx].in_use       = true;
    *out_lock                          = &s_bus_ports[free_idx].mutex;

out:
    (void)pthread_mutex_unlock(&s_global_mutex);
    return ret;
}

static void link_bus_port_unbind(void *bus_lock)
{
    size_t i;

    if (bus_lock == NULL) {
        return;
    }

    link_global_mutex_ready();
    (void)pthread_mutex_lock(&s_global_mutex);

    for (i = 0U; i < LINK_BUS_PORT_MAX; i++) {
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
 * Modbus 连接管理
 * ------------------------------------------------------------------------- */
static sw_err_t link_mb_ctx_create(drv_modbus_link_t *link)
{
    if (link->mb != NULL) {
        modbus_close(link->mb);
        modbus_free(link->mb);
        link->mb = NULL;
    }
    link->mb_connected = false;

    link->mb = modbus_new_rtu(link->serial_port, link->baud, 'N', 8, 1);
    if (link->mb == NULL) {
        LOG_ERROR("drv_modbus_link[addr=%d]: modbus_new_rtu failed", link->modbus_addr);
        return SW_ERR_HW;
    }
    modbus_set_slave(link->mb, link->modbus_addr);
    /* connect 前设置：官方 libmodbus 在 open 后按此标志下发 TIOCSRS485 */
    (void)modbus_rtu_set_serial_mode(link->mb, MODBUS_RTU_RS485);
    (void)modbus_set_error_recovery(link->mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
    modbus_set_response_timeout(link->mb, 0, link->timeout_us);
    return SW_OK;
}

static sw_err_t link_reconnect_locked(drv_modbus_link_t *link)
{
    sw_err_t ret;

    if (!drv_modbus_link_is_ready(link)) {
        return SW_ERR_NOT_INIT;
    }

    ret = link_mb_ctx_create(link);
    if (ret != SW_OK) {
        return ret;
    }

    if (modbus_connect(link->mb) < 0) {
        LOG_ERROR("drv_modbus_link[addr=%d]: modbus_connect failed", link->modbus_addr);
        modbus_free(link->mb);
        link->mb = NULL;
        return SW_ERR_COMM;
    }

    /* connect 后再设一次：部分板级库只在已打开的 fd 上执行 ioctl */
    (void)modbus_rtu_set_serial_mode(link->mb, MODBUS_RTU_RS485);
    link->mb_connected = true;
    LOG_INFO("drv_modbus_link[addr=%d]: Modbus linked", link->modbus_addr);
    return SW_OK;
}

static void link_on_success_locked(drv_modbus_link_t *link)
{
    link->comm_fail_count = 0U;
}

static void link_on_failure_locked(drv_modbus_link_t *link, bool *need_reconnect)
{
    if (link->comm_fail_count < 0xFFFFU) {
        link->comm_fail_count++;
    }

    /* 同口多从站已各自 open，单次超时不得立刻 close/reopen，否则会抢走 DE */
    if (link->comm_fail_count >= link->reconnect_threshold) {
        link->comm_fail_count = 0U;
        link->mb_connected    = false;
        if (need_reconnect != NULL) {
            *need_reconnect = true;
        }
    }
}

/* -------------------------------------------------------------------------
 * 读写统一执行路径
 * ------------------------------------------------------------------------- */
typedef struct {
    bool                  is_write;
    drv_modbus_reg_type_t read_type;
    uint16_t              addr;
    uint16_t              wval;
    uint16_t              rval;
} link_op_t;

static sw_err_t link_execute(drv_modbus_link_t *link, link_op_t *op)
{
    int              rc;
    bool             need_reconnect = false;
    pthread_mutex_t *bus_mtx;

    if (!drv_modbus_link_is_ready(link)) {
        return SW_ERR_NOT_INIT;
    }

    bus_mtx = (pthread_mutex_t *)link->bus_lock;
    (void)pthread_mutex_lock(bus_mtx);

    if (!link->mb_connected && link_reconnect_locked(link) != SW_OK) {
        link_on_failure_locked(link, &need_reconnect);
        (void)pthread_mutex_unlock(bus_mtx);
        return SW_ERR_COMM;
    }

    (void)modbus_flush(link->mb);

    if (op->is_write) {
        rc = modbus_write_register(link->mb, (int)op->addr, (int)op->wval);
    } else if (op->read_type == DRV_MODBUS_REG_HOLDING) {
        uint16_t buf = 0U;
        rc           = modbus_read_registers(link->mb, (int)op->addr, 1, &buf);
        if (rc >= 0) {
            op->rval = buf;
        }
    } else {
        uint16_t buf = 0U;
        rc           = modbus_read_input_registers(link->mb, (int)op->addr, 1, &buf);
        if (rc >= 0) {
            op->rval = buf;
        }
    }

    if (rc < 0) {
        link_on_failure_locked(link, &need_reconnect);
        (void)pthread_mutex_unlock(bus_mtx);
        /* 失败后不在持锁时重连：下一次读写会在入口处看到 mb_connected=false
         * 再连。把 connect 留在锁内会让同总线的其他设备多等一个超时。 */
        LOG_ERROR("drv_modbus_link[addr=%d]: Modbus %s reg 0x%04X failed%s",
                  link->modbus_addr,
                  op->is_write ? "write" : "read",
                  op->addr,
                  need_reconnect ? ", reconnect deferred" : "");
        return SW_ERR_COMM;
    }

    link_on_success_locked(link);
    (void)pthread_mutex_unlock(bus_mtx);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_modbus_link_init(drv_modbus_link_t *link,
                              const char        *serial_port,
                              int                baud,
                              int                modbus_addr,
                              uint32_t           timeout_us,
                              uint16_t           reconnect_threshold)
{
    sw_err_t ret;

    if ((link == NULL) || (serial_port == NULL)) {
        return SW_ERR_PARAM;
    }

    (void)memset(link, 0, sizeof(*link));

    ret = link_bus_port_bind(serial_port, baud, &link->bus_lock);
    if (ret != SW_OK) {
        return ret;
    }

    link->serial_port         = serial_port;
    link->baud                = baud;
    link->modbus_addr         = modbus_addr;
    link->timeout_us          = timeout_us;
    link->reconnect_threshold = reconnect_threshold;
    link->mb_connected        = false;

    ret = link_mb_ctx_create(link);
    if (ret != SW_OK) {
        link_bus_port_unbind(link->bus_lock);
        link->bus_lock    = NULL;
        link->serial_port = NULL;
        return ret;
    }

    if (modbus_connect(link->mb) < 0) {
        LOG_WARN("drv_modbus_link[addr=%d]: modbus_connect failed, defer link", modbus_addr);
    } else {
        link->mb_connected = true;
    }

    return SW_OK;
}

bool drv_modbus_link_is_ready(const drv_modbus_link_t *link)
{
    return (link != NULL) && (link->serial_port != NULL) && (link->baud > 0) && (link->modbus_addr > 0);
}

sw_err_t drv_modbus_link_read_reg(drv_modbus_link_t *link, drv_modbus_reg_type_t type, uint16_t addr, uint16_t *p_val)
{
    sw_err_t  ret;
    link_op_t op = {false, type, addr, 0U, 0U};

    if ((p_val == NULL) || ((type != DRV_MODBUS_REG_HOLDING) && (type != DRV_MODBUS_REG_INPUT))) {
        return SW_ERR_PARAM;
    }
    ret = link_execute(link, &op);
    if (ret == SW_OK) {
        *p_val = op.rval;
    }
    return ret;
}

sw_err_t drv_modbus_link_write_reg(drv_modbus_link_t *link, uint16_t addr, uint16_t val)
{
    link_op_t op = {true, DRV_MODBUS_REG_HOLDING, addr, val, 0U};
    return link_execute(link, &op);
}
