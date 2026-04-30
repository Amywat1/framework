#ifndef DOMAIN_MODEL_VEHICLE_TYPE_H
#define DOMAIN_MODEL_VEHICLE_TYPE_H

typedef struct vehicle_type_t {
    char vehicle_type_id[32];
    char vehicle_type_name[32];
    int max_length_mm;
    int max_width_mm;
    int max_height_mm;
    int min_length_mm;
} vehicle_type_t;

void vehicle_type_init(vehicle_type_t *vehicle_type, const char *vehicle_type_id, const char *vehicle_type_name);

#endif

