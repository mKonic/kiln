#include "Check.h"
int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);   // a crash mid-suite must not eat earlier lines
    for (auto& c : kiln::test::Cases()) {
        std::printf("%s\n", c.name);
        c.fn();
    }
    std::printf("%d checks, %d failed\n", kiln::test::Checks(), kiln::test::Failures());
    return kiln::test::Failures() ? 1 : 0;
}
