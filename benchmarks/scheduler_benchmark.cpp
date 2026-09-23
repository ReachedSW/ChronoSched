#include "chronosched/scheduler.hpp"
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    using clock = std::chrono::steady_clock;
    for (const auto count : {1'000U, 100'000U}) {
        chronosched::Scheduler scheduler;
        const auto started = clock::now();
        std::vector<chronosched::TaskHandle> handles; handles.reserve(count);
        for (unsigned i = 0; i < count; ++i) handles.push_back(scheduler.schedule_after(std::chrono::hours(1), [] {}));
        const auto inserted = clock::now();
        for (auto& handle : handles) handle.cancel();
        const auto cancelled = clock::now();
        std::cout << count << " insertions: " << std::chrono::duration_cast<std::chrono::milliseconds>(inserted - started).count()
                  << " ms; cancellations: " << std::chrono::duration_cast<std::chrono::milliseconds>(cancelled - inserted).count() << " ms\n";
    }
}
