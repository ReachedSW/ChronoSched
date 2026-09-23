#include "chronosched/scheduler.hpp"

#include <condition_variable>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace chronosched {

class Scheduler::Runtime {
public:
    struct Task {
        TaskId id;
        std::chrono::steady_clock::time_point deadline;
        Callback callback;
        std::shared_ptr<detail::TaskState> state;
        std::uint64_t sequence;
        // Reserved for Day 2 priority support. All Day 1 tasks use zero.
        int priority{0};
    };

    struct TaskCompare {
        bool operator()(const std::shared_ptr<Task>& left, const std::shared_ptr<Task>& right) const {
            if (left->deadline != right->deadline) {
                return left->deadline < right->deadline;
            }
            if (left->priority != right->priority) {
                return left->priority > right->priority;
            }
            return left->sequence < right->sequence;
        }
    };

    Runtime() : condition_(std::make_shared<std::condition_variable>()), worker_([this] { run(); }) {}

    ~Runtime() { stop(); }

    TaskHandle schedule(std::chrono::steady_clock::time_point deadline, Callback callback) {
        if (!callback) {
            throw std::invalid_argument("ChronoSched callback must not be empty");
        }

        std::unique_lock lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("ChronoSched scheduler is shutting down");
        }
        if (next_id_ == std::numeric_limits<TaskId>::max()) {
            throw std::overflow_error("ChronoSched task ID space exhausted");
        }

        const auto state = std::make_shared<detail::TaskState>();
        const TaskId id = ++next_id_;
        const auto task = std::make_shared<Task>(Task{id, deadline, std::move(callback), state, next_sequence_++});
        const bool notify = queue_.empty() || TaskCompare{}(task, *queue_.begin());
        queue_.insert(task);
        {
            std::lock_guard notifier_lock(state->notifier_mutex);
            state->cancellation_notifier = [wakeup = condition_] { wakeup->notify_one(); };
        }
        lock.unlock();
        if (notify) {
            condition_->notify_one();
        }
        return TaskHandle{id, state};
    }

    void set_error_handler(ErrorHandler handler) {
        std::lock_guard lock(mutex_);
        error_handler_ = std::move(handler);
    }

    void stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            queue_.clear();
        }
        condition_->notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void run() noexcept {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            if (queue_.empty()) {
                condition_->wait(lock, [this] { return stopping_ || !queue_.empty(); });
                continue;
            }

            const auto task = *queue_.begin();
            if (task->state->cancelled.load(std::memory_order_acquire)) {
                queue_.erase(queue_.begin());
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (task->deadline > now) {
                condition_->wait_until(lock, task->deadline);
                continue;
            }

            queue_.erase(queue_.begin());
            lock.unlock();
            execute(task);
            lock.lock();
        }
    }

    void execute(const std::shared_ptr<Task>& task) noexcept {
        if (task->state->cancelled.load(std::memory_order_acquire)) {
            return;
        }
        try {
            task->callback();
        } catch (...) {
            ErrorHandler handler;
            {
                std::lock_guard lock(mutex_);
                handler = error_handler_;
            }
            if (handler) {
                try {
                    handler(task->id, std::current_exception());
                } catch (...) {
                    // Error handlers must not be able to terminate the scheduler thread.
                }
            }
        }
    }

    std::mutex mutex_;
    std::shared_ptr<std::condition_variable> condition_;
    std::set<std::shared_ptr<Task>, TaskCompare> queue_;
    ErrorHandler error_handler_;
    std::thread worker_;
    TaskId next_id_{0};
    std::uint64_t next_sequence_{0};
    bool stopping_{false};
};

Scheduler::Scheduler() : runtime_(std::make_unique<Runtime>()) {}
Scheduler::~Scheduler() = default;

TaskHandle Scheduler::schedule_steady_at(std::chrono::steady_clock::time_point deadline, Callback callback) {
    return runtime_->schedule(deadline, std::move(callback));
}

TaskHandle Scheduler::schedule_at(std::chrono::steady_clock::time_point deadline, Callback callback) {
    return schedule_steady_at(deadline, std::move(callback));
}

TaskHandle Scheduler::schedule_at(std::chrono::system_clock::time_point deadline, Callback callback) {
    const auto system_now = std::chrono::system_clock::now();
    const auto steady_now = std::chrono::steady_clock::now();
    if (deadline <= system_now) {
        return schedule_steady_at(steady_now, std::move(callback));
    }
    const auto remaining = deadline - system_now;
    return schedule_steady_at(steady_now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(remaining), std::move(callback));
}

void Scheduler::set_error_handler(ErrorHandler handler) {
    runtime_->set_error_handler(std::move(handler));
}

} // namespace chronosched
