#include "chronosched/scheduler.hpp"
#include "repeat_calculations.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace chronosched {

class Scheduler::Runtime {
public:
    enum class Lifecycle { Pending, Dispatched, Running, Completed, Cancelled };
    struct Task {
        TaskId id; Callback callback; std::shared_ptr<detail::TaskState> state; TaskPriority priority;
        bool repeating; RepeatMode repeat_mode; CatchUpPolicy catch_up; std::chrono::steady_clock::duration interval{};
        std::chrono::steady_clock::time_point rate_cursor{}; std::size_t missed_burst{0};
        Lifecycle lifecycle{Lifecycle::Pending}; std::uint64_t version{0};
    };
    struct QueueEntry {
        std::chrono::steady_clock::time_point deadline; TaskPriority priority; std::uint64_t sequence;
        std::uint64_t version; std::shared_ptr<Task> task;
    };
    struct QueueCompare {
        bool operator()(const QueueEntry& a, const QueueEntry& b) const {
            if (a.deadline != b.deadline) return a.deadline < b.deadline;
            if (a.priority != b.priority) return static_cast<int>(a.priority) > static_cast<int>(b.priority);
            return a.sequence < b.sequence;
        }
    };
    explicit Runtime(SchedulerOptions options) {
        const auto count = options.worker_threads == 0 ? std::size_t{1} : options.worker_threads;
        timer_ = std::thread([this] { timer_loop(); });
        workers_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) workers_.emplace_back([this] { worker_loop(); });
    }
    ~Runtime() { stop(); }

    TaskHandle schedule(std::chrono::steady_clock::time_point deadline, Callback callback, TaskPriority priority,
                        bool repeating = false, std::chrono::steady_clock::duration interval = {},
                        RepeatMode mode = RepeatMode::FixedDelay, CatchUpPolicy catch_up = CatchUpPolicy::SkipMissed) {
        if (!callback) throw std::invalid_argument("ChronoSched callback must not be empty");
        std::unique_lock lock(core_mutex_);
        if (stopping_) throw std::runtime_error("ChronoSched scheduler is shutting down");
        if (next_id_ == std::numeric_limits<TaskId>::max()) throw std::overflow_error("ChronoSched task ID space exhausted");
        auto state = std::make_shared<detail::TaskState>();
        const auto id = ++next_id_;
        auto task = std::make_shared<Task>(Task{id, std::move(callback), state, priority, repeating, mode, catch_up, interval, deadline});
        tasks_.emplace(id, task); enqueue_locked(task, deadline); install_hooks_locked(task);
        lock.unlock(); timer_cv_.notify_one();
        return TaskHandle{id, std::move(state)};
    }
    void set_error_handler(ErrorHandler handler) { std::lock_guard lock(core_mutex_); error_handler_ = std::move(handler); }
    void stop() noexcept {
        std::vector<std::shared_ptr<detail::TaskState>> states;
        {
            std::lock_guard lock(core_mutex_);
            if (stopping_) return;
            stopping_ = true; timed_.clear(); completions_.clear();
            for (const auto& [id, task] : tasks_) { (void)id; task->state->cancelled.store(true, std::memory_order_release); states.push_back(task->state); }
        }
        timer_cv_.notify_all(); execution_cv_.notify_all();
        if (timer_.joinable()) timer_.join();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
        for (const auto& state : states) { std::lock_guard lock(state->notifier_mutex); state->cancellation_notifier = {}; state->rescheduler = {}; }
    }

private:
    static constexpr std::size_t max_execute_missed_burst = 64;
    void install_hooks_locked(const std::shared_ptr<Task>& task) {
        std::lock_guard handle_lock(task->state->notifier_mutex);
        task->state->cancellation_notifier = [this] { timer_cv_.notify_one(); execution_cv_.notify_all(); };
        std::weak_ptr<Task> weak = task;
        task->state->rescheduler = [this, weak](std::chrono::steady_clock::time_point deadline) {
            std::lock_guard lock(core_mutex_);
            const auto task = weak.lock();
            if (!task || stopping_ || task->state->cancelled.load(std::memory_order_acquire) || task->lifecycle != Lifecycle::Pending) return false;
            ++task->version; enqueue_locked(task, deadline);
            if (task->repeating && task->repeat_mode == RepeatMode::FixedRate) task->rate_cursor = deadline;
            timer_cv_.notify_one(); return true;
        };
    }
    void enqueue_locked(const std::shared_ptr<Task>& task, std::chrono::steady_clock::time_point deadline) {
        ++task->version; task->lifecycle = Lifecycle::Pending;
        timed_.insert(QueueEntry{deadline, task->priority, next_sequence_++, task->version, task});
    }
    bool stale_locked(const QueueEntry& entry) const {
        return entry.version != entry.task->version || entry.task->lifecycle != Lifecycle::Pending || entry.task->state->cancelled.load(std::memory_order_acquire);
    }
    void timer_loop() noexcept {
        std::unique_lock lock(core_mutex_);
        while (!stopping_) {
            process_completions_locked();
            while (!timed_.empty() && stale_locked(*timed_.begin())) {
                auto task = timed_.begin()->task; timed_.erase(timed_.begin());
                if (task->state->cancelled.load(std::memory_order_acquire)) { task->lifecycle = Lifecycle::Cancelled; if (!task->repeating) tasks_.erase(task->id); }
            }
            if (stopping_) break;
            if (timed_.empty()) { timer_cv_.wait(lock, [this] { return stopping_ || !timed_.empty() || !completions_.empty(); }); continue; }
            const auto entry = *timed_.begin();
            if (entry.deadline > std::chrono::steady_clock::now()) { timer_cv_.wait_until(lock, entry.deadline); continue; }
            timed_.erase(timed_.begin());
            if (stale_locked(entry)) continue;
            entry.task->lifecycle = Lifecycle::Dispatched;
            { std::lock_guard execution_lock(execution_mutex_); execution_.push_back(entry); }
            execution_cv_.notify_one();
        }
    }
    void worker_loop() noexcept {
        while (true) {
            QueueEntry entry;
            { std::unique_lock lock(execution_mutex_); execution_cv_.wait(lock, [this] { return stopping_ || !execution_.empty(); }); if (stopping_) return; entry = execution_.front(); execution_.pop_front(); }
            if (!begin_execution(entry)) continue;
            try { entry.task->callback(); } catch (...) { report_exception(entry.task->id, std::current_exception()); }
            report_completion(entry.task, entry.deadline);
        }
    }
    bool begin_execution(const QueueEntry& entry) {
        std::lock_guard lock(core_mutex_);
        if (stopping_ || entry.version != entry.task->version || entry.task->lifecycle != Lifecycle::Dispatched || entry.task->state->cancelled.load(std::memory_order_acquire)) {
            if (entry.task->state->cancelled.load(std::memory_order_acquire)) { entry.task->lifecycle = Lifecycle::Cancelled; tasks_.erase(entry.task->id); }
            return false;
        }
        entry.task->lifecycle = Lifecycle::Running; return true;
    }
    void report_exception(TaskId id, std::exception_ptr exception) noexcept {
        ErrorHandler handler; { std::lock_guard lock(core_mutex_); handler = error_handler_; }
        if (handler) try { handler(id, exception); } catch (...) { }
    }
    struct Completion { std::shared_ptr<Task> task; std::chrono::steady_clock::time_point dispatched_deadline, completed_at; };
    void report_completion(const std::shared_ptr<Task>& task, std::chrono::steady_clock::time_point deadline) {
        { std::lock_guard lock(core_mutex_); if (!stopping_) completions_.push_back({task, deadline, std::chrono::steady_clock::now()}); }
        timer_cv_.notify_one();
    }
    void process_completions_locked() {
        while (!completions_.empty()) {
            const auto completion = std::move(completions_.front()); completions_.pop_front(); const auto& task = completion.task;
            if (task->lifecycle != Lifecycle::Running) continue;
            if (task->state->cancelled.load(std::memory_order_acquire)) { task->lifecycle = Lifecycle::Cancelled; tasks_.erase(task->id); continue; }
            if (!task->repeating) { task->lifecycle = Lifecycle::Completed; tasks_.erase(task->id); continue; }
            if (task->repeat_mode == RepeatMode::FixedDelay) { enqueue_locked(task, completion.completed_at + task->interval); continue; }
            schedule_fixed_rate_locked(task, completion.completed_at);
        }
    }
    void schedule_fixed_rate_locked(const std::shared_ptr<Task>& task, std::chrono::steady_clock::time_point now) {
        const auto decision = detail::next_fixed_rate(task->rate_cursor, task->interval, now, task->catch_up,
                                                      task->missed_burst, max_execute_missed_burst);
        task->missed_burst = decision.missed_burst;
        task->rate_cursor = decision.next_rate_cursor;
        enqueue_locked(task, decision.dispatch_deadline);
    }
    std::mutex core_mutex_; std::condition_variable timer_cv_; std::set<QueueEntry, QueueCompare> timed_; std::deque<Completion> completions_;
    std::map<TaskId, std::shared_ptr<Task>> tasks_; ErrorHandler error_handler_; TaskId next_id_{0}; std::uint64_t next_sequence_{0}; std::atomic_bool stopping_{false}; std::thread timer_;
    std::mutex execution_mutex_; std::condition_variable execution_cv_; std::deque<QueueEntry> execution_; std::vector<std::thread> workers_;
};

Scheduler::Scheduler(SchedulerOptions options) : runtime_(std::make_unique<Runtime>(options)) {}
Scheduler::~Scheduler() = default;
TaskHandle Scheduler::schedule_steady_at(std::chrono::steady_clock::time_point deadline, Callback callback, TaskPriority priority) { return runtime_->schedule(deadline, std::move(callback), priority); }
TaskHandle Scheduler::schedule_at(std::chrono::steady_clock::time_point deadline, Callback callback) { return schedule_steady_at(deadline, std::move(callback)); }
TaskHandle Scheduler::schedule_at(std::chrono::steady_clock::time_point deadline, Callback callback, TaskPriority priority) { return schedule_steady_at(deadline, std::move(callback), priority); }
TaskHandle Scheduler::schedule_at(std::chrono::system_clock::time_point deadline, Callback callback) {
    const auto system_now = std::chrono::system_clock::now(); const auto steady_now = std::chrono::steady_clock::now();
    return schedule_steady_at(deadline <= system_now ? steady_now : steady_now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(deadline - system_now), std::move(callback));
}
TaskHandle Scheduler::schedule_every(std::chrono::steady_clock::duration interval, Callback callback, RepeatMode mode, CatchUpPolicy catch_up, TaskPriority priority) {
    if (interval <= interval.zero()) throw std::invalid_argument("ChronoSched interval must be positive");
    const auto first = std::chrono::steady_clock::now() + interval;
    return runtime_->schedule(first, std::move(callback), priority, true, interval, mode, catch_up);
}
void Scheduler::set_error_handler(ErrorHandler handler) { runtime_->set_error_handler(std::move(handler)); }
} // namespace chronosched
