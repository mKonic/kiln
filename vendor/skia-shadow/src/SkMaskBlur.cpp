/*
 * Copyright 2017 Google LLC
 * Use of this source code is governed by a BSD-style license in ../LICENSE.
 */
#include "SkMaskBlur.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace skia_mask {
    namespace {
        // SkGaussFilter::calculate_bessel_factors and normalize (sigma < 2).
        std::vector<std::uint16_t> SmallKernel(double sigma) {
            const double var = sigma * sigma;
            auto bessel0 = [](double t) {
                const double q = t * t / 4;
                double sum = 1, factor = 1;
                for (int k = 1; factor > 1e-6; ++k) { factor *= q / (k * k); sum += factor; }
                return sum;
            };
            auto bessel1 = [](double t) {
                const double q = t * t / 4;
                double sum = t / 2, factor = sum;
                for (int k = 1; factor > 1e-6; ++k) { factor *= q / (k * (k + 1)); sum += factor; }
                return sum;
            };
            const double d = std::exp(var);
            std::array<double, 6> b{ bessel0(var), bessel1(var) }, g{ b[0] / d, b[1] / d };
            int n = 1;
            while (g[n] > 0.01) {
                b[n + 1] = -(2 * n / var) * b[n] + b[n - 1];
                g[n + 1] = b[n + 1] / d;
                ++n;
            }
            double sum = 0;
            for (int i = n - 1; i >= 1; --i) sum += 2 * g[i];
            sum += g[0];
            for (int i = 0; i < n; ++i) g[i] /= sum;
            sum = 0;
            for (int i = n - 1; i >= 1; --i) sum += 2 * g[i];
            g[0] = 1 - sum;
            std::vector<std::uint16_t> result;
            for (int i = 0; i < n; ++i) result.push_back(std::uint16_t(std::round(g[i] * 65536)));
            return result;
        }

        int Window(double sigma) {
            return std::max(1, int(std::floor(sigma * 3 * std::sqrt(2 * std::numbers::pi) / 4 + 0.5)));
        }

        // PlanGauss::Scan's three running sums, with the same integer normalization. A forward
        // zero-padded scan replaces its two-ended scan (the latter avoids scanning a zero tail).
        void Scan(const std::uint8_t* src, int count, std::uint8_t* dst, int stride, int window) {
            const std::array<int, 3> delays{ window - 1, window - 1, (window & 1) ? window - 1 : window };
            std::array<std::vector<std::uint32_t>, 3> history;
            for (int i = 0; i < 3; ++i) history[i].resize(delays[i]);
            std::array<int, 3> cursor{};
            std::array<std::uint32_t, 3> sums{};
            const int tail = delays[0] + delays[1] + delays[2];
            const int divisor = window * window * ((window & 1) ? window : window + 1);
            const std::uint64_t weight = std::uint64_t(std::round(double(1ull << 32) / divisor));
            for (int x = 0; x < count + tail; ++x) {
                const std::uint32_t input = x < count ? src[x] : 0;
                sums[0] += input;
                sums[1] += sums[0];
                sums[2] += sums[1];
                dst[std::size_t(x) * stride] = std::uint8_t((weight * sums[2] + (1ull << 31)) >> 32);
                const std::array<std::uint32_t, 3> leading{ input, sums[0], sums[1] };
                for (int i = 2; i >= 0; --i) {
                    sums[i] -= history[i][cursor[i]];
                    history[i][cursor[i]] = leading[i];
                    if (++cursor[i] == delays[i]) cursor[i] = 0;
                }
            }
        }

        // small_blur's fixed 8.8 arithmetic, scalar rather than 8-lane SIMD. Each product is
        // truncated before summing; each axis rounds back to A8 before the other axis runs.
        void SmallScan(const std::uint8_t* src, int count, int srcStride, std::uint8_t* dst, int dstStride,
                       const std::vector<std::uint16_t>& kernel) {
            const int radius = int(kernel.size()) - 1;
            for (int x = 0; x < count + 2 * radius; ++x) {
                unsigned sum = 128;
                for (int k = -radius; k <= radius; ++k) {
                    const int i = x - radius + k;
                    if (i >= 0 && i < count) sum += (unsigned(src[std::size_t(i) * srcStride]) * kernel[std::abs(k)]) >> 8;
                }
                dst[std::size_t(x) * dstStride] = std::uint8_t(sum >> 8);
            }
        }
    }

    int Border(double sigma) {
        sigma = std::clamp(sigma, 0.0, 135.0);   // SkMaskBlurFilter's overflow bound
        if (sigma <= 1.0 / 3) return 0;
        if (sigma < 2) return int(SmallKernel(sigma).size()) - 1;
        const int w = Window(sigma);
        return (w & 1) ? 3 * ((w - 1) / 2) : 3 * (w / 2) - 1;
    }

    std::vector<std::uint8_t> Blur(const std::vector<std::uint8_t>& src, int width, int height, double sigma) {
        if (!std::isfinite(sigma) || width <= 0 || height <= 0 || src.size() != std::size_t(width) * height) return {};
        sigma = std::clamp(sigma, 0.0, 135.0);
        const int border = Border(sigma);
        if (!border) return src;
        if (width > std::numeric_limits<int>::max() - 2 * border || height > std::numeric_limits<int>::max() - 2 * border) return {};
        const int dw = width + 2 * border, dh = height + 2 * border;
        std::vector<std::uint8_t> out(std::size_t(dw) * dh);
        if (sigma < 2) {
            const auto kernel = SmallKernel(sigma);
            std::vector<std::uint8_t> tmp(std::size_t(width) * dh);
            for (int x = 0; x < width; ++x) SmallScan(src.data() + x, height, width, tmp.data() + x, width, kernel);
            for (int y = 0; y < dh; ++y) SmallScan(tmp.data() + std::size_t(y) * width, width, 1, out.data() + std::size_t(y) * dw, 1, kernel);
        } else {
            // Horizontal and transpose, then vertical and transpose back, as in SkMaskBlurFilter.
            std::vector<std::uint8_t> tmp(std::size_t(height) * dw);
            const int window = Window(sigma);
            for (int y = 0; y < height; ++y) Scan(src.data() + std::size_t(y) * width, width, tmp.data() + y, height, window);
            for (int x = 0; x < dw; ++x) Scan(tmp.data() + std::size_t(x) * height, height, out.data() + x, dw, window);
        }
        return out;
    }
}
