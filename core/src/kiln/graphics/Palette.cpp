#include "kiln/graphics/Palette.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kiln::gfx {

    namespace {
        constexpr int A(std::uint32_t c) { return int(c >> 24); }
        constexpr int R(std::uint32_t c) { return int((c >> 16) & 0xFF); }
        constexpr int G(std::uint32_t c) { return int((c >> 8) & 0xFF); }
        constexpr int B(std::uint32_t c) { return int(c & 0xFF); }
        constexpr std::uint32_t Argb(int a, int r, int g, int b) {
            return (std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b);
        }
        constexpr std::uint32_t kWhite = 0xFFFFFFFF, kBlack = 0xFF000000;
        float Constrain(float v, float lo, float hi) { return v < lo ? lo : std::min(v, hi); }
    }

    namespace color_utils {
        std::array<float, 3> ColorToHsl(std::uint32_t c) {
            // RGBToHSL.
            const float rf = R(c) / 255.f, gf = G(c) / 255.f, bf = B(c) / 255.f;
            const float max = std::max(rf, std::max(gf, bf)), min = std::min(rf, std::min(gf, bf));
            const float delta = max - min;
            float h, s;
            const float l = (max + min) / 2.f;
            if (max == min) {
                h = s = 0.f;
            } else {
                if (max == rf) h = std::fmod((gf - bf) / delta, 6.f);
                else if (max == gf) h = ((bf - rf) / delta) + 2.f;
                else h = ((rf - gf) / delta) + 4.f;
                s = delta / (1.f - std::fabs(2.f * l - 1.f));
            }
            h = std::fmod(h * 60.f, 360.f);
            if (h < 0) h += 360.f;
            return { Constrain(h, 0.f, 360.f), Constrain(s, 0.f, 1.f), Constrain(l, 0.f, 1.f) };
        }

        std::uint32_t HslToColor(const std::array<float, 3>& hsl) {
            const float h = hsl[0], s = hsl[1], l = hsl[2];
            const float c = (1.f - std::fabs(2 * l - 1.f)) * s;
            const float m = l - 0.5f * c;
            const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
            auto round = [](float v) { return int(std::floor(v + 0.5f)); };   // Math.round(float)
            int r = 0, g = 0, b = 0;
            switch (int(h) / 60) {
                case 0: r = round(255 * (c + m)); g = round(255 * (x + m)); b = round(255 * m); break;
                case 1: r = round(255 * (x + m)); g = round(255 * (c + m)); b = round(255 * m); break;
                case 2: r = round(255 * m); g = round(255 * (c + m)); b = round(255 * (x + m)); break;
                case 3: r = round(255 * m); g = round(255 * (x + m)); b = round(255 * (c + m)); break;
                case 4: r = round(255 * (x + m)); g = round(255 * m); b = round(255 * (c + m)); break;
                case 5: case 6: r = round(255 * (c + m)); g = round(255 * m); b = round(255 * (x + m)); break;
                default: break;
            }
            return Argb(255, std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        }

        double CalculateLuminance(std::uint32_t c) {
            // colorToXYZ's Y / 100.
            auto lin = [](int v) { double s = v / 255.0; return s < 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
            return (lin(R(c)) * 0.2126 + lin(G(c)) * 0.7152 + lin(B(c)) * 0.0722);
        }

        std::uint32_t CompositeColors(std::uint32_t fg, std::uint32_t bg) {
            const int bgA = A(bg), fgA = A(fg);
            const int a = 0xFF - (((0xFF - bgA) * (0xFF - fgA)) / 0xFF);
            auto comp = [&](int fc, int bc) { return a == 0 ? 0 : ((0xFF * fc * fgA) + (bc * bgA * (0xFF - fgA))) / (a * 0xFF); };
            return Argb(a, comp(R(fg), R(bg)), comp(G(fg), G(bg)), comp(B(fg), B(bg)));
        }

        std::uint32_t SetAlphaComponent(std::uint32_t c, int alpha) {
            if (alpha < 0 || alpha > 255) throw std::invalid_argument("alpha must be between 0 and 255.");
            return (c & 0x00FFFFFFu) | (std::uint32_t(alpha) << 24);
        }

        double CalculateContrast(std::uint32_t fg, std::uint32_t bg) {
            if (A(bg) != 255) throw std::invalid_argument("background can not be translucent");
            if (A(fg) < 255) fg = CompositeColors(fg, bg);
            const double l1 = CalculateLuminance(fg) + 0.05, l2 = CalculateLuminance(bg) + 0.05;
            return std::max(l1, l2) / std::min(l1, l2);
        }

        int CalculateMinimumAlpha(std::uint32_t fg, std::uint32_t bg, float minContrastRatio) {
            if (A(bg) != 255) throw std::invalid_argument("background can not be translucent");
            if (CalculateContrast(SetAlphaComponent(fg, 255), bg) < minContrastRatio) return -1;
            int iterations = 0, minAlpha = 0, maxAlpha = 255;
            while (iterations <= 10 && (maxAlpha - minAlpha) > 1) {
                const int test = (minAlpha + maxAlpha) / 2;
                if (CalculateContrast(SetAlphaComponent(fg, test), bg) < minContrastRatio) minAlpha = test;
                else maxAlpha = test;
                ++iterations;
            }
            return maxAlpha;
        }
    }

    // ---- Target -------------------------------------------------------------------------------

    namespace {
        PaletteTarget MakeTarget(int lightness, bool vibrant) {
            PaletteTarget t;
            if (lightness < 0) { t.targetLightness = 0.26f; t.maximumLightness = 0.45f; }          // dark
            else if (lightness == 0) { t.minimumLightness = 0.3f; t.targetLightness = 0.5f; t.maximumLightness = 0.7f; }
            else { t.minimumLightness = 0.55f; t.targetLightness = 0.74f; }                         // light
            if (vibrant) { t.minimumSaturation = 0.35f; t.targetSaturation = 1.f; }
            else { t.targetSaturation = 0.3f; t.maximumSaturation = 0.4f; }
            return t;
        }
    }
    const PaletteTarget& PaletteTarget::LightVibrant() { static const PaletteTarget t = MakeTarget(1, true); return t; }
    const PaletteTarget& PaletteTarget::Vibrant() { static const PaletteTarget t = MakeTarget(0, true); return t; }
    const PaletteTarget& PaletteTarget::DarkVibrant() { static const PaletteTarget t = MakeTarget(-1, true); return t; }
    const PaletteTarget& PaletteTarget::LightMuted() { static const PaletteTarget t = MakeTarget(1, false); return t; }
    const PaletteTarget& PaletteTarget::Muted() { static const PaletteTarget t = MakeTarget(0, false); return t; }
    const PaletteTarget& PaletteTarget::DarkMuted() { static const PaletteTarget t = MakeTarget(-1, false); return t; }

    // ---- Swatch -------------------------------------------------------------------------------

    Palette::Swatch::Swatch(std::uint32_t argb, int population) : m_Rgb(argb), m_Population(population) {}

    void Palette::Swatch::EnsureTextColors() const {
        if (m_TextColors) return;
        using namespace color_utils;
        const int lightBody = CalculateMinimumAlpha(kWhite, m_Rgb, 4.5f), lightTitle = CalculateMinimumAlpha(kWhite, m_Rgb, 3.0f);
        m_TextColors = true;
        if (lightBody != -1 && lightTitle != -1) {
            m_Body = SetAlphaComponent(kWhite, lightBody);
            m_Title = SetAlphaComponent(kWhite, lightTitle);
            return;
        }
        const int darkBody = CalculateMinimumAlpha(kBlack, m_Rgb, 4.5f), darkTitle = CalculateMinimumAlpha(kBlack, m_Rgb, 3.0f);
        if (darkBody != -1 && darkTitle != -1) {
            m_Body = SetAlphaComponent(kBlack, darkBody);
            m_Title = SetAlphaComponent(kBlack, darkTitle);
            return;
        }
        m_Body = lightBody != -1 ? SetAlphaComponent(kWhite, lightBody) : SetAlphaComponent(kBlack, darkBody);
        m_Title = lightTitle != -1 ? SetAlphaComponent(kWhite, lightTitle) : SetAlphaComponent(kBlack, darkTitle);
    }
    std::uint32_t Palette::Swatch::TitleTextColor() const { EnsureTextColors(); return m_Title; }
    std::uint32_t Palette::Swatch::BodyTextColor() const { EnsureTextColors(); return m_Body; }

    Palette::Filter Palette::DefaultFilter() {
        return [](std::uint32_t, const std::array<float, 3>& hsl) {
            const bool white = hsl[2] >= 0.95f, black = hsl[2] <= 0.05f;
            const bool redILine = hsl[0] >= 10.f && hsl[0] <= 37.f && hsl[1] <= 0.82f;
            return !white && !black && !redILine;
        };
    }

    // ---- ColorCutQuantizer --------------------------------------------------------------------

    namespace {
        constexpr int kWidth = 5, kMask = (1 << kWidth) - 1;
        enum Component { kRed = -3, kGreen = -2, kBlue = -1 };
        int ModifyWordWidth(int value, int current, int target) {
            const int v = target > current ? value << (target - current) : value >> (current - target);
            return v & ((1 << target) - 1);
        }
        int QRed(int c) { return (c >> (kWidth + kWidth)) & kMask; }
        int QGreen(int c) { return (c >> kWidth) & kMask; }
        int QBlue(int c) { return c & kMask; }
        int QuantizeFromRgb888(std::uint32_t c) {
            return ModifyWordWidth(R(c), 8, kWidth) << (kWidth + kWidth) | ModifyWordWidth(G(c), 8, kWidth) << kWidth | ModifyWordWidth(B(c), 8, kWidth);
        }
        std::uint32_t ApproximateToRgb888(int r, int g, int b) {
            return Argb(255, ModifyWordWidth(r, kWidth, 8), ModifyWordWidth(g, kWidth, 8), ModifyWordWidth(b, kWidth, 8));
        }
        std::uint32_t ApproximateToRgb888(int c) { return ApproximateToRgb888(QRed(c), QGreen(c), QBlue(c)); }

        void ModifySignificantOctet(std::vector<int>& a, int dimension, int lower, int upper) {
            if (dimension == kGreen) {
                for (int i = lower; i <= upper; ++i) { const int c = a[std::size_t(i)]; a[std::size_t(i)] = QGreen(c) << (kWidth + kWidth) | QRed(c) << kWidth | QBlue(c); }
            } else if (dimension == kBlue) {
                for (int i = lower; i <= upper; ++i) { const int c = a[std::size_t(i)]; a[std::size_t(i)] = QBlue(c) << (kWidth + kWidth) | QGreen(c) << kWidth | QRed(c); }
            }
        }

        struct Quantizer {
            std::vector<int> colors;
            std::vector<int> hist;
            const std::vector<Palette::Filter>& filters;

            bool Ignore(std::uint32_t rgb, const std::array<float, 3>& hsl) const {
                for (const auto& f : filters) if (!f(rgb, hsl)) return true;
                return false;
            }

            struct Vbox {
                int lower, upper, population = 0;
                int minR, maxR, minG, maxG, minB, maxB;
                int Volume() const { return (maxR - minR + 1) * (maxG - minG + 1) * (maxB - minB + 1); }
                int ColorCount() const { return 1 + upper - lower; }
                bool CanSplit() const { return ColorCount() > 1; }
            };

            void Fit(Vbox& v) const {
                int minR = std::numeric_limits<int>::max(), minG = minR, minB = minR;
                int maxR = std::numeric_limits<int>::min(), maxG = maxR, maxB = maxR;
                int count = 0;
                for (int i = v.lower; i <= v.upper; ++i) {
                    const int c = colors[std::size_t(i)];
                    count += hist[std::size_t(c)];
                    const int r = QRed(c), g = QGreen(c), b = QBlue(c);
                    maxR = std::max(maxR, r); minR = std::min(minR, r);
                    maxG = std::max(maxG, g); minG = std::min(minG, g);
                    maxB = std::max(maxB, b); minB = std::min(minB, b);
                }
                v.minR = minR; v.maxR = maxR; v.minG = minG; v.maxG = maxG; v.minB = minB; v.maxB = maxB;
                v.population = count;
            }
            Vbox Make(int lower, int upper) const { Vbox v{ lower, upper }; Fit(v); return v; }

            int FindSplitPoint(const Vbox& v) {
                const int rl = v.maxR - v.minR, gl = v.maxG - v.minG, bl = v.maxB - v.minB;
                const int dim = (rl >= gl && rl >= bl) ? kRed : (gl >= rl && gl >= bl) ? kGreen : kBlue;
                ModifySignificantOctet(colors, dim, v.lower, v.upper);
                std::sort(colors.begin() + v.lower, colors.begin() + v.upper + 1);
                ModifySignificantOctet(colors, dim, v.lower, v.upper);
                const int mid = v.population / 2;
                for (int i = v.lower, count = 0; i <= v.upper; ++i) {
                    count += hist[std::size_t(colors[std::size_t(i)])];
                    if (count >= mid) return std::min(v.upper - 1, i);
                }
                return v.lower;
            }

            Palette::Swatch Average(const Vbox& v) const {
                int rs = 0, gs = 0, bs = 0, total = 0;
                for (int i = v.lower; i <= v.upper; ++i) {
                    const int c = colors[std::size_t(i)], pop = hist[std::size_t(c)];
                    total += pop;
                    rs += pop * QRed(c); gs += pop * QGreen(c); bs += pop * QBlue(c);
                }
                auto mean = [&](int sum) { return int(std::floor(float(sum) / float(total) + 0.5f)); };   // Math.round(float)
                return Palette::Swatch(ApproximateToRgb888(mean(rs), mean(gs), mean(bs)), total);
            }

            // java.util.PriorityQueue (a binary heap ordered by volume, largest first), so equal
            // volumes split and iterate in the same order as upstream.
            struct Heap {
                std::vector<Vbox> q;
                static bool Less(const Vbox& a, const Vbox& b) { return b.Volume() - a.Volume() < 0; }   // compare(a, b) < 0
                void Offer(Vbox v) {
                    q.push_back(v);
                    std::size_t k = q.size() - 1;
                    while (k > 0) {
                        const std::size_t parent = (k - 1) >> 1;
                        if (!Less(v, q[parent])) break;
                        q[k] = q[parent];
                        k = parent;
                    }
                    q[k] = v;
                }
                Vbox Poll() {
                    Vbox result = q.front();
                    Vbox x = q.back();
                    q.pop_back();
                    if (!q.empty()) {
                        std::size_t k = 0;
                        const std::size_t n = q.size(), half = n >> 1;
                        while (k < half) {
                            std::size_t child = (k << 1) + 1;
                            const std::size_t right = child + 1;
                            if (right < n && Less(q[right], q[child])) child = right;   // compare(c, right) > 0
                            if (!Less(q[child], x)) break;
                            q[k] = q[child];
                            k = child;
                        }
                        q[k] = x;
                    }
                    return result;
                }
            };
        };
    }

    std::vector<Palette::Swatch> Palette::Quantize(std::vector<std::uint32_t> pixels, int maxColors, const std::vector<Filter>& filters) {
        Quantizer qz{ {}, std::vector<int>(std::size_t(1) << (kWidth * 3), 0), filters };
        for (std::uint32_t p : pixels) ++qz.hist[std::size_t(QuantizeFromRgb888(p))];
        for (std::size_t c = 0; c < qz.hist.size(); ++c) {
            if (qz.hist[c] > 0) {
                const std::uint32_t rgb = ApproximateToRgb888(int(c));
                if (qz.Ignore(rgb, color_utils::ColorToHsl(rgb))) qz.hist[c] = 0;
            }
            if (qz.hist[c] > 0) qz.colors.push_back(int(c));
        }
        std::vector<Swatch> out;
        if (int(qz.colors.size()) <= maxColors) {
            for (int c : qz.colors) out.emplace_back(ApproximateToRgb888(c), qz.hist[std::size_t(c)]);
            return out;
        }
        Quantizer::Heap pq;
        pq.Offer(qz.Make(0, int(qz.colors.size()) - 1));
        // splitBoxes
        while (int(pq.q.size()) < maxColors) {
            Quantizer::Vbox v = pq.Poll();
            if (!v.CanSplit()) break;   // as upstream: polled and not put back
            const int split = qz.FindSplitPoint(v);
            Quantizer::Vbox upper = qz.Make(split + 1, v.upper);
            v.upper = split;
            qz.Fit(v);
            pq.Offer(upper);
            pq.Offer(v);
        }
        // generateAverageColors, in the heap's array order.
        for (const auto& v : pq.q) {
            Swatch s = qz.Average(v);
            if (!qz.Ignore(s.Rgb(), s.Hsl())) out.push_back(s);
        }
        return out;
    }

    // ---- Builder and Palette --------------------------------------------------------------------

    namespace {
        std::vector<PaletteTarget> DefaultTargets() {
            return { PaletteTarget::LightVibrant(), PaletteTarget::Vibrant(), PaletteTarget::DarkVibrant(),
                     PaletteTarget::LightMuted(), PaletteTarget::Muted(), PaletteTarget::DarkMuted() };
        }
    }

    Palette::Builder Palette::From(const ImageBitmap& bitmap) {
        if (!bitmap) throw std::invalid_argument("Bitmap is not valid");
        Builder b;
        b.m_Bitmap = bitmap;
        b.m_Width = bitmap.Width();
        b.m_Height = bitmap.Height();
        b.m_Filters.push_back(DefaultFilter());
        b.m_Targets = DefaultTargets();
        return b;
    }

    Palette::Builder Palette::FromPixels(std::vector<std::uint32_t> argb, int width, int height) {
        if (width <= 0 || height <= 0 || argb.size() != std::size_t(width) * std::size_t(height)) throw std::invalid_argument("Bitmap is not valid");
        Builder b;
        b.m_Pixels = std::move(argb);
        b.m_Width = width;
        b.m_Height = height;
        b.m_Filters.push_back(DefaultFilter());
        b.m_Targets = DefaultTargets();
        return b;
    }

    Palette::Builder Palette::FromSwatches(std::vector<Swatch> swatches) {
        if (swatches.empty()) throw std::invalid_argument("List of Swatches is not valid");
        Builder b;
        b.m_Swatches = std::move(swatches);
        b.m_Filters.push_back(DefaultFilter());
        return b;
    }

    Palette::Builder& Palette::Builder::SetRegion(int left, int top, int right, int bottom) {
        if (m_Swatches) return *this;
        // Rect.intersect with the bitmap's bounds.
        const int l = std::max(0, left), t = std::max(0, top), r = std::min(m_Width, right), bt = std::min(m_Height, bottom);
        if (!(l < r && t < bt)) throw std::invalid_argument("The given region must intersect with the Bitmap's dimensions.");
        m_Region = Region{ l, t, r, bt };
        return *this;
    }

    Palette::Builder& Palette::Builder::AddTarget(const PaletteTarget& t) {
        if (std::find(m_Targets.begin(), m_Targets.end(), t) == m_Targets.end()) m_Targets.push_back(t);
        return *this;
    }

    Palette Palette::Builder::Generate() {
        std::vector<Swatch> swatches;
        if (m_Swatches) {
            swatches = *m_Swatches;
        } else {
            // The source pixels (straight ARGB).
            std::vector<std::uint32_t> src;
            if (m_Bitmap) {
                const PixelMap map = m_Bitmap->ToPixelMap();
                src.resize(map.Buffer().size());
                for (std::size_t i = 0; i < src.size(); ++i) src[i] = map.Buffer()[i].argb;
            } else {
                src = m_Pixels;
            }
            int w = m_Width, h = m_Height;
            // scaleBitmapDown: createScaledBitmap without filtering (nearest).
            double scale = -1;
            if (m_ResizeArea > 0) {
                const long long area = (long long)w * h;
                if (area > m_ResizeArea) scale = std::sqrt(m_ResizeArea / double(area));
            } else if (m_ResizeMaxDimension > 0) {
                const int maxDim = std::max(w, h);
                if (maxDim > m_ResizeMaxDimension) scale = m_ResizeMaxDimension / double(maxDim);
            }
            std::optional<Region> region = m_Region;
            if (scale > 0) {
                const int sw = int(std::ceil(w * scale)), sh = int(std::ceil(h * scale));
                std::vector<std::uint32_t> scaled(std::size_t(sw) * std::size_t(sh));
                for (int y = 0; y < sh; ++y) {
                    const int sy = std::min(h - 1, int((y + 0.5) * h / sh));
                    for (int x = 0; x < sw; ++x) {
                        const int sx = std::min(w - 1, int((x + 0.5) * w / sw));
                        scaled[std::size_t(y) * std::size_t(sw) + std::size_t(x)] = src[std::size_t(sy) * std::size_t(w) + std::size_t(sx)];
                    }
                }
                if (region) {
                    const double s = sw / double(w);
                    region->left = int(std::floor(region->left * s));
                    region->top = int(std::floor(region->top * s));
                    region->right = std::min(int(std::ceil(region->right * s)), sw);
                    region->bottom = std::min(int(std::ceil(region->bottom * s)), sh);
                }
                src = std::move(scaled);
                w = sw;
                h = sh;
            }
            if (region) {
                std::vector<std::uint32_t> subset;
                for (int y = region->top; y < region->bottom; ++y)
                    for (int x = region->left; x < region->right; ++x) subset.push_back(src[std::size_t(y) * std::size_t(w) + std::size_t(x)]);
                src = std::move(subset);
            }
            swatches = Quantize(std::move(src), m_MaxColors, m_Filters);
        }
        Palette p(std::move(swatches), m_Targets);
        p.Generate();
        return p;
    }

    Palette::Palette(std::vector<Swatch> swatches, std::vector<PaletteTarget> targets) : m_Swatches(std::move(swatches)), m_Targets(std::move(targets)) {
        // findDominantSwatch: the first with the largest population.
        int maxPop = std::numeric_limits<int>::min();
        for (std::size_t i = 0; i < m_Swatches.size(); ++i)
            if (m_Swatches[i].Population() > maxPop) { maxPop = m_Swatches[i].Population(); m_Dominant = int(i); }
    }

    void Palette::Generate() {
        std::vector<std::uint32_t> used;
        const int maxPopulation = m_Dominant >= 0 ? m_Swatches[std::size_t(m_Dominant)].Population() : 1;
        for (PaletteTarget target : m_Targets) {
            // normalizeWeights
            float sum = 0;
            for (float w : { target.saturationWeight, target.lightnessWeight, target.populationWeight }) if (w > 0) sum += w;
            if (sum != 0) {
                if (target.saturationWeight > 0) target.saturationWeight /= sum;
                if (target.lightnessWeight > 0) target.lightnessWeight /= sum;
                if (target.populationWeight > 0) target.populationWeight /= sum;
            }
            float maxScore = 0;
            int best = -1;
            for (std::size_t i = 0; i < m_Swatches.size(); ++i) {
                const Swatch& s = m_Swatches[i];
                const auto hsl = s.Hsl();
                const bool scored = hsl[1] >= target.minimumSaturation && hsl[1] <= target.maximumSaturation && hsl[2] >= target.minimumLightness &&
                                    hsl[2] <= target.maximumLightness && std::find(used.begin(), used.end(), s.Rgb()) == used.end();
                if (!scored) continue;
                float score = 0;
                if (target.saturationWeight > 0) score += target.saturationWeight * (1.f - std::fabs(hsl[1] - target.targetSaturation));
                if (target.lightnessWeight > 0) score += target.lightnessWeight * (1.f - std::fabs(hsl[2] - target.targetLightness));
                if (target.populationWeight > 0) score += target.populationWeight * (s.Population() / float(maxPopulation));
                if (best < 0 || score > maxScore) { best = int(i); maxScore = score; }
            }
            if (best >= 0 && target.exclusive) used.push_back(m_Swatches[std::size_t(best)].Rgb());
            m_Selected.push_back(best);
        }
    }

    const Palette::Swatch* Palette::SwatchFor(const PaletteTarget& target) const {
        for (std::size_t i = 0; i < m_Targets.size(); ++i)
            if (m_Targets[i] == target) return m_Selected[i] < 0 ? nullptr : &m_Swatches[std::size_t(m_Selected[i])];
        return nullptr;
    }

    std::uint32_t Palette::ColorFor(const PaletteTarget& target, std::uint32_t defaultColor) const {
        const Swatch* s = SwatchFor(target);
        return s ? s->Rgb() : defaultColor;
    }

}
