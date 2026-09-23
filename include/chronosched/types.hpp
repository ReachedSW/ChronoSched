#pragma once

#include <cstdint>
#include <cstddef>

namespace chronosched {

using TaskId = std::uint64_t;
enum class TaskPriority { Low, Normal, High, Critical };
enum class RepeatMode { FixedDelay, FixedRate };
enum class CatchUpPolicy { SkipMissed, ExecuteOnce, ExecuteMissed };
struct SchedulerOptions { std::size_t worker_threads{1}; };

} // namespace chronosched
