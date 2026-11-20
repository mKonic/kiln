#pragma once

// Utils (constraintlayout-core motion utils): colour packing and logging. Upstream's socket
// and stack-trace debugging helpers are not ported.

#include <cmath>
#include <cstdio>
#include <string>

namespace kiln::constraintlayout::core::motion {

    struct MotionUtils {
        static void log(const std::string& tag, const std::string& value) { std::printf("%s : %s\n", tag.c_str(), value.c_str()); }
        static void loge(const std::string& tag, const std::string& value) { std::fprintf(stderr, "%s : %s\n", tag.c_str(), value.c_str()); }
        static int clamp(int c) {
            const int n = 255;
            c &= ~(c >> 31);
            c -= n;
            c &= (c >> 31);
            c += n;
            return c;
        }
        // Linear RGBA back to a gamma-encoded ARGB colour.
        static int getInterpolatedColor(const float* value) {
            const int r = clamp(int(float(std::pow(value[0], 1.0 / 2.2)) * 255.0f));
            const int g = clamp(int(float(std::pow(value[1], 1.0 / 2.2)) * 255.0f));
            const int b = clamp(int(float(std::pow(value[2], 1.0 / 2.2)) * 255.0f));
            const int a = clamp(int(value[3] * 255.0f));
            return int((unsigned(a) << 24) | (unsigned(r) << 16) | (unsigned(g) << 8) | unsigned(b));
        }
        static int rgbaTocColor(float r, float g, float b, float a) {
            const int ir = clamp(int(r * 255.0f));
            const int ig = clamp(int(g * 255.0f));
            const int ib = clamp(int(b * 255.0f));
            const int ia = clamp(int(a * 255.0f));
            return int((unsigned(ia) << 24) | (unsigned(ir) << 16) | (unsigned(ig) << 8) | unsigned(ib));
        }
    };

}
