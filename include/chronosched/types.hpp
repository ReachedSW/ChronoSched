#pragma once

#include <cstdint>
#include <cstddef>

namespace chronosched {

using TaskId = std::uint64_t;
enum class TaskPriority { Low, Normal, High, Critical };
enum class RepeatMode { FixedDelay, FixedRate };
enum class CatchUpPolicy { SkipMissed, ExecuteOnce, ExecuteMissed };
enum class ShutdownMode { CancelPending, Drain };
struct SchedulerOptions { std::size_t worker_threads{1}; };

/// A point-in-time, lock-free snapshot of scheduler activity.
struct SchedulerStats {
    std::size_t queued_tasks{0};       ///< Accepted callback invocations not yet started.
    std::size_t running_tasks{0};      ///< Callbacks currently executing.
    std::uint64_t executed_callbacks{0}; ///< Callback invocations that began execution.
    std::uint64_t cancelled_tasks{0};  ///< Logical tasks cancelled before completion.
    std::uint64_t callback_failures{0}; ///< Callback invocations that threw.
    std::uint64_t late_executions{0};  ///< Invocations begun after their requested deadline.
};

} // namespace chronosched
