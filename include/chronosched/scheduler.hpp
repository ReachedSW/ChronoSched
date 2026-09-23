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

    Scheduler();
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

    [[nodiscard]] TaskHandle schedule_at(std::chrono::steady_clock::time_point deadline, Callback callback);
    [[nodiscard]] TaskHandle schedule_at(std::chrono::system_clock::time_point deadline, Callback callback);

    void set_error_handler(ErrorHandler handler);

private:
    class Runtime;
    [[nodiscard]] TaskHandle schedule_steady_at(std::chrono::steady_clock::time_point deadline, Callback callback);

    std::unique_ptr<Runtime> runtime_;
};

} // namespace chronosched
