/**
 * @file    drv_vfd.c
 * @brief   变频器驱动实现（Modbus RTU + IO 数字量原语）
 * @author  HUWANGWEI
 * @date    2026-04-08
 */

#include "drv_vfd.h"

#include "framework/common/log.h"
#include "modbus/modbus-rtu.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 厂商寄存器地址（编译时选择，取消注释当前厂商块并注释其余）
 * VFD_REG_FREQ_SET    ：未定义时 drv_vfd_write(REG_FREQ) 返回 SW_ERR_PARAM
 * VFD_REG_CLEAR_FAULT ：未定义时 drv_vfd_write(REG_CLEAR_FAULT) 返回 SW_ERR_PARAM；
 *                       IO 复位须上层调用 drv_vfd_set_rst
 * ------------------------------------------------------------------------- */

/* //伟创 VFD
#define VFD_REG_STATE        0x1001U
#define VFD_REG_FAULT_CODE   0x1007U
#define VFD_REG_CURRENT      0x1004U
#define VFD_REG_CLEAR_FAULT  0x1101U
#define VFD_DATA_CLEAR_FAULT 0xA5A5U
*/

/* //台达 VFD
#define VFD_REG_STATE        0x2226U
#define VFD_REG_FAULT_CODE   0x2100U
#define VFD_REG_CURRENT      0x2104U
#define VFD_REG_FREQ_SET     0x2103U
#define VFD_REG_CLEAR_FAULT  0x2002U
#define VFD_DATA_CLEAR_FAULT 0x0002U
*/

/* //士林 VFD（清故障数据 0x1101 写入控制寄存器 0x1000）*/
#define VFD_REG_STATE        0x1001U
#define VFD_REG_FAULT_CODE   0x1007U
#define VFD_REG_CURRENT      0x1004U
#define VFD_REG_CLEAR_FAULT  0x1000U
#define VFD_DATA_CLEAR_FAULT 0x1101U


/* 翌腾 VFD（当前使用；无 Modbus 清故障寄存器，复位须上层通过 pin_rst 脉冲）
#define VFD_REG_STATE      0x1304U
#define VFD_REG_FAULT_CODE 0x1300U
#define VFD_REG_CURRENT    0x1206U
*/

#define VFD_MODBUS_TIMEOUT_US   100000U /* Modbus 响应超时 100ms */
#define VFD_COMM_FAIL_RECONNECT 50U     /* 连续失败 N 次后重建 Modbus 连接 */

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
static pthread_mutex_t s_global_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    s_bus_ports[free_idx].baud                                  = baud;
    s_bus_ports[free_idx].ref_count                             = 1U;
    s_bus_ports[free_idx].mutex_inited                          = false;

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

/* -------------------------------------------------------------------------
 * Modbus 连接管理
 * ------------------------------------------------------------------------- */

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

static void mb_on_success_locked(drv_vfd_t *vfd)
{
    vfd->comm_fail_count = 0U;
}

static void mb_on_failure_locked(drv_vfd_t *vfd, bool *need_reconnect)
{
    vfd->mb_connected = false;

    if (vfd->comm_fail_count < 0xFFFFU) {
        vfd->comm_fail_count++;
    }

    if (vfd->comm_fail_count >= VFD_COMM_FAIL_RECONNECT) {
        vfd->comm_fail_count = 0U;
        if (need_reconnect != NULL) {
            *need_reconnect = true;
        }
    }
}

typedef struct {
    bool     is_write;
    uint16_t addr;
    uint16_t wval;
    uint16_t rval;
} mb_op_t;

static sw_err_t mb_execute(drv_vfd_t *vfd, mb_op_t *op)
{
    int              rc;
    bool             need_reconnect = false;
    pthread_mutex_t *bus_mtx;

    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    bus_mtx = (pthread_mutex_t *)vfd->bus_lock;
    (void)pthread_mutex_lock(bus_mtx);

    if (!vfd->mb_connected && mb_reconnect_locked(vfd) != SW_OK) {
        mb_on_failure_locked(vfd, &need_reconnect);
        (void)pthread_mutex_unlock(bus_mtx);
        return SW_ERR_COMM;
    }

    if (op->is_write) {
        rc = modbus_write_register(vfd->mb, (int)op->addr, (int)op->wval);
    } else {
        uint16_t buf = 0U;
        rc           = modbus_read_registers(vfd->mb, (int)op->addr, 1, &buf);
        if (rc >= 0) {
            op->rval = buf;
        }
    }

    if (rc < 0) {
        mb_on_failure_locked(vfd, &need_reconnect);
        if (need_reconnect && (mb_reconnect_locked(vfd) != SW_OK)) {
            LOG_WARN("drv_vfd[addr=%d]: reconnect failed", vfd->modbus_addr);
        }
        (void)pthread_mutex_unlock(bus_mtx);
        LOG_ERROR("drv_vfd[addr=%d]: Modbus %s reg 0x%04X failed",
                  vfd->modbus_addr,
                  op->is_write ? "write" : "read",
                  op->addr);
        return SW_ERR_COMM;
    }

    mb_on_success_locked(vfd);
    (void)pthread_mutex_unlock(bus_mtx);
    return SW_OK;
}

static sw_err_t mb_write_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t val)
{
    mb_op_t op = {true, addr, val, 0U};
    return mb_execute(vfd, &op);
}

static sw_err_t mb_read_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t *p_val)
{
    sw_err_t ret;
    mb_op_t  op = {false, addr, 0U, 0U};

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

static void vfd_apply_gear_impl(drv_vfd_t *vfd, drv_vfd_gear_t gear)
{
    int abs_gear = (gear < 0) ? -(int)gear : (int)gear;

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

    vfd->pin_fwd         = pin_fwd;
    vfd->pin_rev         = pin_rev;
    vfd->pin_rst         = pin_rst;
    vfd->gear            = VFD_GEAR_STOP;
    vfd->serial_port     = serial_port;
    vfd->baud            = baud;
    vfd->modbus_addr     = modbus_addr;
    vfd->do_set          = do_set;
    vfd->mb_connected    = false;

    if (pthread_mutex_init(&vfd->io_mutex, NULL) != 0) {
        LOG_ERROR("drv_vfd_init[addr=%d]: io_mutex init failed", modbus_addr);
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock    = NULL;
        vfd->serial_port = NULL;
        return SW_ERR_HW;
    }

    ret = mb_ctx_create(vfd);
    if (ret != SW_OK) {
        (void)pthread_mutex_destroy(&vfd->io_mutex);
        vfd_bus_port_unbind(vfd->bus_lock);
        vfd->bus_lock    = NULL;
        vfd->serial_port = NULL;
        return ret;
    }

    if (modbus_connect(vfd->mb) < 0) {
        LOG_WARN("drv_vfd_init[addr=%d]: modbus_connect failed, defer link", modbus_addr);
    } else {
        vfd->mb_connected = true;
    }

    vfd_do_set(vfd, pin_fwd, false);
    if (pin_rev.raw != IO_HANDLE_NULL) {
        vfd_do_set(vfd, pin_rev, false);
    }
    vfd_do_set(vfd, pin_rst, false);

    LOG_INFO("drv_vfd_init[addr=%d] ok", modbus_addr);
    return SW_OK;
}

sw_err_t drv_vfd_config_speed_io(drv_vfd_t    *vfd,
                                 io_do_t       pin_spd1,
                                 io_do_t       pin_spd2,
                                 const uint8_t spd_cfg[VFD_GEAR_MAX])
{
    uint8_t i;

    if (!vfd_is_initialized(vfd) || (spd_cfg == NULL)) {
        return SW_ERR_PARAM;
    }
    if ((pin_spd1.raw == IO_HANDLE_NULL) || (pin_spd2.raw == IO_HANDLE_NULL)) {
        LOG_ERROR("drv_vfd_config_speed_io[addr=%d]: invalid speed IO pins", vfd->modbus_addr);
        return SW_ERR_PARAM;
    }

    for (i = 0U; i < (uint8_t)VFD_GEAR_MAX; i++) {
        if (spd_cfg[i] == 0U) {
            LOG_ERROR("drv_vfd_config_speed_io[addr=%d]: gear %u uses VFD_SPD_IO(0,0), conflicts with stop",
                      vfd->modbus_addr,
                      (unsigned)(i + 1U));
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

sw_err_t drv_vfd_stop_outputs(drv_vfd_t *vfd)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    (void)pthread_mutex_lock(&vfd->io_mutex);
    vfd_spd_io_clear(vfd);
    vfd_do_set(vfd, vfd->pin_fwd, false);
    if (vfd->pin_rev.raw != IO_HANDLE_NULL) {
        vfd_do_set(vfd, vfd->pin_rev, false);
    }
    vfd->gear = VFD_GEAR_STOP;
    (void)pthread_mutex_unlock(&vfd->io_mutex);
    return SW_OK;
}

sw_err_t drv_vfd_apply_gear(drv_vfd_t *vfd, drv_vfd_gear_t gear)
{
    int abs_gear_int;

    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    if (gear == VFD_GEAR_STOP) {
        return drv_vfd_stop_outputs(vfd);
    }

    abs_gear_int = (gear < 0) ? -(int)gear : (int)gear;

    if (abs_gear_int > VFD_GEAR_MAX) {
        return SW_ERR_PARAM;
    }
    if ((gear < 0) && (vfd->pin_rev.raw == IO_HANDLE_NULL)) {
        LOG_ERROR("drv_vfd_apply_gear[addr=%d]: reverse not supported", vfd->modbus_addr);
        return SW_ERR_PARAM;
    }
    (void)pthread_mutex_lock(&vfd->io_mutex);
    vfd_apply_gear_impl(vfd, gear);
    (void)pthread_mutex_unlock(&vfd->io_mutex);
    return SW_OK;
}

sw_err_t drv_vfd_set_rst(drv_vfd_t *vfd, bool level)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }
    if (vfd->pin_rst.raw == IO_HANDLE_NULL) {
        return SW_ERR_PARAM;
    }
    vfd_do_set(vfd, vfd->pin_rst, level);
    return SW_OK;
}

drv_vfd_state_t drv_vfd_get_state(drv_vfd_t *vfd)
{
    drv_vfd_gear_t gear;

    if (vfd == NULL) {
        return HAL_VFD_STATE_STOPPED;
    }
    (void)pthread_mutex_lock(&vfd->io_mutex);
    gear = vfd->gear;
    (void)pthread_mutex_unlock(&vfd->io_mutex);
    if (gear > 0) {
        return HAL_VFD_STATE_FWD;
    }
    if (gear < 0) {
        return HAL_VFD_STATE_REV;
    }
    return HAL_VFD_STATE_STOPPED;
}

sw_err_t drv_vfd_read(drv_vfd_t *vfd, drv_vfd_reg_t reg, uint16_t *p_val)
{
    sw_err_t ret;
    uint16_t addr;
    uint16_t val = 0U;

    if ((vfd == NULL) || (p_val == NULL)) {
        return SW_ERR_PARAM;
    }
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    switch (reg) {
        case DRV_VFD_REG_STATE:      addr = VFD_REG_STATE;      break;
        case DRV_VFD_REG_FAULT_CODE: addr = VFD_REG_FAULT_CODE; break;
        case DRV_VFD_REG_CURRENT:    addr = VFD_REG_CURRENT;    break;
        default:
            return SW_ERR_PARAM;
    }

    ret = mb_read_reg(vfd, addr, &val);
    if (ret == SW_OK) {
        *p_val = val;
    }
    return ret;
}

sw_err_t drv_vfd_write(drv_vfd_t *vfd, drv_vfd_reg_t reg, uint16_t val)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    (void)val;

    switch (reg) {
#ifdef VFD_REG_FREQ_SET
        case DRV_VFD_REG_FREQ:
            return mb_write_reg(vfd, VFD_REG_FREQ_SET, val);
#endif
#ifdef VFD_REG_CLEAR_FAULT
        case DRV_VFD_REG_CLEAR_FAULT:
            return mb_write_reg(vfd, VFD_REG_CLEAR_FAULT, VFD_DATA_CLEAR_FAULT);
#endif
        default:
            return SW_ERR_PARAM;
    }
}
