#ifndef DOMAIN_MODEL_CONDITIONAL_CONTROL_H
#define DOMAIN_MODEL_CONDITIONAL_CONTROL_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"
#include "domain/model/position_trigger.h"

#define MAX_SEGMENT_CONDITIONAL_CONTROLS (4)

/**
 * @file conditional_control.h
 * @brief 定义受限条件控制模型。
 */
/**
 * @brief 描述一条带起止触发条件的控制规则。
 */
typedef struct conditional_control_t
{
    /**< 控制规则 ID。 */
    char control_id[32];
    /**< 控制类型。 */
    conditional_control_kind_t kind;
    /**< 被控执行对象类别。 */
    actuator_category_t control_object;
    /**< 触发判定使用的参考量。 */
    position_reference_t basis;
    /**< 启动触发条件。 */
    position_trigger_t start_trigger;
    /**< 停止触发条件。 */
    position_trigger_t stop_trigger;
    /**< 在退出阶段是否保持有效。 */
    bool active_during_exit;
} conditional_control_t;

/**
 * @brief 将条件控制对象初始化为零值状态。
 * @param conditional_control 条件控制对象，不能为空。
 */
void conditional_control_init(conditional_control_t *conditional_control);
/**
 * @brief 判断条件控制配置是否合法。
 * @param conditional_control 条件控制对象。
 * @return 合法返回 `true`，否则返回 `false`。
 */
bool conditional_control_is_valid(const conditional_control_t *conditional_control);

#endif
