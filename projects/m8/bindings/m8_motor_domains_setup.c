/**
 * @file    m8_motor_domains_setup.c
 * @brief   M8 机型电机相关领域模块装配实现（表驱动）。
 */

#include "projects/m8/bindings/m8_motor_domains_setup.h"

#include "projects/m8/bindings/m8_motor_exec.h"
#include "projects/m8/config/m8_motor_binding.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/domain/device_control/mechanism/fan.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/lift.h"
#include "framework/domain/device_control/mechanism/rear_lock.h"
#include "framework/common/log.h"

/** @brief 单条领域装配项。 */
typedef struct {
    const char *name;                             /**< 日志用模块名 */
    uint32_t    domain_bit;                       /**< 对应 m8_motor_domain_mask_t 位 */
    sw_err_t  (*init)(hal_motor_exec_t *exec);     /**< 绑定函数 */
} m8_motor_domain_entry_t;

static sw_err_t domain_init_brush(hal_motor_exec_t *exec)
{
    return brush_init(exec, M8_BIND_BRUSH_SIDE, M8_BIND_BRUSH_TOP);
}

static sw_err_t domain_init_gantry(hal_motor_exec_t *exec)
{
    return gantry_init(exec, M8_BIND_GANTRY);
}

static sw_err_t domain_init_lift(hal_motor_exec_t *exec)
{
    return lift_init(exec, M8_BIND_LIFT);
}

static sw_err_t domain_init_rear_lock(hal_motor_exec_t *exec)
{
    return rear_lock_init(exec, M8_BIND_REAR_LOCK);
}

static sw_err_t domain_init_fan(hal_motor_exec_t *exec)
{
    return fan_init(exec, M8_BIND_FAN);
}

static const m8_motor_domain_entry_t s_domain_table[] = {
    { "brush",     M8_DOMAIN_BRUSH,     domain_init_brush     },
    { "gantry",    M8_DOMAIN_GANTRY,    domain_init_gantry    },
    { "lift",      M8_DOMAIN_LIFT,      domain_init_lift      },
    { "rear_lock", M8_DOMAIN_REAR_LOCK, domain_init_rear_lock },
    { "fan",       M8_DOMAIN_FAN,       domain_init_fan       },
};

sw_err_t m8_motor_domains_setup_mask(uint32_t mask)
{
    motor_executor_t *real_exec = m8_motor_exec_get();
    hal_motor_exec_t *exec      = (hal_motor_exec_t *)real_exec;
    sw_err_t          ret;
    size_t            i;

    if (real_exec == NULL) {
        LOG_ERROR("m8_motor_domains_setup: motor exec not initialized");
        return SW_ERR_NOT_INIT;
    }

    if (mask == 0U) {
        mask = M8_DOMAIN_ALL;
    }

    for (i = 0U; i < (sizeof(s_domain_table) / sizeof(s_domain_table[0])); i++) {
        const m8_motor_domain_entry_t *entry = &s_domain_table[i];

        if ((mask & entry->domain_bit) == 0U) {
            continue;
        }

        ret = entry->init(exec);
        if (ret != SW_OK) {
            LOG_ERROR("m8_motor_domains_setup: %s failed ret=%d", entry->name, (int)ret);
            return ret;
        }
    }

    LOG_INFO("m8_motor_domains_setup ok (mask=0x%x)", mask);
    return SW_OK;
}

sw_err_t m8_motor_domains_setup(void)
{
    return m8_motor_domains_setup_mask(M8_DOMAIN_ALL);
}
