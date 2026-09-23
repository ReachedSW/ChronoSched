#include <catch2/catch_test_macros.hpp>

#include "chronosched/scheduler.hpp"
#include "repeat_calculations.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST_CASE("a one-shot task executes") {
    chronosched::Scheduler scheduler;
    std::promise<void> completed;
    auto future = completed.get_future();
    (void)scheduler.schedule_after(0ms, [&] { completed.set_value(); });
    REQUIRE(future.wait_for(500ms) == std::future_status::ready);
}

TEST_CASE("schedule_after observes its delay") {
    chronosched::Scheduler scheduler;
    const auto started = std::chrono::steady_clock::now();
    std::promise<std::chrono::steady_clock::time_point> completed;
    auto future = completed.get_future();
    (void)scheduler.schedule_after(25ms, [&] { completed.set_value(std::chrono::steady_clock::now()); });
    REQUIRE(future.wait_for(500ms) == std::future_status::ready);
    CHECK(future.get() - started >= 15ms);
}

TEST_CASE("schedule_at accepts steady and system clock deadlines") {
    chronosched::Scheduler scheduler;
    std::atomic_int completed{0};
    std::promise<void> done;
    const auto callback = [&] {
        if (++completed == 2) {
            done.set_value();
        }
    };
    (void)scheduler.schedule_at(std::chrono::steady_clock::now() + 10ms, callback);
    (void)scheduler.schedule_at(std::chrono::system_clock::now() + 15ms, callback);
    REQUIRE(done.get_future().wait_for(500ms) == std::future_status::ready);
}

TEST_CASE("tasks execute in deadline then insertion order") {
    chronosched::Scheduler scheduler;
    std::mutex mutex;
    std::vector<int> observed;
    std::promise<void> done;
    const auto deadline = std::chrono::steady_clock::now() + 35ms;
    for (int value : {1, 2, 3}) {
        (void)scheduler.schedule_at(deadline, [&, value] {
            std::lock_guard lock(mutex);
            observed.push_back(value);
            if (observed.size() == 4) {
                done.set_value();
            }
        });
    }
    (void)scheduler.schedule_after(10ms, [&] {
        std::lock_guard lock(mutex);
        observed.push_back(0);
    });
    REQUIRE(done.get_future().wait_for(500ms) == std::future_status::ready);
    std::lock_guard lock(mutex);
    REQUIRE(observed == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("priority orders tasks with an equal deadline") {
    chronosched::Scheduler scheduler;
    std::mutex mutex;
    std::vector<int> order;
    std::promise<void> done;
    const auto deadline = std::chrono::steady_clock::now() + 30ms;
    (void)scheduler.schedule_at(deadline, [&] { std::lock_guard lock(mutex); order.push_back(1); });
    (void)scheduler.schedule_at(deadline, [&] { std::lock_guard lock(mutex); order.push_back(2); }, chronosched::TaskPriority::Critical);
    (void)scheduler.schedule_at(deadline, [&] { std::lock_guard lock(mutex); order.push_back(3); done.set_value(); });
    REQUIRE(done.get_future().wait_for(500ms) == std::future_status::ready);
    std::lock_guard lock(mutex);
    REQUIRE(order == std::vector<int>{2, 1, 3});
}

TEST_CASE("rescheduling invalidates the original deadline") {
    chronosched::Scheduler scheduler;
    std::promise<void> done;
    const auto handle = scheduler.schedule_after(150ms, [&] { done.set_value(); });
    REQUIRE(handle.reschedule_after(15ms));
    REQUIRE(done.get_future().wait_for(300ms) == std::future_status::ready);
    CHECK_FALSE(handle.reschedule_after(1ms));
}

TEST_CASE("a worker callback does not hold up the timer") {
    chronosched::Scheduler scheduler({.worker_threads = 2});
    std::promise<void> long_started;
    std::promise<void> timely;
    auto long_future = long_started.get_future();
    (void)scheduler.schedule_after(0ms, [&] { long_started.set_value(); std::this_thread::sleep_for(120ms); });
    REQUIRE(long_future.wait_for(300ms) == std::future_status::ready);
    (void)scheduler.schedule_after(10ms, [&] { timely.set_value(); });
    REQUIRE(timely.get_future().wait_for(80ms) == std::future_status::ready);
}

TEST_CASE("a dispatched callback can be cancelled before it starts") {
    chronosched::Scheduler scheduler({.worker_threads = 1});
    std::promise<void> started;
    std::promise<void> release;
    auto release_future = release.get_future().share();
    (void)scheduler.schedule_after(0ms, [&] { started.set_value(); release_future.wait(); });
    REQUIRE(started.get_future().wait_for(300ms) == std::future_status::ready);
    std::atomic_bool invoked{false};
    const auto target = scheduler.schedule_after(0ms, [&] { invoked = true; });
    target.cancel();
    release.set_value();
    std::this_thread::sleep_for(40ms);
    CHECK_FALSE(invoked.load());
}

TEST_CASE("worker and error handler exceptions are contained") {
    chronosched::Scheduler scheduler;
    std::promise<void> survived;
    scheduler.set_error_handler([](chronosched::TaskId, std::exception_ptr) { throw std::runtime_error("handler"); });
    (void)scheduler.schedule_after(0ms, [] { throw std::runtime_error("callback"); });
    (void)scheduler.schedule_after(5ms, [&] { survived.set_value(); });
    REQUIRE(survived.get_future().wait_for(500ms) == std::future_status::ready);
}

TEST_CASE("fixed delay uses callback completion as its reference") {
    chronosched::Scheduler scheduler;
    std::promise<std::chrono::steady_clock::time_point> second;
    std::atomic_int count{0};
    const auto first_start = std::chrono::steady_clock::now();
    chronosched::TaskHandle handle;
    handle = scheduler.schedule_every(25ms, [&] {
        if (++count == 1) std::this_thread::sleep_for(35ms);
        else { second.set_value(std::chrono::steady_clock::now()); handle.cancel(); }
    });
    auto second_future = second.get_future();
    REQUIRE(second_future.wait_for(600ms) == std::future_status::ready);
    const auto second_time = second_future.get();
    CHECK(count.load() == 2);
    CHECK(second_time - first_start >= 75ms);
}

TEST_CASE("repeating tasks keep one identity and stop after cancellation") {
    chronosched::Scheduler scheduler;
    std::promise<void> twice;
    std::atomic_int count{0};
    chronosched::TaskHandle handle;
    handle = scheduler.schedule_every(10ms, [&] { if (++count == 2) { handle.cancel(); twice.set_value(); } }, chronosched::RepeatMode::FixedRate);
    const auto id = handle.id();
    REQUIRE(twice.get_future().wait_for(500ms) == std::future_status::ready);
    std::this_thread::sleep_for(40ms);
    CHECK(handle.id() == id);
    CHECK(count.load() == 2);
}

TEST_CASE("repeating cancellation before first execution prevents all runs") {
    chronosched::Scheduler scheduler;
    std::atomic_int count{0};
    const auto handle = scheduler.schedule_every(20ms, [&] { ++count; });
    handle.cancel();
    std::this_thread::sleep_for(60ms);
    CHECK(count.load() == 0);
}

TEST_CASE("fixed-rate calculations are deterministic") {
    using clock = std::chrono::steady_clock;
    const auto origin = clock::time_point{};
    const auto interval = 10ms;
    const auto now = origin + 35ms;
    const auto skip = chronosched::detail::next_fixed_rate(origin, interval, now, chronosched::CatchUpPolicy::SkipMissed, 0);
    CHECK(skip.dispatch_deadline == origin + 40ms);
    const auto once = chronosched::detail::next_fixed_rate(origin, interval, now, chronosched::CatchUpPolicy::ExecuteOnce, 0);
    CHECK(once.dispatch_deadline == now);
    CHECK(once.next_rate_cursor == origin + 30ms);
    const auto missed = chronosched::detail::next_fixed_rate(origin, interval, now, chronosched::CatchUpPolicy::ExecuteMissed, 0);
    CHECK(missed.dispatch_deadline == origin + 10ms);
    CHECK(missed.missed_burst == 1);
    const auto capped = chronosched::detail::next_fixed_rate(origin, interval, now, chronosched::CatchUpPolicy::ExecuteMissed, 64);
    CHECK(capped.dispatch_deadline == origin + 40ms);
}

TEST_CASE("cancellation prevents queued work") {
    chronosched::Scheduler scheduler;
    std::atomic_bool invoked{false};
    const auto task = scheduler.schedule_after(80ms, [&] { invoked = true; });
    task.cancel();
    CHECK(task.is_cancelled());
    std::this_thread::sleep_for(110ms);
    CHECK_FALSE(invoked.load());
}

TEST_CASE("destroying a handle does not cancel work") {
    chronosched::Scheduler scheduler;
    std::promise<void> done;
    {
        const auto task = scheduler.schedule_after(0ms, [&] { done.set_value(); });
        CHECK_FALSE(task.is_cancelled());
    }
    REQUIRE(done.get_future().wait_for(500ms) == std::future_status::ready);
}

TEST_CASE("callback exceptions are isolated and reported") {
    chronosched::Scheduler scheduler;
    std::promise<chronosched::TaskId> reported;
    auto report = reported.get_future();
    scheduler.set_error_handler([&](chronosched::TaskId id, std::exception_ptr) { reported.set_value(id); });
    const auto faulty = scheduler.schedule_after(0ms, [] { throw std::runtime_error("expected"); });
    REQUIRE(report.wait_for(500ms) == std::future_status::ready);
    CHECK(report.get() == faulty.id());
    std::promise<void> survived;
    (void)scheduler.schedule_after(0ms, [&] { survived.set_value(); });
    REQUIRE(survived.get_future().wait_for(500ms) == std::future_status::ready);
}

TEST_CASE("multiple threads can schedule tasks") {
    chronosched::Scheduler scheduler;
    constexpr int count = 24;
    std::atomic_int completed{0};
    std::promise<void> done;
    std::vector<std::thread> producers;
    for (int index = 0; index < count; ++index) {
        producers.emplace_back([&] {
            (void)scheduler.schedule_after(0ms, [&] {
                if (++completed == count) {
                    done.set_value();
                }
            });
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    REQUIRE(done.get_future().wait_for(1s) == std::future_status::ready);
}

TEST_CASE("negative delays are rejected and zero delays execute") {
    chronosched::Scheduler scheduler;
    CHECK_THROWS_AS(scheduler.schedule_after(-1ms, [] {}), std::invalid_argument);
    std::promise<void> done;
    (void)scheduler.schedule_after(0ms, [&] { done.set_value(); });
    REQUIRE(done.get_future().wait_for(500ms) == std::future_status::ready);
}

TEST_CASE("destruction discards pending callbacks") {
    std::atomic_bool invoked{false};
    {
        chronosched::Scheduler scheduler;
        (void)scheduler.schedule_after(100ms, [&] { invoked = true; });
    }
    std::this_thread::sleep_for(130ms);
    CHECK_FALSE(invoked.load());
}
