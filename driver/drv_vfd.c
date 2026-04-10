/**
 * @file    drv_vfd.c
 * @brief   士林变频器驱动实现（Modbus RTU + IO 数字量控制）
 * @author  HUWANGWEI
 * @date    2026-04-08
 *
 * @note    士林 E310/E510 系列 Modbus 寄存器布局：
 *          0x2000 — 控制字（bit0=正转，bit1=反转，bit2=复位）
 *          0x2001 — 频率设定（0.01Hz）
 *          0x2100 — 状态字
 *          0x2102 — 故障代码
 */

#include "drv_vfd.h"
#include "common/log.h"
#include "modbus/modbus-rtu.h"
#include <unistd.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 士林 VFD Modbus 寄存器地址
 * ------------------------------------------------------------------------- */
#define VFD_REG_FREQ_SET    0x2001U     /* 频率设定值（0.01Hz）*/
#define VFD_REG_FAULT_CODE  0x2102U     /* 故障代码 */

#define VFD_FAULT_RESET_PULSE_MS    200U    /* 复位脉冲宽度 */
#define VFD_MODBUS_TIMEOUT_US       500000U /* 500ms */

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */
static sw_err_t mb_write_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t val)
{
    if (modbus_write_register(vfd->mb, (int)addr, (int)val) < 0) {
        LOG_ERROR("drv_vfd[addr=%d]: Modbus write reg 0x%04X failed",
                  modbus_get_slave(vfd->mb), addr);
        return SW_ERR_COMM;
    }
    return SW_OK;
}

static sw_err_t mb_read_reg(drv_vfd_t *vfd, uint16_t addr, uint16_t *p_val)
{
    uint16_t buf = 0U;
    if (modbus_read_registers(vfd->mb, (int)addr, 1, &buf) < 0) {
        return SW_ERR_COMM;
    }
    *p_val = buf;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_vfd_init(drv_vfd_t   *vfd,
                      const char  *serial_port,
                      int          baud,
                      int          modbus_addr,
                      drv_io_do_t  pin_fwd,
                      bool         has_rev,
                      drv_io_do_t  pin_rev,
                      drv_io_do_t  pin_rst)
{
    if (vfd == NULL || serial_port == NULL) {
        return SW_ERR_PARAM;
    }

    vfd->pin_fwd   = pin_fwd;
    vfd->pin_rev   = pin_rev;
    vfd->has_rev   = has_rev;
    vfd->pin_rst   = pin_rst;
    vfd->state     = DRV_VFD_STATE_STOPPED;
    vfd->event_cb  = NULL;

    vfd->mb = modbus_new_rtu(serial_port, baud, 'N', 8, 1);
    if (vfd->mb == NULL) {
        LOG_ERROR("drv_vfd_init[addr=%d]: modbus_new_rtu failed", modbus_addr);
        return SW_ERR_HW;
    }

    modbus_set_slave(vfd->mb, modbus_addr);
    modbus_set_response_timeout(vfd->mb, 0, VFD_MODBUS_TIMEOUT_US);

    if (modbus_connect(vfd->mb) < 0) {
        LOG_ERROR("drv_vfd_init[addr=%d]: modbus_connect failed", modbus_addr);
        modbus_free(vfd->mb);
        vfd->mb = NULL;
        return SW_ERR_HW;
    }

    /* 上电安全状态：所有控制输出关断 */
    (void)drv_io_do_set(pin_fwd, false);
    if (has_rev) {
        (void)drv_io_do_set(pin_rev, false);
    }
    (void)drv_io_do_set(pin_rst, false);

    LOG_INFO("drv_vfd_init[addr=%d] ok", modbus_addr);
    return SW_OK;
}

sw_err_t drv_vfd_run_fwd(drv_vfd_t *vfd, uint16_t freq_hz)
{
    sw_err_t ret;

    if (vfd == NULL || vfd->mb == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (freq_hz == 0U) {
        return drv_vfd_stop(vfd);
    }

    ret = mb_write_reg(vfd, VFD_REG_FREQ_SET, freq_hz);
    if (ret != SW_OK) {
        return ret;
    }

    if (vfd->has_rev) {
        (void)drv_io_do_set(vfd->pin_rev, false);
    }
    (void)drv_io_do_set(vfd->pin_fwd, true);

    vfd->state = DRV_VFD_STATE_FWD;
    return SW_OK;
}

sw_err_t drv_vfd_run_rev(drv_vfd_t *vfd, uint16_t freq_hz)
{
    sw_err_t ret;

    if (vfd == NULL || vfd->mb == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (!vfd->has_rev) {
        LOG_ERROR("drv_vfd_run_rev: this VFD instance does not support reverse");
        return SW_ERR_PARAM;
    }
    if (freq_hz == 0U) {
        return drv_vfd_stop(vfd);
    }

    ret = mb_write_reg(vfd, VFD_REG_FREQ_SET, freq_hz);
    if (ret != SW_OK) {
        return ret;
    }

    (void)drv_io_do_set(vfd->pin_fwd, false);
    (void)drv_io_do_set(vfd->pin_rev, true);

    vfd->state = DRV_VFD_STATE_REV;
    return SW_OK;
}

sw_err_t drv_vfd_stop(drv_vfd_t *vfd)
{
    if (vfd == NULL) {
        return SW_ERR_PARAM;
    }

    (void)drv_io_do_set(vfd->pin_fwd, false);
    if (vfd->has_rev) {
        (void)drv_io_do_set(vfd->pin_rev, false);
    }

    vfd->state = DRV_VFD_STATE_STOPPED;
    return SW_OK;
}

sw_err_t drv_vfd_fault_reset(drv_vfd_t *vfd)
{
    if (vfd == NULL) {
        return SW_ERR_PARAM;
    }

    (void)drv_io_do_set(vfd->pin_rst, true);
    usleep(VFD_FAULT_RESET_PULSE_MS * 1000U);
    (void)drv_io_do_set(vfd->pin_rst, false);

    vfd->state = DRV_VFD_STATE_STOPPED;
    return SW_OK;
}

drv_vfd_state_t drv_vfd_get_state(const drv_vfd_t *vfd)
{
    if (vfd == NULL) {
        return DRV_VFD_STATE_STOPPED;
    }
    return vfd->state;
}

uint16_t drv_vfd_get_fault_code(drv_vfd_t *vfd)
{
    uint16_t code = 0U;
    if (vfd != NULL && vfd->mb != NULL) {
        (void)mb_read_reg(vfd, VFD_REG_FAULT_CODE, &code);
    }
    return code;
}

void drv_vfd_register_event_cb(drv_vfd_t *vfd, void (*cb)(int event_code))
{
    if (vfd != NULL) {
        vfd->event_cb = cb;
    }
}
