#include "chronosched/scheduler.hpp"

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    chronosched::Scheduler scheduler;
    [[maybe_unused]] const auto task =
        scheduler.schedule_after(20ms, [] { std::cout << "ChronoSched task executed\n"; });
    std::this_thread::sleep_for(50ms);
    std::cout << "Press Enter to close...";
    std::cin.get();
}
