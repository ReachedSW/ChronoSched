#pragma once

#include "chronosched/types.hpp"

#include <chrono>
#include <cstddef>

namespace chronosched::detail {

struct FixedRateDecision {
    std::chrono::steady_clock::time_point dispatch_deadline;
    std::chrono::steady_clock::time_point next_rate_cursor;
    std::size_t missed_burst;
};

inline FixedRateDecision next_fixed_rate(std::chrono::steady_clock::time_point rate_cursor,
                                         std::chrono::steady_clock::duration interval,
                                         std::chrono::steady_clock::time_point now,
                                         CatchUpPolicy policy, std::size_t missed_burst,
                                         std::size_t burst_cap = 64) {
    auto candidate = rate_cursor + interval;
    if (candidate > now) return {candidate, candidate, 0};
    const auto first_future = candidate + interval * ((now - candidate) / interval + 1);
    switch (policy) {
    case CatchUpPolicy::SkipMissed: return {first_future, first_future, 0};
    // Keep the next original tick as the next candidate after the immediate run.
    case CatchUpPolicy::ExecuteOnce: return {now, first_future - interval, 0};
    case CatchUpPolicy::ExecuteMissed:
        if (missed_burst + 1 > burst_cap) return {first_future, first_future, 0};
        return {candidate, candidate, missed_burst + 1};
    }
    return {first_future, first_future, 0};
}

} // namespace chronosched::detail
