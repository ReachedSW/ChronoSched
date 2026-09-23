#include "chronosched/task_handle.hpp"

namespace chronosched {

TaskHandle::TaskHandle(TaskId id, std::shared_ptr<detail::TaskState> state) noexcept
    : id_(id), state_(std::move(state)) {}

void TaskHandle::cancel() const noexcept {
    if (!state_) return;

    std::function<void()> notifier;
    {
        std::lock_guard lock(state_->notifier_mutex);
        notifier = state_->cancellation_notifier;
    }
    if (notifier) notifier();
    else state_->cancelled.store(true, std::memory_order_release);
}

bool TaskHandle::is_cancelled() const noexcept {
    return !state_ || state_->cancelled.load(std::memory_order_acquire);
}

TaskId TaskHandle::id() const noexcept {
    return id_;
}

bool TaskHandle::reschedule_at(std::chrono::steady_clock::time_point deadline) const {
    if (!state_ || state_->cancelled.load(std::memory_order_acquire)) return false;
    std::function<bool(std::chrono::steady_clock::time_point)> rescheduler;
    {
        std::lock_guard lock(state_->notifier_mutex);
        rescheduler = state_->rescheduler;
    }
    return rescheduler && rescheduler(deadline);
}

} // namespace chronosched
