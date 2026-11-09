#pragma once

#include <cstdint>

namespace kiln::material3::internal {
    struct LinearProgressMotion { float firstHead, firstTail, secondHead, secondTail; };
    struct CircularProgressMotion { float globalRotation, additionalRotation, progress; };
    LinearProgressMotion LinearProgressMotionAt(std::int64_t nanos);
    CircularProgressMotion CircularProgressMotionAt(std::int64_t nanos);
}
