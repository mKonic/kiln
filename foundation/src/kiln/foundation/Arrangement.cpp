#include "kiln/foundation/Arrangement.h"

#include <algorithm>
#include <numeric>

namespace kiln::foundation {

    namespace {
        template<typename F> void Each(std::span<const int> sizes, bool reversed, F&& f) {
            if (!reversed) for (std::size_t i = 0; i < sizes.size(); ++i) f(i, sizes[i]);
            else for (std::size_t i = sizes.size(); i-- > 0;) f(i, sizes[i]);
        }
        int Sum(std::span<const int> s) { return std::accumulate(s.begin(), s.end(), 0); }

        void PlaceRightOrBottom(int total, std::span<const int> s, std::span<int> out, bool rev) {
            int current = total - Sum(s);
            Each(s, rev, [&](std::size_t i, int v) { out[i] = current; current += v; });
        }
        void PlaceLeftOrTop(std::span<const int> s, std::span<int> out, bool rev) {
            int current = 0;
            Each(s, rev, [&](std::size_t i, int v) { out[i] = current; current += v; });
        }
        void PlaceCenter(int total, std::span<const int> s, std::span<int> out, bool rev) {
            float current = float(total - Sum(s)) / 2;
            Each(s, rev, [&](std::size_t i, int v) { out[i] = RoundJava(current); current += float(v); });
        }
        void PlaceSpaceEvenly(int total, std::span<const int> s, std::span<int> out, bool rev) {
            const float gap = float(total - Sum(s)) / float(s.size() + 1);
            float current = gap;
            Each(s, rev, [&](std::size_t i, int v) { out[i] = RoundJava(current); current += float(v) + gap; });
        }
        void PlaceSpaceBetween(int total, std::span<const int> s, std::span<int> out, bool rev) {
            if (s.empty()) return;
            const int gaps = std::max(int(s.size()) - 1, 1);
            const float gap = float(total - Sum(s)) / float(gaps);
            float current = (rev && s.size() == 1) ? gap : 0.0f;
            Each(s, rev, [&](std::size_t i, int v) { out[i] = RoundJava(current); current += float(v) + gap; });
        }
        void PlaceSpaceAround(int total, std::span<const int> s, std::span<int> out, bool rev) {
            const float gap = s.empty() ? 0.0f : float(total - Sum(s)) / float(s.size());
            float current = gap / 2;
            Each(s, rev, [&](std::size_t i, int v) { out[i] = RoundJava(current); current += float(v) + gap; });
        }
    }

    void Arrangement::Arrange(const Density& d, int total, std::span<const int> s, LayoutDirection dir,
                              std::span<int> out) const {
        const bool rtl = dir == LayoutDirection::Rtl;
        switch (kind) {
            case Kind::Start: if (!rtl) PlaceLeftOrTop(s, out, false); else PlaceRightOrBottom(total, s, out, true); return;
            case Kind::End: if (!rtl) PlaceRightOrBottom(total, s, out, false); else PlaceLeftOrTop(s, out, true); return;
            case Kind::Center: PlaceCenter(total, s, out, rtl); return;
            case Kind::SpaceEvenly: PlaceSpaceEvenly(total, s, out, rtl); return;
            case Kind::SpaceBetween: PlaceSpaceBetween(total, s, out, rtl); return;
            case Kind::SpaceAround: PlaceSpaceAround(total, s, out, rtl); return;
            case Kind::AbsLeft: PlaceLeftOrTop(s, out, false); return;
            case Kind::AbsRight: PlaceRightOrBottom(total, s, out, false); return;
            case Kind::AbsCenter: PlaceCenter(total, s, out, false); return;
            case Kind::AbsSpaceEvenly: PlaceSpaceEvenly(total, s, out, false); return;
            case Kind::AbsSpaceBetween: PlaceSpaceBetween(total, s, out, false); return;
            case Kind::AbsSpaceAround: PlaceSpaceAround(total, s, out, false); return;
            case Kind::Spaced: break;
        }
        // SpacedAligned (Arrangement.kt:578-620).
        if (s.empty()) return;
        const int spacePx = d.RoundToPx(space);
        const bool isRtl = rtlMirror && rtl;
        int lastSpace = 0, freeSpace = total;
        if (isRtl) {
            for (std::size_t i = 0; i < s.size(); ++i) {
                out[i] = std::max(0, freeSpace - s[i]);
                lastSpace = std::min(spacePx, out[i]);
                freeSpace = out[i] - lastSpace;
            }
            freeSpace += lastSpace;
        } else {
            int occupied = 0;
            for (std::size_t i = 0; i < s.size(); ++i) {
                out[i] = std::min(occupied, total - s[i]);
                lastSpace = std::min(spacePx, total - out[i] - s[i]);
                occupied = out[i] + s[i] + lastSpace;
            }
            occupied -= lastSpace;
            freeSpace = total - occupied;
        }
        if (alignBias && freeSpace > 0) {
            int group;
            if (alignIsVertical) group = AlignmentV{ *alignBias }.Align(0, freeSpace);
            else group = AlignmentH{ *alignBias }.Align(0, freeSpace, rtlMirror ? dir : LayoutDirection::Ltr);
            const int offset = isRtl ? group - freeSpace : group;
            if (offset != 0) for (int& p : out) p += offset;
        }
    }

}
