#include "Check.h"
#include <cstring>

// kiln_tests [filter...]: runs every case whose name contains any filter (all without one).
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);   // a crash mid-suite must not eat earlier lines
    int ran = 0;
    for (auto& c : kiln::test::Cases()) {
        bool wanted = argc < 2;
        for (int i = 1; i < argc && !wanted; ++i) wanted = std::strstr(c.name, argv[i]) != nullptr;
        if (!wanted) continue;
        std::printf("%s\n", c.name);
        c.fn();
        ++ran;
    }
    std::printf("%d cases, %d checks, %d failed\n", ran, kiln::test::Checks(), kiln::test::Failures());
    return kiln::test::Failures() || ran == 0 ? 1 : 0;
}
