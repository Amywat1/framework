/**
 * @file    m8_cloud_runtime.c
 * @brief   M8 云端物模型运行时状态与工具实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "projects/m8/adapters/cloud/m8_cloud_runtime.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/ports/outbound/storage/deploy_store.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/domain/device_control/model/device_state.h"
#include "projects/m8/config/m8_io_pins.h"
#include <stdio.h>
#include <string.h>

#define M8_CLOUD_CFG_MAX        16U
#define M8_CLOUD_HOLD_MAX       16U
#define M8_CLOUD_TRI_MAX        8U
#define M8_CLOUD_IO_STR_MAX     POINT_STR_MAX
#define M8_CLOUD_DEPLOY_PORT    "portNumber"

typedef struct
{
    const char *id;
    bool        value;
} m8_cloud_bool_slot_t;

typedef struct
{
    const char *id;
    int32_t     value;
} m8_cloud_tri_slot_t;

typedef struct
{
    bool     custom_stopping;
    int32_t  wash_today;
    int32_t  wash_start;
    int32_t  wash_complete;
    int32_t  wash_failed;
    int32_t  port_number;
    char     i1_io[M8_CLOUD_IO_STR_MAX];
    char     o1_io[M8_CLOUD_IO_STR_MAX];
    char     i1_io_change[M8_CLOUD_IO_STR_MAX];
    char     o1_io_change[M8_CLOUD_IO_STR_MAX];
    uint32_t last_input_snapshot;
    uint32_t last_output_snapshot;
} m8_cloud_runtime_state_t;

static m8_cloud_bool_slot_t s_cfg[M8_CLOUD_CFG_MAX];
static size_t               s_cfg_count;
static m8_cloud_bool_slot_t s_hold[M8_CLOUD_HOLD_MAX];
static size_t               s_hold_count;
static m8_cloud_tri_slot_t  s_tri[M8_CLOUD_TRI_MAX];
static size_t               s_tri_count;
static m8_cloud_runtime_state_t s_state;

static bool read_di_level(io_di_t pin)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->di_read == NULL))
    {
        return false;
    }

    return io->di_read(pin);
}

bool m8_cloud_di_active(io_di_t pin, bool active_low)
{
    bool level = read_di_level(pin);

    return active_low ? !level : level;
}

bool m8_cloud_di_inverted(io_di_t pin, bool active_low)
{
    return !m8_cloud_di_active(pin, active_low);
}

static m8_cloud_bool_slot_t *find_cfg(const char *id)
{
    for (size_t i = 0U; i < s_cfg_count; i++)
    {
        if ((s_cfg[i].id != NULL) && (strcmp(s_cfg[i].id, id) == 0))
        {
            return &s_cfg[i];
        }
    }
    return NULL;
}

static m8_cloud_bool_slot_t *find_hold(const char *id)
{
    for (size_t i = 0U; i < s_hold_count; i++)
    {
        if ((s_hold[i].id != NULL) && (strcmp(s_hold[i].id, id) == 0))
        {
            return &s_hold[i];
        }
    }
    return NULL;
}

static m8_cloud_tri_slot_t *find_tri(const char *id)
{
    for (size_t i = 0U; i < s_tri_count; i++)
    {
        if ((s_tri[i].id != NULL) && (strcmp(s_tri[i].id, id) == 0))
        {
            return &s_tri[i];
        }
    }
    return NULL;
}

static m8_cloud_bool_slot_t *alloc_cfg(const char *id)
{
    m8_cloud_bool_slot_t *slot = find_cfg(id);

    if (slot != NULL)
    {
        return slot;
    }

    if (s_cfg_count >= M8_CLOUD_CFG_MAX)
    {
        return NULL;
    }

    s_cfg[s_cfg_count].id    = id;
    s_cfg[s_cfg_count].value = false;
    s_cfg_count++;
    return &s_cfg[s_cfg_count - 1U];
}

static m8_cloud_bool_slot_t *alloc_hold(const char *id)
{
    m8_cloud_bool_slot_t *slot = find_hold(id);

    if (slot != NULL)
    {
        return slot;
    }

    if (s_hold_count >= M8_CLOUD_HOLD_MAX)
    {
        return NULL;
    }

    s_hold[s_hold_count].id    = id;
    s_hold[s_hold_count].value = false;
    s_hold_count++;
    return &s_hold[s_hold_count - 1U];
}

static m8_cloud_tri_slot_t *alloc_tri(const char *id)
{
    m8_cloud_tri_slot_t *slot = find_tri(id);

    if (slot != NULL)
    {
        return slot;
    }

    if (s_tri_count >= M8_CLOUD_TRI_MAX)
    {
        return NULL;
    }

    s_tri[s_tri_count].id    = id;
    s_tri[s_tri_count].value = M8_CLOUD_TRI_STOP;
    s_tri_count++;
    return &s_tri[s_tri_count - 1U];
}

void m8_cloud_runtime_set_custom_stopping(bool active)
{
    s_state.custom_stopping = active;
}

void m8_cloud_runtime_on_wash_started(void)
{
    s_state.wash_start++;
}

void m8_cloud_runtime_on_wash_completed(void)
{
    s_state.wash_complete++;
    s_state.wash_today++;
    s_state.custom_stopping = false;
}

void m8_cloud_runtime_on_wash_failed(void)
{
    s_state.wash_failed++;
    s_state.custom_stopping = false;
}

bool m8_cloud_runtime_get_bool(const char *id)
{
    m8_cloud_bool_slot_t *slot = find_cfg(id);

    return (slot != NULL) ? slot->value : false;
}

void m8_cloud_runtime_set_bool(const char *id, bool value)
{
    m8_cloud_bool_slot_t *slot = alloc_cfg(id);

    if (slot != NULL)
    {
        slot->value = value;
    }
}

bool m8_cloud_runtime_get_hold(const char *id)
{
    m8_cloud_bool_slot_t *slot = find_hold(id);

    return (slot != NULL) ? slot->value : false;
}

void m8_cloud_runtime_set_hold(const char *id, bool active)
{
    m8_cloud_bool_slot_t *slot = alloc_hold(id);

    if (slot != NULL)
    {
        slot->value = active;
    }
}

int32_t m8_cloud_runtime_get_tri(const char *id)
{
    m8_cloud_tri_slot_t *slot = find_tri(id);

    return (slot != NULL) ? slot->value : M8_CLOUD_TRI_STOP;
}

void m8_cloud_runtime_set_tri(const char *id, int32_t value)
{
    m8_cloud_tri_slot_t *slot = alloc_tri(id);

    if (slot != NULL)
    {
        slot->value = value;
    }
}

static void format_input_snapshot(char *buf, size_t buf_size)
{
    static const struct
    {
        io_di_t pin;
        bool    active_low;
    } s_di_map[] = {
        { M8_IO_DI_FRONT_WHEEL_LIMIT,    false },
        { M8_IO_DI_REAR_WHEEL_LOCK,      false },
        { M8_IO_DI_TOP_BRUSH_COLLISION,  false },
        { M8_IO_DI_BUMPER_ROD_LEFT,      false },
        { M8_IO_DI_BUMPER_ROD_RIGHT,     false },
        { M8_IO_DI_ESTOP,                false },
        { M8_IO_DI_RELEASE_LOCK,         false },
        { M8_IO_DI_REAR_LOCK_HOME,       false },
        { M8_IO_DI_BUMPER_LEFT,          false },
        { M8_IO_DI_BUMPER_RIGHT,         false },
        { M8_IO_DI_GANTRY_ENCODER_PULSE, false },
        { M8_IO_DI_GANTRY_REV_LIMIT,     false },
        { M8_IO_DI_GANTRY_FWD_LIMIT,     false },
        { M8_IO_DI_LIFT_UP_LIMIT,        false },
        { M8_IO_DI_LIFT_DOWN_LIMIT,      false },
        { M8_IO_DI_SWITCH_SIDE_BRUSH,    false },
        { M8_IO_DI_SWITCH_TOP_BRUSH,     false },
        { M8_IO_DI_SIDE_BRUSH_OVERLOAD,  false },
        { M8_IO_DI_TOP_BRUSH_OVERLOAD,   false },
        { M8_IO_DI_WATER_PUMP_OVERLOAD,  false },
        { M8_IO_DI_GANTRY_ALARM,         false },
        { M8_IO_DI_FAN_ALARM,            false },
        { M8_IO_DI_BRUSH_ALARM,          false },
    };
    uint32_t bits = 0U;
    size_t   i;

    for (i = 0U; i < (sizeof(s_di_map) / sizeof(s_di_map[0])); i++)
    {
        if (m8_cloud_di_active(s_di_map[i].pin, s_di_map[i].active_low))
        {
            bits |= (1U << i);
        }
    }

    (void)snprintf(buf, buf_size,
                   "I_01-24:%08x;25-30:%06x",
                   (unsigned)(bits & 0xFFFFFFU),
                   (unsigned)((bits >> 24) & 0x3FU));
}

static void format_output_snapshot(char *buf, size_t buf_size)
{
    (void)snprintf(buf, buf_size, "O_01-30:00000000;31-30:000000");
}

void m8_cloud_runtime_refresh_io_snapshot(void)
{
    dev_state_t st = dev_ctx_get_device_state();
    uint32_t    in_snap;
    uint32_t    out_snap = 0U;

    if ((st != DEV_STATE_STOP) && (st != DEV_STATE_INIT) && (st != DEV_STATE_FAULT))
    {
        return;
    }

    format_input_snapshot(s_state.i1_io, sizeof(s_state.i1_io));
    format_output_snapshot(s_state.o1_io, sizeof(s_state.o1_io));

    in_snap = 0U;
    (void)sscanf(s_state.i1_io, "I_01-24:%x", &in_snap);

    if ((s_state.last_input_snapshot != 0U) && (in_snap != s_state.last_input_snapshot))
    {
        uint32_t diff = in_snap ^ s_state.last_input_snapshot;
        unsigned bit  = 0U;

        while (((diff & 1U) == 0U) && (bit < 24U))
        {
            diff >>= 1U;
            bit++;
        }

        (void)snprintf(s_state.i1_io_change, sizeof(s_state.i1_io_change),
                       "I1_%02u:%u--%u",
                       bit + 1U,
                       (unsigned)((s_state.last_input_snapshot >> bit) & 1U),
                       (unsigned)((in_snap >> bit) & 1U));
    }

    if ((s_state.last_output_snapshot != 0U) && (out_snap != s_state.last_output_snapshot))
    {
        (void)snprintf(s_state.o1_io_change, sizeof(s_state.o1_io_change),
                       "O1_%02u:%u--%u", 1U, 0U, 0U);
    }

    s_state.last_input_snapshot  = in_snap;
    s_state.last_output_snapshot = out_snap;
}

sw_err_t m8_cloud_runtime_get_io_str(const char *id, point_value_t *out)
{
    const char *src = "";

    if ((id == NULL) || (out == NULL))
    {
        return SW_ERR_PARAM;
    }

    m8_cloud_runtime_refresh_io_snapshot();

    if (strcmp(id, "sts_I1_IO") == 0)
    {
        src = s_state.i1_io;
    }
    else if (strcmp(id, "sts_I1_IO_change") == 0)
    {
        src = s_state.i1_io_change;
    }
    else if (strcmp(id, "sts_O1_IO") == 0)
    {
        src = s_state.o1_io;
    }
    else if (strcmp(id, "sts_O1_IO_change") == 0)
    {
        src = s_state.o1_io_change;
    }
    else
    {
        return SW_ERR_PARAM;
    }

    strncpy(out->s, src, sizeof(out->s) - 1U);
    out->s[sizeof(out->s) - 1U] = '\0';
    return SW_OK;
}

int32_t m8_cloud_runtime_get_port_number(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();
    char                      buf[16];
    int32_t                   port = 0;

    if ((ds != NULL) && (ds->get != NULL) &&
        (ds->get(M8_CLOUD_DEPLOY_PORT, buf, sizeof(buf)) == SW_OK))
    {
        (void)sscanf(buf, "%d", &port);
        return port;
    }

    return s_state.port_number;
}

bool m8_cloud_runtime_is_custom_stopping(void)
{
    return s_state.custom_stopping;
}

int32_t m8_cloud_runtime_get_wash_today(void)
{
    return s_state.wash_today;
}

int32_t m8_cloud_runtime_get_wash_start(void)
{
    return s_state.wash_start;
}

int32_t m8_cloud_runtime_get_wash_complete(void)
{
    return s_state.wash_complete;
}

int32_t m8_cloud_runtime_get_wash_failed(void)
{
    return s_state.wash_failed;
}
