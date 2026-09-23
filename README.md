# ChronoSched

ChronoSched is a compact C++20, thread-safe scheduler for one-shot and repeating work in services, simulations, and long-running processes.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Presets: `debug`, `release`, `asan`, and `tsan`. ASAN/TSAN require GCC or Clang; TSAN is intended for Linux.

## Usage

```cpp
using namespace std::chrono_literals;
chronosched::Scheduler scheduler({.worker_threads = 2});
auto task = scheduler.schedule_after(500ms, [] { /* work */ }, chronosched::TaskPriority::High);
task.reschedule_after(100ms); // succeeds only while pending
auto poll = scheduler.schedule_every(1s, [] { /* work */ }, chronosched::RepeatMode::FixedRate,
                                     chronosched::CatchUpPolicy::SkipMissed);
```

Priorities are `Low`, `Normal`, `High`, and `Critical`; equal deadlines preserve insertion order. FixedDelay uses callback completion as its reference. FixedRate preserves the initial timeline. Its catch-up policies are `SkipMissed`, `ExecuteOnce`, and serial `ExecuteMissed` (capped at 64 overdue runs).

Cancellation prevents pending work and may prevent dispatched work before it begins. It does not interrupt callbacks already running, but prevents later repeat occurrences. Handles can outlive a scheduler and destroying a handle does not cancel it. Public methods are thread-safe.

## Shutdown, errors, and metrics

`shutdown(ShutdownMode::CancelPending)` is the destructor default: new work is rejected, pending/dispatched callbacks are cancelled, repeats stop, running callbacks finish, and all threads join.

`shutdown(ShutdownMode::Drain)` rejects new work, runs already accepted one-shot callbacks, and stops repeats after a current/dispatched iteration. It therefore finishes predictably. Shutdown is idempotent; scheduling after it begins throws `std::runtime_error`.

`stats()` snapshots `queued_tasks` (accepted invocations not begun), `running_tasks`, `executed_callbacks`, `cancelled_tasks` (logical tasks), `callback_failures`, and `late_executions` (started after deadline). Callback exceptions are sent to `set_error_handler(TaskId, std::exception_ptr)` and contained; handler exceptions are contained too.

## Tools and platforms

Enable the local benchmark with `-DCHRONOSCHED_BUILD_BENCHMARKS=ON`, then run `chronosched_benchmark`. It reports local elapsed insertion/cancellation times, not universal claims.

The Lua example is optional: configure with `-DCHRONOSCHED_BUILD_LUA_EXAMPLE=ON` and an installed Lua development package. GCC, Clang, and MSVC are covered by CI.

For FreeBSD, install `cmake` and `llvm` (for example, `pkg install cmake llvm`), configure normally, and run tests. FreeBSD build instructions are prepared but not runtime-verified in this environment.

See [CONTRIBUTING.md](CONTRIBUTING.md), [architecture](docs/ARCHITECTURE.md), [threading](docs/THREADING_MODEL.md), and [design decisions](docs/DESIGN_DECISIONS.md). Run `clang-format -i include/chronosched/*.hpp src/*.cpp tests/*.cpp` before contributing.

## Limitations

Callbacks are not preemptible. System-clock deadlines are converted at submission. A callback must not call `shutdown()` on, or destroy, its own scheduler.

## License

MIT; see [LICENSE](LICENSE).
