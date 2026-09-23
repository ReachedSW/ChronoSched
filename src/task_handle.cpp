#include "chronosched/task_handle.hpp"

namespace chronosched {

TaskHandle::TaskHandle(TaskId id, std::shared_ptr<detail::TaskState> state) noexcept
    : id_(id), state_(std::move(state)) {}

void TaskHandle::cancel() const noexcept {
    if (!state_ || state_->cancelled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    std::lock_guard lock(state_->notifier_mutex);
    if (state_->cancellation_notifier) {
        state_->cancellation_notifier();
    }
}

bool TaskHandle::is_cancelled() const noexcept {
    return !state_ || state_->cancelled.load(std::memory_order_acquire);
}

TaskId TaskHandle::id() const noexcept {
    return id_;
}

bool TaskHandle::reschedule_at(std::chrono::steady_clock::time_point deadline) const {
    if (!state_ || state_->cancelled.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(state_->notifier_mutex);
    return state_->rescheduler && state_->rescheduler(deadline);
}

} // namespace chronosched
