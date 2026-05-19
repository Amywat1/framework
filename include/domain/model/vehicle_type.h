#ifndef DOMAIN_MODEL_VEHICLE_TYPE_H
#define DOMAIN_MODEL_VEHICLE_TYPE_H

/**
 * @file vehicle_type.h
 * @brief 定义车辆类型模型。
 */
/**
 * @brief 描述一种车辆尺寸约束类型。
 */
typedef struct vehicle_type_t
{
    /**< 车型 ID。 */
    char vehicle_type_id[32];
    /**< 车型名称。 */
    char vehicle_type_name[32];
    /**< 允许的最大车长，单位毫米。 */
    int max_length_mm;
    /**< 允许的最大车宽，单位毫米。 */
    int max_width_mm;
    /**< 允许的最大车高，单位毫米。 */
    int max_height_mm;
    /**< 允许的最小车长，单位毫米。 */
    int min_length_mm;
} vehicle_type_t;

/**
 * @brief 初始化车型对象。
 * @param vehicle_type 车型对象，不能为空。
 * @param vehicle_type_id 车型 ID，可为空。
 * @param vehicle_type_name 车型名称，可为空。
 */
void vehicle_type_init(vehicle_type_t *vehicle_type, const char *vehicle_type_id, const char *vehicle_type_name);

#endif
