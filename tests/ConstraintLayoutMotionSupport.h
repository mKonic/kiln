#pragma once

// Helpers for the constraintlayout-core motion tests: Java's number formatting.

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace cltest {

    // Float.toString for values in [1e-3, 1e7): the shortest fixed form, with ".0" if integral.
    inline std::string JavaFloat(float v) {
        if (std::isnan(v)) return "NaN";
        char buf[64];
        auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
        (void)ec;
        std::string s(buf, end);
        if (s.find('.') == std::string::npos) s += ".0";
        return s;
    }

    // new DecimalFormat("###.###").format(v) (and "##.##" with places = 2): half-even, no
    // trailing zeros.
    inline std::string DecimalFormat(double v, int places = 3) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", places, v);
        std::string s = buf;
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
        }
        return s;
    }

    inline std::string ArrayToString(const std::vector<float>& f) {
        std::string ret = "[";
        for (std::size_t i = 0; i < f.size(); i++) {
            if (i > 0) ret += ", ";
            ret += DecimalFormat(f[i]);
        }
        return ret + "]";
    }

    inline std::string TextDraw(int dimx, int dimy, const std::vector<float>& x, const std::vector<float>& y, bool flip) {
        float minX = x[0], maxX = x[0], minY = y[0], maxY = y[0];
        std::string ret;
        for (std::size_t i = 0; i < x.size(); i++) {
            minX = std::min(minX, x[i]);
            maxX = std::max(maxX, x[i]);
            minY = std::min(minY, y[i]);
            maxY = std::max(maxY, y[i]);
        }
        std::vector<std::string> c(static_cast<std::size_t>(dimy), std::string(static_cast<std::size_t>(dimx), ' '));
        const int dimx1 = dimx - 1;
        const int dimy1 = dimy - 1;
        for (std::size_t j = 0; j < x.size(); j++) {
            const int xp = int(dimx1 * (x[j] - minX) / (maxX - minX));
            const int yp = int(dimy1 * (y[j] - minY) / (maxY - minY));
            c[std::size_t(flip ? dimy - yp - 1 : yp)][std::size_t(xp)] = '*';
        }
        for (std::size_t i = 0; i < c.size(); i++) {
            float v;
            if (flip) v = (minY - maxY) * (float(i) / (float(c.size()) - 1.0f)) + maxY;
            else v = (maxY - minY) * (float(i) / (float(c.size()) - 1.0f)) + minY;
            v = float(int(v * 1000 + 0.5)) / 1000.f;
            if (i % 5 == 0 || i == c.size() - 1) ret += "|" + c[i] + "| " + JavaFloat(v) + "\n";
            else ret += "|" + c[i] + "|\n";
        }
        const std::string minStr = JavaFloat(float(int(minX * 1000 + 0.5)) / 1000.f);
        const std::string maxStr = JavaFloat(float(int(maxX * 1000 + 0.5)) / 1000.f);
        std::string s = minStr + std::string(static_cast<std::size_t>(dimx), ' ');
        s = s.substr(0, std::size_t(dimx) - maxStr.size() + 2) + maxStr + "\n";
        return ret + s;
    }

}
