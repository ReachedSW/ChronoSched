# ChronoSched

ChronoSched is a small C++20, thread-safe scheduler for one-shot and repeating work in long-running services, simulations, and server processes. It uses a steady-clock timer thread and a configurable worker pool; callbacks never run on the timer thread.

## Features

- One-shot scheduling by duration or steady/system-clock deadline.
- Priorities: `Critical`, `High`, `Normal`, and `Low`; equal deadlines then use insertion order.
- Copyable handles with stable monotonic IDs, cancellation, and pending-task rescheduling.
- Repeating tasks with FixedDelay or FixedRate timing and three catch-up policies.
- Isolated callback and error-handler exceptions.
- Deterministic cancel-pending destruction that joins the timer and all workers.

`SchedulerOptions::worker_threads` defaults to one. Zero is accepted and normalized to one.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Usage

```cpp
using namespace std::chrono_literals;
chronosched::Scheduler scheduler({.worker_threads = 2});

auto important = scheduler.schedule_after(500ms, [] { /* work */ },
    chronosched::TaskPriority::High);
important.reschedule_after(100ms); // succeeds while still pending

auto poll = scheduler.schedule_every(1s, [] { /* work */ },
    chronosched::RepeatMode::FixedRate,
    chronosched::CatchUpPolicy::SkipMissed);
// poll.cancel();
```

Cancelling pending work prevents it from starting. Cancelling dispatched work prevents it when the worker has not begun it yet. Running callbacks are never interrupted; cancellation prevents their later repeating occurrences. Destroying a handle does not cancel it.

FixedDelay uses the callback completion time as the next reference. FixedRate stays on its original timeline. When a FixedRate callback is late, `SkipMissed` chooses the first future tick, `ExecuteOnce` performs one immediate catch-up, and `ExecuteMissed` performs overdue ticks serially (with a 64-iteration burst cap).
