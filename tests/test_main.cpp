#include <catch2/catch_session.hpp>

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    bool no_pause = false;
    std::vector<char*> catch_arguments;
    catch_arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--no-pause") {
            no_pause = true;
        } else {
            catch_arguments.push_back(argv[index]);
        }
    }

    Catch::Session session;
    const int result = session.run(static_cast<int>(catch_arguments.size()), catch_arguments.data());
    if (!no_pause) {
        std::cout << "\nPress Enter to close...";
        std::cin.get();
    }
    return result;
}
