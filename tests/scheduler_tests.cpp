#include <catch2/catch_test_macros.hpp>

#include "chronosched/scheduler.hpp"

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
            if (observed.size() == 3) {
                done.set_value();
            }
        });
    }
    (void)scheduler.schedule_after(10ms, [&] {
        std::lock_guard lock(mutex);
        observed.push_back(0);
    });
    REQUIRE(done.get_future().wait_for(500ms) == std::future_status::ready);
    REQUIRE(observed == std::vector<int>{0, 1, 2, 3});
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
