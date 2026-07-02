/**
 * @file    brush.c
 * @brief   刷子机构领域层实现。
 *
 * 状态机驱动接触器切换时序，确保变频器完全停止后才操作接触器，
 * 并在接触器稳定后再重启变频器，防止带载切换损坏设备。
 */

#include "domain/device/mechanism/brush.h"
#include <stddef.h>
#include <time.h>

/* -------------------- 静态模块状态 -------------------- */

static motor_executor_t      *s_exec;
static brush_contactor_ops_t  s_contactor_ops;
static brush_contactor_cfg_t  s_contactor_cfg;

static brush_state_t s_state             = BRUSH_STATE_IDLE;
static brush_id_t    s_selected          = BRUSH_SIDE;  /* 当前接触器已吸合的刷子 */
static bool          s_contactor_engaged = false;       /* 接触器是否已吸合 */
static brush_id_t    s_target_id         = BRUSH_SIDE;  /* 下一次要运行的刷子 */
static int           s_target_gear       = 0;
static bool          s_start_pending     = false;       /* 停止/切换后需自动重启 */
static uint64_t      s_contactor_until   = 0;           /* 接触器等待截止时刻（ms） */

/* -------------------- 内部工具 -------------------- */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

/* 断开当前接触器，进入 CONTACTOR_OFF 等待 */
static void begin_contactor_off(void)
{
    (void)s_contactor_ops.set_off(s_contactor_ops.ctx, s_selected);
    s_contactor_engaged = false;
    s_contactor_until   = now_ms() + s_contactor_cfg.release_ms;
    s_state             = BRUSH_STATE_CONTACTOR_OFF;
}

/* 吸合目标接触器，进入 CONTACTOR_ON 等待 */
static void begin_contactor_on(void)
{
    (void)s_contactor_ops.set_on(s_contactor_ops.ctx, s_target_id);
    s_selected          = s_target_id;
    s_contactor_engaged = true;
    s_contactor_until   = now_ms() + s_contactor_cfg.close_ms;
    s_state             = BRUSH_STATE_CONTACTOR_ON;
}

/* -------------------- 公共 API -------------------- */

sw_err_t brush_init(motor_executor_t           *exec,
                    const brush_contactor_ops_t *contactor_ops,
                    const brush_contactor_cfg_t *contactor_cfg)
{
    if ((exec == NULL) || (contactor_ops == NULL) || (contactor_cfg == NULL)) {
        return SW_ERR_PARAM;
    }
    if ((contactor_ops->set_on == NULL) || (contactor_ops->set_off == NULL)) {
        return SW_ERR_PARAM;
    }

    s_exec             = exec;
    s_contactor_ops    = *contactor_ops;
    s_contactor_cfg    = *contactor_cfg;
    s_state            = BRUSH_STATE_IDLE;
    s_contactor_engaged = false;
    s_start_pending    = false;

    return SW_OK;
}

sw_err_t brush_start(brush_id_t id, int speed_gear)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if ((unsigned)id >= (unsigned)BRUSH_ID_MAX) {
        return SW_ERR_PARAM;
    }
    if (s_state == BRUSH_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    s_target_id   = id;
    s_target_gear = speed_gear;

    switch (s_state) {
    case BRUSH_STATE_RUNNING:
        if (s_selected == id) {
            /* 同一刷子：仅调速，无需切换 */
            (void)motor_set_speed(s_exec, 0,
                                  motor_speed_gear(speed_gear),
                                  MOTOR_DIR_FORWARD);
        } else {
            /* 不同刷子：先停，tick 检测到停止后再切换 */
            (void)motor_stop(s_exec, 0);
            s_start_pending = true;
            s_state         = BRUSH_STATE_STOPPING;
        }
        break;

    case BRUSH_STATE_IDLE:
        if (!s_contactor_engaged) {
            /* 首次启动或接触器未吸合：直接吸合目标接触器 */
            begin_contactor_on();
            s_start_pending = true;
        } else if (s_selected != id) {
            /* 需切换接触器：先断开 */
            begin_contactor_off();
            s_start_pending = true;
        } else {
            /* 接触器已在正确位置，直接启动变频器 */
            (void)motor_run_continuous(s_exec, 0,
                                       motor_speed_gear(speed_gear),
                                       MOTOR_DIR_FORWARD);
            s_state = BRUSH_STATE_STARTING;
        }
        break;

    default:
        /* 其余过渡态：记录目标，tick 完成当前动作后自动重启 */
        s_start_pending = true;
        break;
    }

    return SW_OK;
}

sw_err_t brush_stop(void)
{
    if (s_exec == NULL) {
        return SW_ERR_NOT_INIT;
    }
    if (s_state == BRUSH_STATE_FAULT) {
        return SW_ERR_STATE;
    }

    s_start_pending = false;

    if ((s_state == BRUSH_STATE_IDLE) || (s_state == BRUSH_STATE_STOPPING)) {
        return SW_OK;
    }

    (void)motor_stop(s_exec, 0);
    s_state = BRUSH_STATE_STOPPING;
    return SW_OK;
}

void brush_tick(void)
{
    if (s_exec == NULL) {
        return;
    }

    motor_tick(s_exec);

    motor_phase_t ph = motor_phase(s_exec, 0);

    /* 故障/急停：任意态下立即转故障 */
    if ((ph == MOTOR_PHASE_FAULT) || (ph == MOTOR_PHASE_ESTOP)) {
        s_state         = BRUSH_STATE_FAULT;
        s_start_pending = false;
        return;
    }

    switch (s_state) {
    case BRUSH_STATE_IDLE:
    case BRUSH_STATE_RUNNING:
        break;

    case BRUSH_STATE_STOPPING:
        if (ph == MOTOR_PHASE_STOPPED) {
            if (s_start_pending && s_contactor_engaged && (s_selected != s_target_id)) {
                /* 需要切换接触器 */
                begin_contactor_off();
            } else if (s_start_pending) {
                /* 同接触器，直接重启变频器 */
                (void)motor_run_continuous(s_exec, 0,
                                           motor_speed_gear(s_target_gear),
                                           MOTOR_DIR_FORWARD);
                s_start_pending = false;
                s_state         = BRUSH_STATE_STARTING;
            } else {
                s_state = BRUSH_STATE_IDLE;
            }
        }
        break;

    case BRUSH_STATE_CONTACTOR_OFF:
        if (now_ms() >= s_contactor_until) {
            begin_contactor_on();
        }
        break;

    case BRUSH_STATE_CONTACTOR_ON:
        if (now_ms() >= s_contactor_until) {
            if (s_target_id != s_selected) {
                /* 接触器吸合期间目标刷子发生变更，需重新切换 */
                begin_contactor_off();
            } else if (s_start_pending) {
                (void)motor_run_continuous(s_exec, 0,
                                           motor_speed_gear(s_target_gear),
                                           MOTOR_DIR_FORWARD);
                s_start_pending = false;
                s_state         = BRUSH_STATE_STARTING;
            } else {
                s_state = BRUSH_STATE_IDLE;
            }
        }
        break;

    case BRUSH_STATE_STARTING:
        if (ph == MOTOR_PHASE_RUNNING) {
            s_state = BRUSH_STATE_RUNNING;
        }
        break;

    case BRUSH_STATE_FAULT:
        break;

    default:
        break;
    }
}

brush_state_t brush_state(void)
{
    return s_state;
}

brush_id_t brush_selected(void)
{
    return s_selected;
}

bool brush_contactor_engaged(void)
{
    return s_contactor_engaged;
}

motor_fault_code_t brush_fault_code(void)
{
    if (s_exec == NULL) {
        return MOTOR_FAULT_NONE;
    }
    return motor_fault_code(s_exec, 0);
}
