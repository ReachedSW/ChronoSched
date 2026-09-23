#pragma once

#include "chronosched/types.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace chronosched::detail {

struct TaskState {
    std::atomic_bool cancelled{false};
    std::mutex notifier_mutex;
    std::function<void()> cancellation_notifier;
};

} // namespace chronosched::detail

namespace chronosched {

class TaskHandle {
public:
    TaskHandle() = default;

    void cancel() const noexcept;
    [[nodiscard]] bool is_cancelled() const noexcept;
    [[nodiscard]] TaskId id() const noexcept;

private:
    friend class Scheduler;
    TaskHandle(TaskId id, std::shared_ptr<detail::TaskState> state) noexcept;

    TaskId id_{0};
    std::shared_ptr<detail::TaskState> state_;
};

} // namespace chronosched
