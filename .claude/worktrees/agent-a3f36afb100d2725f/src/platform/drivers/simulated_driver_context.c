#include "platform/drivers/simulated_driver_context.h"

#include <string.h>

/**
 * @brief 在初始化阶段写入默认反馈快照。
 * @param driver_context 共享上下文，不能为空。
 */
static void initialize_feedback_snapshot(simulated_driver_context_t *driver_context)
{
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_follow_ok = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_stopped_feedback_available = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_stopped = true;
    driver_context->runtime_snapshot.actuator_feedback.side_brush_stopped_feedback_available = true;
    driver_context->runtime_snapshot.actuator_feedback.side_brush_stopped = true;
    driver_context->runtime_snapshot.actuator_feedback.chemical_closed_feedback_available = true;
    driver_context->runtime_snapshot.actuator_feedback.chemical_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.ro_water_closed_feedback_available = true;
    driver_context->runtime_snapshot.actuator_feedback.ro_water_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.dryer_closed_feedback_available = true;
    driver_context->runtime_snapshot.actuator_feedback.dryer_closed = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_home_feedback_available = true;
    driver_context->runtime_snapshot.actuator_feedback.roof_brush_home_reached = true;
}

void simulated_driver_context_init(simulated_driver_context_t *driver_context)
{
    if (driver_context == 0)
    {
        return;
    }

    memset(driver_context, 0, sizeof(*driver_context));
    driver_context->runtime_snapshot.position_snapshot.position_valid = true;
    driver_context->roof_follow_feedback_available = true;
    driver_context->roof_stop_feedback_available = true;
    driver_context->side_stop_feedback_available = true;
    driver_context->chemical_close_feedback_available = true;
    driver_context->ro_water_close_feedback_available = true;
    driver_context->dryer_close_feedback_available = true;
    driver_context->roof_home_feedback_available = true;
    driver_context->roof_home_reached = true;
    initialize_feedback_snapshot(driver_context);
    (void)pthread_mutex_init(&driver_context->mutex, 0);
}

void simulated_driver_context_lock(simulated_driver_context_t *driver_context)
{
    if (driver_context == 0)
    {
        return;
    }

    (void)pthread_mutex_lock(&driver_context->mutex);
}

void simulated_driver_context_unlock(simulated_driver_context_t *driver_context)
{
    if (driver_context == 0)
    {
        return;
    }

    (void)pthread_mutex_unlock(&driver_context->mutex);
}
