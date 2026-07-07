/**
 * @file    drv_vfd.c
 * @brief   变频器驱动实现（Modbus RTU + IO 数字量原语）
 * @author  HUWANGWEI
 * @date    2026-04-08
 */

#include "drv_vfd.h"

#include "drv_modbus_link.h"
#include "framework/common/log.h"

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

/* -------------------------------------------------------------------------
 * 基础辅助
 * ------------------------------------------------------------------------- */
static bool vfd_is_initialized(const drv_vfd_t *vfd)
{
    return (vfd != NULL) && drv_modbus_link_is_ready(&vfd->link) && (vfd->do_set != NULL);
}

static bool vfd_has_rev(const drv_vfd_t *vfd)
{
    return (vfd != NULL) && (vfd->pin_rev.raw != IO_HANDLE_NULL);
}

static int vfd_abs_gear(hal_vfd_gear_t gear)
{
    return (gear < 0) ? -(int)gear : (int)gear;
}

static void vfd_do_set(drv_vfd_t *vfd, io_do_t pin, bool val)
{
    if ((vfd != NULL) && (vfd->do_set != NULL)) {
        (void)vfd->do_set(pin, val);
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

/** @brief  关断运行相关 DO（spd/fwd/rev，不含 rst） */
static void vfd_run_outputs_off(drv_vfd_t *vfd)
{
    vfd_spd_io_clear(vfd);
    vfd_do_set(vfd, vfd->pin_fwd, false);
    if (vfd_has_rev(vfd)) {
        vfd_do_set(vfd, vfd->pin_rev, false);
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

static void vfd_apply_gear_impl(drv_vfd_t *vfd, hal_vfd_gear_t gear)
{
    int abs_gear = vfd_abs_gear(gear);

    if (vfd->spd_io_ready) {
        vfd_spd_io_apply(vfd, (uint8_t)abs_gear);
    }
    vfd_do_set(vfd, vfd->pin_fwd, gear > 0);
    if (vfd_has_rev(vfd)) {
        vfd_do_set(vfd, vfd->pin_rev, gear < 0);
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

    if (pthread_mutex_init(&vfd->io_mutex, NULL) != 0) {
        LOG_ERROR("drv_vfd_init[addr=%d]: io_mutex init failed", modbus_addr);
        return SW_ERR_HW;
    }

    ret = drv_modbus_link_init(
        &vfd->link, serial_port, baud, modbus_addr, VFD_MODBUS_TIMEOUT_US, VFD_COMM_FAIL_RECONNECT);
    if (ret != SW_OK) {
        (void)pthread_mutex_destroy(&vfd->io_mutex);
        return ret;
    }

    vfd->pin_fwd = pin_fwd;
    vfd->pin_rev = pin_rev;
    vfd->pin_rst = pin_rst;
    vfd->gear    = 0;
    vfd->do_set  = do_set;

    vfd_run_outputs_off(vfd);
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
        LOG_ERROR("drv_vfd_config_speed_io[addr=%d]: invalid speed IO pins", vfd->link.modbus_addr);
        return SW_ERR_PARAM;
    }

    for (i = 0U; i < (uint8_t)VFD_GEAR_MAX; i++) {
        if (spd_cfg[i] == 0U) {
            LOG_ERROR("drv_vfd_config_speed_io[addr=%d]: gear %u uses VFD_SPD_IO(0,0), conflicts with stop",
                      vfd->link.modbus_addr,
                      (unsigned)(i + 1U));
            return SW_ERR_PARAM;
        }
        vfd->spd_cfg[i] = spd_cfg[i];
    }

    vfd->pin_spd1 = pin_spd1;
    vfd->pin_spd2 = pin_spd2;

    vfd_do_set(vfd, pin_spd1, false);
    vfd_do_set(vfd, pin_spd2, false);

    vfd->spd_io_ready = true;
    LOG_INFO("drv_vfd_config_speed_io[addr=%d] ok", vfd->link.modbus_addr);
    return SW_OK;
}

sw_err_t drv_vfd_stop_outputs(drv_vfd_t *vfd)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    (void)pthread_mutex_lock(&vfd->io_mutex);
    vfd_run_outputs_off(vfd);
    vfd->gear = 0;
    (void)pthread_mutex_unlock(&vfd->io_mutex);
    return SW_OK;
}

sw_err_t drv_vfd_apply_gear(drv_vfd_t *vfd, hal_vfd_gear_t gear)
{
    int abs_gear_int;

    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    if (gear == 0) {
        return drv_vfd_stop_outputs(vfd);
    }

    abs_gear_int = vfd_abs_gear(gear);

    if (abs_gear_int > (int)VFD_GEAR_MAX) {
        return SW_ERR_PARAM;
    }
    if ((gear < 0) && !vfd_has_rev(vfd)) {
        LOG_ERROR("drv_vfd_apply_gear[addr=%d]: reverse not supported", vfd->link.modbus_addr);
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

hal_vfd_state_t drv_vfd_get_state(drv_vfd_t *vfd)
{
    hal_vfd_gear_t gear;

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

sw_err_t drv_vfd_read(drv_vfd_t *vfd, hal_vfd_reg_t reg, uint16_t *p_val)
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
    case HAL_VFD_REG_STATE:
        addr = VFD_REG_STATE;
        break;
    case HAL_VFD_REG_FAULT_CODE:
        addr = VFD_REG_FAULT_CODE;
        break;
    case HAL_VFD_REG_CURRENT:
        addr = VFD_REG_CURRENT;
        break;
    default:
        return SW_ERR_PARAM;
    }

    ret = drv_modbus_link_read_reg(&vfd->link, addr, &val);
    if (ret == SW_OK) {
        *p_val = val;
    }
    return ret;
}

sw_err_t drv_vfd_write(drv_vfd_t *vfd, hal_vfd_reg_t reg, uint16_t val)
{
    if (!vfd_is_initialized(vfd)) {
        return SW_ERR_NOT_INIT;
    }

    switch (reg) {
#ifdef VFD_REG_FREQ_SET
    case HAL_VFD_REG_FREQ:
        return drv_modbus_link_write_reg(&vfd->link, VFD_REG_FREQ_SET, val);
#endif
#ifdef VFD_REG_CLEAR_FAULT
    case HAL_VFD_REG_CLEAR_FAULT:
        (void)val;
        return drv_modbus_link_write_reg(&vfd->link, VFD_REG_CLEAR_FAULT, VFD_DATA_CLEAR_FAULT);
#endif
    default:
        (void)val;
        return SW_ERR_PARAM;
    }
}
