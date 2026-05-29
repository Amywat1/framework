#include "domain/model/vehicle_type.h"

#include <string.h>

void vehicle_type_init(vehicle_type_t *vehicle_type, const char *vehicle_type_id, const char *vehicle_type_name)
{
    if (vehicle_type == 0)
    {
        return;
    }

    memset(vehicle_type, 0, sizeof(*vehicle_type));
    if (vehicle_type_id != 0)
    {
        strncpy(vehicle_type->vehicle_type_id, vehicle_type_id, sizeof(vehicle_type->vehicle_type_id) - 1);
    }
    if (vehicle_type_name != 0)
    {
        strncpy(vehicle_type->vehicle_type_name, vehicle_type_name, sizeof(vehicle_type->vehicle_type_name) - 1);
    }
    vehicle_type->max_length_mm = 5500;
    vehicle_type->max_width_mm = 2200;
    vehicle_type->max_height_mm = 2200;
    vehicle_type->min_length_mm = 3500;
}
