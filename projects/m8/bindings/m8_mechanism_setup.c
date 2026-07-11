/**
 * @file    m8_mechanism_setup.c
 * @brief   M8 命名机构装配（组合 framework patterns）
 * @author  HUWANGWEI
 * @date    2026-07-11
 */

#include "projects/m8/bindings/m8_mechanism_setup.h"
#include "projects/m8/bindings/m8_mechanism_fault.h"
#include "projects/m8/bindings/m8_motor_exec.h"
#include "projects/m8/config/m8_actuator_ids.h"
#include "projects/m8/config/m8_brush_ids.h"
#include "projects/m8/config/m8_motor_binding.h"
#include "projects/m8/domain/mechanism/gantry.h"
#include "projects/m8/domain/mechanism/m8_top_brush_lift.h"
#include "projects/m8/domain/mechanism/m8_brush_rotation.h"
#include "projects/m8/domain/mechanism/m8_fan.h"
#include "projects/m8/domain/mechanism/m8_rear_lock.h"
#include "framework/common/log.h"
#include "framework/domain/device_control/patterns/motion_lifecycle.h"

typedef struct
{
    const char *name;
    uint32_t    domain_bit;
    sw_err_t  (*init)(hal_motor_exec_t *exec);
} m8_mechanism_entry_t;

static sw_err_t domain_init_brush(hal_motor_exec_t *exec)
{
    static const int                    s_motor[M8_BRUSH_COUNT] = { M8_BIND_BRUSH_SIDE, M8_BIND_BRUSH_TOP };
    static const brush_interlock_pair_t s_pairs[]               = { { M8_BRUSH_SIDE, M8_BRUSH_TOP } };
    static const motion_lifecycle_opts_t s_opts                 = {
        .motion_actuator_id = M8_ACTUATOR_SIDE_BRUSH,
    };

    return brush_init(exec, s_motor, M8_BRUSH_COUNT, s_pairs, 1, &s_opts);
}

static sw_err_t domain_init_gantry(hal_motor_exec_t *exec)
{
    static const gantry_options_t s_opts = {
        .motion_actuator_id = M8_ACTUATOR_GANTRY,
        .on_process_fault   = m8_gantry_on_process_fault,
    };

    return gantry_init(exec, M8_BIND_GANTRY, &s_opts);
}

static sw_err_t domain_init_lift(hal_motor_exec_t *exec)
{
    static const motion_lifecycle_opts_t s_opts = {
        .motion_actuator_id = M8_ACTUATOR_TOP_BRUSH,
        .on_process_fault   = m8_top_brush_lift_on_process_fault,
    };

    return lift_init(exec, M8_BIND_LIFT, &s_opts);
}

static sw_err_t domain_init_rear_lock(hal_motor_exec_t *exec)
{
    static const motion_lifecycle_opts_t s_opts = {
        .motion_actuator_id = M8_ACTUATOR_REAR_LOCK,
        .on_process_fault   = m8_rear_lock_on_process_fault,
    };

    return rear_lock_init(exec, M8_BIND_REAR_LOCK, &s_opts);
}

static sw_err_t domain_init_fan(hal_motor_exec_t *exec)
{
    static const motion_lifecycle_opts_t s_opts = {
        .motion_actuator_id = M8_ACTUATOR_FAN,
    };

    return fan_init(exec, M8_BIND_FAN, &s_opts);
}

static const m8_mechanism_entry_t s_table[] = {
    { "brush",     M8_DOMAIN_BRUSH,     domain_init_brush     },
    { "gantry",    M8_DOMAIN_GANTRY,    domain_init_gantry    },
    { "lift",      M8_DOMAIN_LIFT,      domain_init_lift      },
    { "rear_lock", M8_DOMAIN_REAR_LOCK, domain_init_rear_lock },
    { "fan",       M8_DOMAIN_FAN,       domain_init_fan       },
};

sw_err_t m8_mechanism_setup_mask(uint32_t mask)
{
    motor_executor_t *real_exec = m8_motor_exec_get();
    hal_motor_exec_t *exec      = (hal_motor_exec_t *)real_exec;
    sw_err_t          ret;
    size_t            i;

    if (real_exec == NULL)
    {
        LOG_ERROR("m8_mechanism_setup: motor exec not initialized");
        return SW_ERR_NOT_INIT;
    }

    if (mask == 0U)
    {
        mask = M8_DOMAIN_ALL;
    }

    for (i = 0U; i < (sizeof(s_table) / sizeof(s_table[0])); i++)
    {
        const m8_mechanism_entry_t *entry = &s_table[i];

        if ((mask & entry->domain_bit) == 0U)
        {
            continue;
        }

        ret = entry->init(exec);
        if (ret != SW_OK)
        {
            LOG_ERROR("m8_mechanism_setup: %s failed ret=%d", entry->name, (int)ret);
            return ret;
        }
    }

    LOG_INFO("m8_mechanism_setup ok (mask=0x%x)", mask);
    return SW_OK;
}

sw_err_t m8_mechanism_setup(void)
{
    return m8_mechanism_setup_mask(M8_DOMAIN_ALL);
}
