#include "domain/model/conditional_control.h"

#include <string.h>

void conditional_control_init(conditional_control_t *conditional_control)
{
    if (conditional_control == 0)
    {
        return;
    }

    memset(conditional_control, 0, sizeof(*conditional_control));
}

bool conditional_control_is_valid(const conditional_control_t *conditional_control)
{
    if (conditional_control == 0)
    {
        return false;
    }
    if (conditional_control->kind != CONDITIONAL_CONTROL_CHEMICAL_WINDOW)
    {
        return false;
    }
    if (conditional_control->control_object != ACTUATOR_CHEMICAL)
    {
        return false;
    }
    if (conditional_control->basis == POSITION_REFERENCE_NONE)
    {
        return false;
    }
    if (!position_trigger_is_valid(&conditional_control->start_trigger) ||
        !position_trigger_is_valid(&conditional_control->stop_trigger))
    {
        return false;
    }
    return conditional_control->start_trigger.reference == conditional_control->basis &&
           conditional_control->stop_trigger.reference == conditional_control->basis;
}
