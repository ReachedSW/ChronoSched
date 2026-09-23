#pragma once

#include "chronosched/task_handle.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>

namespace chronosched {

class Scheduler {
public:
    using Callback = std::function<void()>;
    using ErrorHandler = std::function<void(TaskId, std::exception_ptr)>;

    explicit Scheduler(SchedulerOptions options = {});
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    template <class Rep, class Period>
    [[nodiscard]] TaskHandle schedule_after(std::chrono::duration<Rep, Period> delay, Callback callback) {
        if (delay < std::chrono::duration<Rep, Period>::zero()) {
            throw std::invalid_argument("ChronoSched does not accept negative delays");
        }
        const auto converted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay);
        return schedule_steady_at(std::chrono::steady_clock::now() + converted, std::move(callback));
    }

    template <class Rep, class Period>
    [[nodiscard]] TaskHandle schedule_after(std::chrono::duration<Rep, Period> delay, Callback callback,
                                             TaskPriority priority) {
        if (delay < std::chrono::duration<Rep, Period>::zero()) {
            throw std::invalid_argument("ChronoSched does not accept negative delays");
        }
        const auto converted = std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay);
        return schedule_steady_at(std::chrono::steady_clock::now() + converted, std::move(callback), priority);
    }

    [[nodiscard]] TaskHandle schedule_at(std::chrono::steady_clock::time_point deadline, Callback callback);
    [[nodiscard]] TaskHandle schedule_at(std::chrono::steady_clock::time_point deadline, Callback callback,
                                         TaskPriority priority);
    [[nodiscard]] TaskHandle schedule_at(std::chrono::system_clock::time_point deadline, Callback callback);
    [[nodiscard]] TaskHandle schedule_every(std::chrono::steady_clock::duration interval, Callback callback,
                                             RepeatMode mode = RepeatMode::FixedDelay,
                                             CatchUpPolicy catch_up = CatchUpPolicy::SkipMissed,
                                             TaskPriority priority = TaskPriority::Normal);

    void set_error_handler(ErrorHandler handler);
    void shutdown(ShutdownMode mode = ShutdownMode::CancelPending);
    [[nodiscard]] SchedulerStats stats() const noexcept;

private:
    class Runtime;
    [[nodiscard]] TaskHandle schedule_steady_at(std::chrono::steady_clock::time_point deadline, Callback callback,
                                                 TaskPriority priority = TaskPriority::Normal);

    std::shared_ptr<Runtime> runtime_;
};

} // namespace chronosched
