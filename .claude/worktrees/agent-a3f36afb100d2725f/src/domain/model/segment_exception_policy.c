#include "domain/model/segment_exception_policy.h"

void segment_exception_policy_init(segment_exception_policy_t *segment_exception_policy)
{
    if (segment_exception_policy == 0)
    {
        return;
    }

    segment_exception_policy->on_position_lost = EXCEPTION_STRATEGY_ABORT_SESSION;
    segment_exception_policy->on_follow_lost = EXCEPTION_STRATEGY_ABORT_SESSION;
    segment_exception_policy->on_segment_timeout = EXCEPTION_STRATEGY_ABORT_SESSION;
    segment_exception_policy->on_exit_timeout = EXCEPTION_STRATEGY_ABORT_SESSION;
    segment_exception_policy->on_exit_failure = EXCEPTION_STRATEGY_ABORT_SESSION;
}
