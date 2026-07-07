/**
 * @file    drv_modbus_link.c
 * @brief   Modbus RTU 链路层实现（连接管理 + 总线锁共享 + 失败重连）
 * @author  HUWANGWEI
 * @date    2026-07-07
 */

#include "drv_modbus_link.h"

#include "framework/common/log.h"
#include "modbus/modbus-rtu.h"

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
static pthread_mutex_t s_global_mutex = PTHREAD_MUTEX_INITIALIZER;

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

    if (pthread_mutex_init(&s_bus_ports[free_idx].mutex, NULL) != 0) {
        s_bus_ports[free_idx].port_path[0] = '\0';
        s_bus_ports[free_idx].baud         = 0;
        s_bus_ports[free_idx].ref_count    = 0U;
        LOG_ERROR("drv_modbus_link: pthread_mutex_init failed for %s", serial_port);
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

static void link_bus_port_unbind(void *bus_lock)
{
    size_t i;

    if (bus_lock == NULL) {
        return;
    }

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
    link->mb_connected = false;

    if (link->comm_fail_count < 0xFFFFU) {
        link->comm_fail_count++;
    }

    if (link->comm_fail_count >= link->reconnect_threshold) {
        link->comm_fail_count = 0U;
        if (need_reconnect != NULL) {
            *need_reconnect = true;
        }
    }
}

/* -------------------------------------------------------------------------
 * 读写统一执行路径
 * ------------------------------------------------------------------------- */
typedef struct {
    bool     is_write;
    uint16_t addr;
    uint16_t wval;
    uint16_t rval;
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

    if (op->is_write) {
        rc = modbus_write_register(link->mb, (int)op->addr, (int)op->wval);
    } else {
        uint16_t buf = 0U;
        rc           = modbus_read_registers(link->mb, (int)op->addr, 1, &buf);
        if (rc >= 0) {
            op->rval = buf;
        }
    }

    if (rc < 0) {
        link_on_failure_locked(link, &need_reconnect);
        if (need_reconnect && (link_reconnect_locked(link) != SW_OK)) {
            LOG_WARN("drv_modbus_link[addr=%d]: reconnect failed", link->modbus_addr);
        }
        (void)pthread_mutex_unlock(bus_mtx);
        LOG_ERROR("drv_modbus_link[addr=%d]: Modbus %s reg 0x%04X failed",
                  link->modbus_addr,
                  op->is_write ? "write" : "read",
                  op->addr);
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
                              const char         *serial_port,
                              int                 baud,
                              int                 modbus_addr,
                              uint32_t            timeout_us,
                              uint16_t            reconnect_threshold)
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

    link->serial_port          = serial_port;
    link->baud                 = baud;
    link->modbus_addr          = modbus_addr;
    link->timeout_us           = timeout_us;
    link->reconnect_threshold  = reconnect_threshold;
    link->mb_connected         = false;

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

sw_err_t drv_modbus_link_read_reg(drv_modbus_link_t *link, uint16_t addr, uint16_t *p_val)
{
    sw_err_t  ret;
    link_op_t op = {false, addr, 0U, 0U};

    if (p_val == NULL) {
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
    link_op_t op = {true, addr, val, 0U};
    return link_execute(link, &op);
}
