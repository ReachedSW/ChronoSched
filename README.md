# ChronoSched

ChronoSched is a small C++20 scheduler for one-shot events and tasks in long-running services, simulations, and server processes. This repository currently contains the deliberately limited Day 1 core.

## Day 1 capabilities

- Schedule a callback after a duration or at a `steady_clock` / `system_clock` time point.
- Receive a copyable `TaskHandle` with a stable, monotonic ID.
- Cancel pending work safely from any thread.
- Deterministically order equal-deadline tasks by insertion order.
- Isolate callback exceptions through an optional error handler.
- Shut down deterministically: destruction cancels queued tasks and joins the scheduler thread.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The test dependency is Catch2 v3.7.1, fetched by CMake when tests are enabled.

## Basic use

```cpp
#include <chrono>
#include "chronosched/scheduler.hpp"

using namespace std::chrono_literals;

chronosched::Scheduler scheduler;
auto handle = scheduler.schedule_after(500ms, [] {
    // Runs once on ChronoSched's scheduler thread.
});
```

`schedule_after(0ms, ...)` queues work for prompt execution. Negative durations and empty callbacks are rejected with `std::invalid_argument`.

## Cancellation semantics

Calling `handle.cancel()` marks a task cancelled and prevents a pending callback from starting. It is safe to call repeatedly and from multiple threads. Destroying a `TaskHandle` does **not** cancel its task. As with most concurrent APIs, cancellation cannot interrupt a callback that has already started.

## Current limitations

There is one internal thread and it runs callbacks directly. Destruction uses a simple cancel-pending shutdown: new work is rejected, pending callbacks are discarded, and the thread is joined. It does not provide an explicit shutdown API or draining behavior yet.

Planned future work includes repeating tasks, priorities, rescheduling, an executor/worker pool, metrics, and advanced shutdown modes.
