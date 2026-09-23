#pragma once

#include "chronosched/types.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <chrono>
#include <stdexcept>

namespace chronosched::detail {

struct TaskState {
    std::atomic_bool cancelled{false};
    std::mutex notifier_mutex;
    std::function<void()> cancellation_notifier;
    std::function<bool(std::chrono::steady_clock::time_point)> rescheduler;
};

} // namespace chronosched::detail

namespace chronosched {

class TaskHandle {
public:
    TaskHandle() = default;

    void cancel() const noexcept;
    [[nodiscard]] bool is_cancelled() const noexcept;
    [[nodiscard]] TaskId id() const noexcept;
    bool reschedule_at(std::chrono::steady_clock::time_point deadline) const;
    template<class Rep, class Period> bool reschedule_after(std::chrono::duration<Rep, Period> delay) const {
        if (delay < std::chrono::duration<Rep, Period>::zero()) throw std::invalid_argument("negative delay");
        return reschedule_at(std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay));
    }

private:
    friend class Scheduler;
    TaskHandle(TaskId id, std::shared_ptr<detail::TaskState> state) noexcept;

    TaskId id_{0};
    std::shared_ptr<detail::TaskState> state_;
};

} // namespace chronosched
