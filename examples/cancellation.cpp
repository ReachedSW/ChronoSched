#include "chronosched/scheduler.hpp"

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    chronosched::Scheduler scheduler;
    const auto task = scheduler.schedule_after(100ms, [] { std::cout << "This will not print\n"; });
    task.cancel();
    std::this_thread::sleep_for(120ms);
    std::cout << "Cancelled: " << std::boolalpha << task.is_cancelled() << '\n';
    std::cout << "Press Enter to close...";
    std::cin.get();
}
