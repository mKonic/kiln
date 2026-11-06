// Nonlinear font scaling (ui-unit fontscaling/FontScaleConverterFactory.android.kt,
// FontScaleConverterTable.android.kt): above a 1.03 font scale, sp sizes map to dp through
// lookup tables in which large text grows less than small text. Scales between the tables are
// interpolated; scales past the last table are linear.

#include "kiln/ui/Geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

namespace kiln::ui {

    namespace {

        constexpr float kMinScaleForNonLinear = 1.03f;
        constexpr std::array<float, 9> kCommonFontSizes{ 8, 10, 12, 14, 18, 20, 24, 30, 100 };

        float Lerp(float a, float b, float t) { return a + (b - a) * t; }
        float LerpInv(float a, float b, float v) { return a != b ? (v - a) / (b - a) : 0.0f; }
        float ConstrainedMap(float rangeMin, float rangeMax, float valueMin, float valueMax, float value) {
            return Lerp(rangeMin, rangeMax, std::max(0.0f, std::min(1.0f, LerpInv(valueMin, valueMax, value))));
        }

        struct Table {
            std::vector<float> from, to;

            static float LookupAndInterpolate(float source, const std::vector<float>& src, const std::vector<float>& dst) {
                const float positive = std::abs(source);
                const float sign = source > 0 ? 1.0f : source < 0 ? -1.0f : 0.0f;
                const auto it = std::lower_bound(src.begin(), src.end(), positive);
                if (it != src.end() && *it == positive) return sign * dst[std::size_t(it - src.begin())];
                const int lower = int(it - src.begin()) - 1;
                float startSp, endSp, startDp, endDp;
                if (lower >= int(src.size()) - 1) {
                    startSp = src.back();
                    startDp = dst.back();
                    if (startSp == 0) return 0;
                    return source * (startDp / startSp);
                } else if (lower == -1) {
                    startSp = 0; startDp = 0;
                    endSp = src[0]; endDp = dst[0];
                } else {
                    startSp = src[std::size_t(lower)]; endSp = src[std::size_t(lower + 1)];
                    startDp = dst[std::size_t(lower)]; endDp = dst[std::size_t(lower + 1)];
                }
                return sign * ConstrainedMap(startDp, endDp, startSp, endSp, positive);
            }
            float SpToDp(float sp) const { return LookupAndInterpolate(sp, from, to); }
            float DpToSp(float dp) const { return LookupAndInterpolate(dp, to, from); }
        };

        int Key(float scale) { return int(scale * 100.0f); }
        float ScaleFromKey(int key) { return float(key) / 100.0f; }

        Table Common(std::initializer_list<float> to) {
            return { { kCommonFontSizes.begin(), kCommonFontSizes.end() }, to };
        }

        // The tables, keyed by scale * 100, plus every converter made since.
        std::map<int, Table>& Tables() {
            static std::map<int, Table> tables{
                { Key(1.15f), Common({ 9.2f, 11.5f, 13.8f, 16.4f, 19.8f, 21.8f, 25.2f, 30, 100 }) },
                { Key(1.3f), Common({ 10.4f, 13, 15.6f, 18.8f, 21.6f, 23.6f, 26.4f, 30, 100 }) },
                { Key(1.5f), Common({ 12, 15, 18, 22, 24, 26, 28, 30, 100 }) },
                { Key(1.8f), Common({ 14.4f, 18, 21.6f, 24.4f, 27.6f, 30.8f, 32.8f, 34.8f, 100 }) },
                { Key(2.0f), Common({ 16, 20, 24, 26, 30, 34, 36, 38, 100 }) },
            };
            return tables;
        }
        std::mutex g_TablesLock;

        // forScale: nullptr when the scaling is linear.
        const Table* ForScale(float fontScale) {
            if (!(fontScale >= kMinScaleForNonLinear)) return nullptr;
            std::lock_guard lock(g_TablesLock);
            auto& tables = Tables();
            if (auto it = tables.find(Key(fontScale)); it != tables.end()) return &it->second;
            const auto higher = tables.upper_bound(Key(fontScale));
            if (higher == tables.end()) return &(tables[Key(fontScale)] = Table{ { 1.0f }, { fontScale } });
            float startScale;
            Table startTable;
            if (higher == tables.begin()) {
                startScale = 1;
                startTable = Common({ 8, 10, 12, 14, 18, 20, 24, 30, 100 });
            } else {
                const auto lower = std::prev(higher);
                startScale = ScaleFromKey(lower->first);
                startTable = lower->second;
            }
            const float endScale = ScaleFromKey(higher->first);
            const float t = ConstrainedMap(0, 1, startScale, endScale, fontScale);
            Table out{ { kCommonFontSizes.begin(), kCommonFontSizes.end() }, {} };
            for (const float sp : kCommonFontSizes) out.to.push_back(Lerp(startTable.SpToDp(sp), higher->second.SpToDp(sp), t));
            return &(tables[Key(fontScale)] = std::move(out));
        }

    }

    float FontScaleSpToDp(float fontScale, float sp) {
        if (const Table* t = ForScale(fontScale)) return t->SpToDp(sp);
        return sp * fontScale;
    }
    float FontScaleDpToSp(float fontScale, float dp) {
        if (const Table* t = ForScale(fontScale)) return t->DpToSp(dp);
        return fontScale != 0 ? dp / fontScale : 0;
    }

    float Density::SpToPx(float sp) const { return FontScaleSpToDp(fontScale, sp) * density; }
    float Density::PxToSp(float px) const { return FontScaleDpToSp(fontScale, px / density); }

}
