// SPDX-License-Identifier: MIT
#include <cstring>

#include "minitest.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& c : minitest::registry()) {
        if (filter && std::strstr(c.name, filter) == nullptr) continue;
        int before = minitest::failures();
        try {
            c.fn();
        } catch (const std::exception& e) {
            minitest::report(c.name, 0, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            minitest::report(c.name, 0, "unexpected non-standard exception");
        }
        ++run;
        std::cout << (minitest::failures() == before ? "[ ok ] " : "[FAIL] ") << c.name << "\n";
    }
    std::cout << run << " tests, " << minitest::failures() << " failed checks\n";
    return minitest::failures() == 0 ? 0 : 1;
}
