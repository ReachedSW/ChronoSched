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
        Lifecycle lifecycle{Lifecycle::Pending}; std::uint64_t version{0}; bool cancellation_counted{false};
    };
    struct QueueEntry { std::chrono::steady_clock::time_point deadline; TaskPriority priority; std::uint64_t sequence, version; std::shared_ptr<Task> task; };
    struct QueueCompare { bool operator()(const QueueEntry& a, const QueueEntry& b) const {
        if (a.deadline != b.deadline) return a.deadline < b.deadline;
        if (a.priority != b.priority) return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        return a.sequence < b.sequence;
    }};

    explicit Runtime(SchedulerOptions options) {
        const auto count = options.worker_threads == 0 ? std::size_t{1} : options.worker_threads;
        timer_ = std::thread([this] { timer_loop(); });
        workers_.reserve(count); for (std::size_t i = 0; i < count; ++i) workers_.emplace_back([this] { worker_loop(); });
    }
    ~Runtime() { shutdown(ShutdownMode::CancelPending); }
    TaskHandle schedule(std::chrono::steady_clock::time_point deadline, Callback callback, TaskPriority priority,
                        bool repeating = false, std::chrono::steady_clock::duration interval = {}, RepeatMode mode = RepeatMode::FixedDelay,
                        CatchUpPolicy catch_up = CatchUpPolicy::SkipMissed) {
        if (!callback) throw std::invalid_argument("ChronoSched callback must not be empty");
        std::unique_lock lock(core_mutex_);
        if (!accepting_) throw std::runtime_error("ChronoSched scheduler is shutting down");
        if (next_id_ == std::numeric_limits<TaskId>::max()) throw std::overflow_error("ChronoSched task ID space exhausted");
        auto state = std::make_shared<detail::TaskState>(); const auto id = ++next_id_;
        auto task = std::make_shared<Task>(Task{id, std::move(callback), state, priority, repeating, mode, catch_up, interval, deadline});
        tasks_.emplace(id, task); enqueue_locked(task, deadline); install_hooks_locked(task);
        lock.unlock(); timer_cv_.notify_one(); return TaskHandle{id, std::move(state)};
    }
    void set_error_handler(ErrorHandler handler) { std::lock_guard lock(core_mutex_); error_handler_ = std::move(handler); }
    SchedulerStats stats() const noexcept { return {queued_.load(), running_.load(), executed_.load(), cancelled_.load(), failures_.load(), late_.load()}; }
    void shutdown(ShutdownMode mode) {
        std::unique_lock shutdown_lock(shutdown_mutex_);
        { std::lock_guard lock(core_mutex_);
            if (joined_) return;
            accepting_ = false; shutdown_requested_ = true; drain_ = mode == ShutdownMode::Drain;
            if (!drain_) {
                for (const auto& [id, task] : tasks_) { (void)id; cancel_locked(task); }
                timed_.clear(); completions_.clear(); std::lock_guard qlock(execution_mutex_); execution_.clear();
            } else for (const auto& [id, task] : tasks_) if (task->repeating) { (void)id; cancel_locked(task); }
        }
        timer_cv_.notify_all(); execution_cv_.notify_all();
        if (timer_.joinable()) timer_.join(); for (auto& worker : workers_) if (worker.joinable()) worker.join();
        std::vector<std::shared_ptr<detail::TaskState>> states;
        { std::lock_guard lock(core_mutex_); joined_ = true;
            for (const auto& [id, task] : tasks_) { (void)id; states.push_back(task->state); }
            tasks_.clear();
        }
        for (const auto& state : states) { std::lock_guard hlock(state->notifier_mutex); state->cancellation_notifier = {}; state->rescheduler = {}; }
    }
private:
    static constexpr std::size_t max_execute_missed_burst = 64;
    void install_hooks_locked(const std::shared_ptr<Task>& task) {
        std::lock_guard hlock(task->state->notifier_mutex); std::weak_ptr<Task> weak = task;
        task->state->cancellation_notifier = [this, weak] { if (const auto task = weak.lock()) { std::lock_guard lock(core_mutex_); cancel_locked(task); } timer_cv_.notify_one(); execution_cv_.notify_all(); };
        task->state->rescheduler = [this, weak](std::chrono::steady_clock::time_point deadline) {
            std::lock_guard lock(core_mutex_); const auto task = weak.lock();
            if (!task || !accepting_ || task->state->cancelled.load() || task->lifecycle != Lifecycle::Pending) return false;
            --queued_; enqueue_locked(task, deadline); if (task->repeating && task->repeat_mode == RepeatMode::FixedRate) task->rate_cursor = deadline;
            timer_cv_.notify_one(); return true;
        };
    }
    void cancel_locked(const std::shared_ptr<Task>& task) {
        if (task->lifecycle == Lifecycle::Completed || task->cancellation_counted) return;
        task->state->cancelled.store(true); task->cancellation_counted = true; ++cancelled_;
        if (task->lifecycle == Lifecycle::Pending || task->lifecycle == Lifecycle::Dispatched) { --queued_; task->lifecycle = Lifecycle::Cancelled; }
    }
    void enqueue_locked(const std::shared_ptr<Task>& task, std::chrono::steady_clock::time_point deadline) {
        ++task->version; task->lifecycle = Lifecycle::Pending; ++queued_;
        timed_.insert({deadline, task->priority, next_sequence_++, task->version, task});
    }
    bool stale_locked(const QueueEntry& entry) const { return entry.version != entry.task->version || entry.task->lifecycle != Lifecycle::Pending || entry.task->state->cancelled.load(); }
    void erase_stale_locked(const QueueEntry& entry) { if (entry.task->state->cancelled.load()) { entry.task->lifecycle = Lifecycle::Cancelled; if (!entry.task->repeating) tasks_.erase(entry.task->id); } }
    void timer_loop() noexcept {
        std::unique_lock lock(core_mutex_);
        while (true) {
            process_completions_locked();
            while (!timed_.empty() && stale_locked(*timed_.begin())) { const auto entry = *timed_.begin(); timed_.erase(timed_.begin()); erase_stale_locked(entry); }
            if (shutdown_requested_ && !drain_) break;
            if (timed_.empty()) { if (shutdown_requested_ && drain_) break; timer_cv_.wait(lock, [this] { return shutdown_requested_ || !timed_.empty() || !completions_.empty(); }); continue; }
            const auto entry = *timed_.begin();
            if (entry.deadline > std::chrono::steady_clock::now()) { timer_cv_.wait_until(lock, entry.deadline); continue; }
            timed_.erase(timed_.begin()); if (stale_locked(entry)) { erase_stale_locked(entry); continue; }
            entry.task->lifecycle = Lifecycle::Dispatched; { std::lock_guard qlock(execution_mutex_); execution_.push_back(entry); } execution_cv_.notify_one();
        }
        timer_finished_ = true; lock.unlock(); execution_cv_.notify_all();
    }
    void worker_loop() noexcept {
        while (true) { QueueEntry entry;
            { std::unique_lock lock(execution_mutex_); execution_cv_.wait(lock, [this] { return !execution_.empty() || timer_finished_; }); if (execution_.empty()) return; entry = execution_.front(); execution_.pop_front(); }
            if (!begin_execution(entry)) continue;
            try { entry.task->callback(); } catch (...) { ++failures_; report_exception(entry.task->id, std::current_exception()); }
            report_completion(entry.task, entry.deadline);
        }
    }
    bool begin_execution(const QueueEntry& entry) {
        std::lock_guard lock(core_mutex_);
        if (entry.version != entry.task->version || entry.task->lifecycle != Lifecycle::Dispatched || entry.task->state->cancelled.load()) { erase_stale_locked(entry); return false; }
        entry.task->lifecycle = Lifecycle::Running; --queued_; ++running_; ++executed_; if (std::chrono::steady_clock::now() > entry.deadline) ++late_; return true;
    }
    void report_exception(TaskId id, std::exception_ptr exception) noexcept { ErrorHandler handler; { std::lock_guard lock(core_mutex_); handler = error_handler_; } if (handler) try { handler(id, exception); } catch (...) {} }
    struct Completion { std::shared_ptr<Task> task; std::chrono::steady_clock::time_point deadline, completed_at; };
    void report_completion(const std::shared_ptr<Task>& task, std::chrono::steady_clock::time_point deadline) {
        std::lock_guard lock(core_mutex_); --running_; const auto now = std::chrono::steady_clock::now();
        if (shutdown_requested_ && (task->repeating || !drain_)) { task->lifecycle = task->state->cancelled.load() ? Lifecycle::Cancelled : Lifecycle::Completed; tasks_.erase(task->id); }
        else if (timer_finished_) { task->lifecycle = Lifecycle::Completed; tasks_.erase(task->id); }
        else { completions_.push_back({task, deadline, now}); timer_cv_.notify_one(); }
    }
    void process_completions_locked() {
        while (!completions_.empty()) { const auto completion = std::move(completions_.front()); completions_.pop_front(); const auto& task = completion.task;
            if (task->lifecycle != Lifecycle::Running) continue;
            if (task->state->cancelled.load() || (shutdown_requested_ && task->repeating)) { task->lifecycle = Lifecycle::Cancelled; tasks_.erase(task->id); continue; }
            if (!task->repeating) { task->lifecycle = Lifecycle::Completed; tasks_.erase(task->id); continue; }
            if (task->repeat_mode == RepeatMode::FixedDelay) enqueue_locked(task, completion.completed_at + task->interval);
            else { const auto d = detail::next_fixed_rate(task->rate_cursor, task->interval, completion.completed_at, task->catch_up, task->missed_burst, max_execute_missed_burst); task->missed_burst = d.missed_burst; task->rate_cursor = d.next_rate_cursor; enqueue_locked(task, d.dispatch_deadline); }
        }
    }
    std::mutex core_mutex_; std::condition_variable timer_cv_; std::set<QueueEntry, QueueCompare> timed_; std::deque<Completion> completions_; std::map<TaskId, std::shared_ptr<Task>> tasks_; ErrorHandler error_handler_;
    TaskId next_id_{0}; std::uint64_t next_sequence_{0}; bool accepting_{true}, shutdown_requested_{false}, drain_{false}, joined_{false}; std::atomic_bool timer_finished_{false}; std::thread timer_; std::mutex shutdown_mutex_;
    std::mutex execution_mutex_; std::condition_variable execution_cv_; std::deque<QueueEntry> execution_; std::vector<std::thread> workers_;
    std::atomic_size_t queued_{0}, running_{0}; std::atomic_uint64_t executed_{0}, cancelled_{0}, failures_{0}, late_{0};
};

Scheduler::Scheduler(SchedulerOptions options) : runtime_(std::make_unique<Runtime>(options)) {}
Scheduler::~Scheduler() = default;
TaskHandle Scheduler::schedule_steady_at(std::chrono::steady_clock::time_point d, Callback c, TaskPriority p) { return runtime_->schedule(d, std::move(c), p); }
TaskHandle Scheduler::schedule_at(std::chrono::steady_clock::time_point d, Callback c) { return schedule_steady_at(d, std::move(c)); }
TaskHandle Scheduler::schedule_at(std::chrono::steady_clock::time_point d, Callback c, TaskPriority p) { return schedule_steady_at(d, std::move(c), p); }
TaskHandle Scheduler::schedule_at(std::chrono::system_clock::time_point d, Callback c) { const auto system_now = std::chrono::system_clock::now(); const auto steady_now = std::chrono::steady_clock::now(); return schedule_steady_at(d <= system_now ? steady_now : steady_now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(d - system_now), std::move(c)); }
TaskHandle Scheduler::schedule_every(std::chrono::steady_clock::duration i, Callback c, RepeatMode m, CatchUpPolicy u, TaskPriority p) { if (i <= i.zero()) throw std::invalid_argument("ChronoSched interval must be positive"); const auto first = std::chrono::steady_clock::now() + i; return runtime_->schedule(first, std::move(c), p, true, i, m, u); }
void Scheduler::set_error_handler(ErrorHandler h) { runtime_->set_error_handler(std::move(h)); }
void Scheduler::shutdown(ShutdownMode m) { runtime_->shutdown(m); }
SchedulerStats Scheduler::stats() const noexcept { return runtime_->stats(); }
} // namespace chronosched
