#pragma once

// kiln's stand-in for the one Abseil call material-color-utilities makes here:
// absl::StrCat(absl::Hex(argb)), lowercase hex without padding.

#include <cstdint>
#include <cstdio>
#include <string>

namespace absl {
    struct Hex {
        explicit Hex(std::uint32_t v) : value(v) {}
        std::uint32_t value;
    };
    inline std::string StrCat(const Hex& h) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%x", unsigned(h.value));
        return buf;
    }
}
