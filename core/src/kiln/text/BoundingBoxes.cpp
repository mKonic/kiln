// BoundingBoxesPath.kt and TextLayout.fillRangeBoundingBoxes: per-line boxes around a text range,
// and the outline that joins them (TextLayoutResult.getBoundingBoxesForRange / fillPathForRange,
// what BackgroundAnnotation draws).
#include "kiln/text/Paragraph.h"

#include <algorithm>
#include <cmath>

namespace kiln::text {

    namespace {
        constexpr std::size_t kMaxCachedRanges = 64;

        int BoxCount(const std::vector<float>& b) { return int(b.size() / 4); }
        float Left(const std::vector<float>& b, int i) { return b[std::size_t(i) * 4]; }
        float Top(const std::vector<float>& b, int i) { return b[std::size_t(i) * 4 + 1]; }
        float Right(const std::vector<float>& b, int i) { return b[std::size_t(i) * 4 + 2]; }
        float Bottom(const std::vector<float>& b, int i) { return b[std::size_t(i) * 4 + 3]; }
        void AddBox(std::vector<float>& b, float l, float t, float r, float bo) { b.insert(b.end(), { l, t, r, bo }); }
        void SetBox(std::vector<float>& b, int i, float l, float t, float r, float bo) {
            const std::size_t k = std::size_t(i) * 4;
            b[k] = l; b[k + 1] = t; b[k + 2] = r; b[k + 3] = bo;
        }

        // sortAndCoalesceLineBoxes: one line's boxes left to right, touching ones merged.
        void SortAndCoalesceLineBoxes(std::vector<float>& b, int first, float lineTop, float lineBottom) {
            const int count = BoxCount(b);
            for (int i = first + 1; i < count; ++i) {
                const float kl = Left(b, i), kt = Top(b, i), kr = Right(b, i), kb = Bottom(b, i);
                int j = i - 1;
                while (j >= first && Left(b, j) > kl) {
                    SetBox(b, j + 1, Left(b, j), Top(b, j), Right(b, j), Bottom(b, j));
                    --j;
                }
                SetBox(b, j + 1, kl, kt, kr, kb);
            }
            int write = first;
            for (int i = first + 1; i < count; ++i) {
                const float prevLeft = Left(b, write), prevRight = Right(b, write);
                if (Left(b, i) <= prevRight + 0.5f) {
                    SetBox(b, write, prevLeft, lineTop, std::max(prevRight, Right(b, i)), lineBottom);
                } else {
                    ++write;
                    SetBox(b, write, Left(b, i), Top(b, i), Right(b, i), Bottom(b, i));
                }
            }
            b.resize(std::size_t(write + 1) * 4);
        }

        // spreadAndCoalesceHorizontalInPlace.
        void SpreadAndCoalesceHorizontal(std::vector<float>& b, float spreadLeft, float spreadRight) {
            const int count = BoxCount(b);
            int write = 0;
            for (int i = 0; i < count; ++i) {
                const float left = Left(b, i) - spreadLeft, right = Right(b, i) + spreadRight;
                if (right <= left) continue;   // no width left
                const float top = Top(b, i), bottom = Bottom(b, i);
                if (write > 0) {
                    const int p = write - 1;
                    if (std::abs(top - Top(b, p)) < 0.5f && std::abs(bottom - Bottom(b, p)) < 0.5f && left <= Right(b, p) + 0.5f) {
                        SetBox(b, p, std::min(Left(b, p), left), std::min(Top(b, p), top), std::max(Right(b, p), right), std::max(Bottom(b, p), bottom));
                        continue;
                    }
                }
                SetBox(b, write++, left, top, right, bottom);
            }
            b.resize(std::size_t(write) * 4);
        }

        // findIndexOfLastBoxInTheSameLine: the end of the boxes sharing the line of `start`.
        int LineEndIndex(const std::vector<float>& b, int start) {
            const float t = Top(b, start), bo = Bottom(b, start);
            int i = start + 1;
            while (i < BoxCount(b) && std::abs(Top(b, i) - t) < 0.5f && std::abs(Bottom(b, i) - bo) < 0.5f) ++i;
            return i;
        }

        // addRectToPath: with the vertical spread; nothing when no height is left.
        void AddRect(gfx::Path& path, float l, float t, float r, float b, ui::Offset radius, bool rounded, float spreadTop, float spreadBottom) {
            const float top = t - spreadTop, bottom = b + spreadBottom;
            if (bottom <= top) return;
            if (rounded) {
                path.AddRoundRect({ { l, top, r, bottom }, radius, radius, radius, radius }, gfx::Path::Direction::Clockwise);
            } else {
                path.AddRect({ l, top, r, bottom }, gfx::Path::Direction::Clockwise);
            }
        }

        // addPolygonWithRoundedCorners: each 90 degree corner (outer or inner step) rounded with a
        // quadratic through the corner, radii scaled down together to fit the shorter edge.
        void AddPolygon(gfx::Path& path, const std::vector<float>& v, ui::Offset radius, bool rounded) {
            const int n = int(v.size() / 2);
            if (n < 3) return;
            if (!rounded) {
                path.MoveTo(v[0], v[1]);
                for (int i = 1; i < n; ++i) path.LineTo(v[std::size_t(i) * 2], v[std::size_t(i) * 2 + 1]);
                path.Close();
                return;
            }
            for (int i = 0; i < n; ++i) {
                const int prev = (i - 1 + n) % n, next = (i + 1) % n;
                const float x = v[std::size_t(i) * 2], y = v[std::size_t(i) * 2 + 1];
                const float dxIn = x - v[std::size_t(prev) * 2], dyIn = y - v[std::size_t(prev) * 2 + 1];
                const float dxOut = v[std::size_t(next) * 2] - x, dyOut = v[std::size_t(next) * 2 + 1] - y;
                const float lenIn = std::abs(dxIn) + std::abs(dyIn), lenOut = std::abs(dxOut) + std::abs(dyOut);
                if (lenIn < 0.01f || lenOut < 0.01f) {
                    if (i == 0) path.MoveTo(x, y); else path.LineTo(x, y);
                    continue;
                }
                const float maxRx = (std::abs(dxIn) + std::abs(dxOut)) / 2, maxRy = (std::abs(dyIn) + std::abs(dyOut)) / 2;
                const float rawRx = std::max(0.0f, radius.x), rawRy = std::max(0.0f, radius.y);
                float scale = 1;
                if (rawRx > maxRx) scale = std::min(scale, maxRx / rawRx);
                if (rawRy > maxRy) scale = std::min(scale, maxRy / rawRy);
                const float rx = rawRx * scale, ry = rawRy * scale;
                const float entryX = x - rx * dxIn / lenIn, entryY = y - ry * dyIn / lenIn;
                const float exitX = x + rx * dxOut / lenOut, exitY = y + ry * dyOut / lenOut;
                if (i == 0) path.MoveTo(entryX, entryY); else path.LineTo(entryX, entryY);
                path.QuadraticTo(x, y, exitX, exitY);
            }
            path.Close();
        }

        // mergeAndAddConnectingLinesToPath: a stack of single-run lines as one clockwise
        // rectilinear outline (top edge, the right side down with a step at each seam where the
        // right edge moves, the bottom edge, the left side up the same way).
        void AddLineStack(gfx::Path& path, const std::vector<float>& b, int first, int end, std::vector<float>& v, ui::Offset radius,
                          bool rounded, float spreadTop, float spreadBottom) {
            const int lines = end - first;
            if (lines <= 0) return;
            if (lines == 1) {
                AddRect(path, Left(b, first), Top(b, first), Right(b, first), Bottom(b, first), radius, rounded, spreadTop, spreadBottom);
                return;
            }
            const int last = end - 1;
            const float minTop = Top(b, first) - spreadTop, maxBottom = Bottom(b, last) + spreadBottom;
            if (maxBottom <= minTop) return;
            v.clear();
            const float firstLeft = Left(b, first);
            float currRight = Right(b, first);
            v.insert(v.end(), { firstLeft, minTop, currRight, minTop });
            for (int i = first; i < last; ++i) {
                const float nextRight = Right(b, i + 1);
                if (std::abs(currRight - nextRight) >= 0.5f) {
                    const float seam = std::clamp((Bottom(b, i) + Top(b, i + 1)) / 2, minTop, maxBottom);
                    v.insert(v.end(), { currRight, seam, nextRight, seam });
                    currRight = nextRight;
                }
            }
            const float lastLeft = Left(b, last);
            float currLeft = std::abs(lastLeft - firstLeft) < 0.5f ? firstLeft : lastLeft;
            v.insert(v.end(), { currRight, maxBottom, currLeft, maxBottom });
            for (int i = last; i >= first + 1; --i) {
                const float raw = Left(b, i - 1);
                const float prevLeft = std::abs(raw - firstLeft) < 0.5f ? firstLeft : raw;
                if (std::abs(currLeft - prevLeft) >= 0.5f) {
                    const float seam = std::clamp((Bottom(b, i - 1) + Top(b, i)) / 2, minTop, maxBottom);
                    v.insert(v.end(), { currLeft, seam, prevLeft, seam });
                    currLeft = prevLeft;
                }
            }
            AddPolygon(path, v, radius, rounded);
        }
    }

    void TextLayoutResult::BoundingBoxCoords(int start, int end, std::vector<float>& target) const {
        target.clear();
        const int length = int(m_Input.text.Length());
        start = std::clamp(start, 0, length);
        end = std::clamp(end, start, length);
        const MultiParagraph& mp = *m_Paragraphs;
        if (start == end || mp.LineCount() == 0 || mp.Paragraphs().empty() || start >= mp.Paragraphs().back().endIndex) return;

        // The cache: ranges already measured (64 at most, then cleared).
        auto& cache = mp.BoundingBoxCache();
        const std::uint64_t key = (std::uint64_t(std::uint32_t(start)) << 32) | std::uint32_t(end);
        if (auto it = cache.find(key); it != cache.end()) { target = it->second; return; }

        // fillRangeBoundingBoxes over the lines the range touches, up to the last laid out one.
        const int clampedEnd = std::min(end, mp.LineEnd(mp.LineCount() - 1));
        if (start < clampedEnd) {
            const int firstLine = mp.LineForOffset(start), lastLine = mp.LineForOffset(clampedEnd - 1);
            for (int line = firstLine; line <= lastLine; ++line) {
                const int lineStart = mp.LineStart(line), lineVisibleEnd = mp.LineEnd(line, true), lineEnd = mp.LineEnd(line);
                const int segStart = std::max(start, lineStart), segEnd = std::min(clampedEnd, lineVisibleEnd);
                if (segStart >= segEnd) continue;
                const float lineTop = mp.LineTop(line), lineBottom = mp.LineBottom(line);
                const bool canUseLineBounds = !mp.IsLineEllipsized(line);
                // The whole visible line: its bounds.
                if (segStart == lineStart && segEnd == lineVisibleEnd && canUseLineBounds) {
                    const float l = mp.LineLeft(line), r = mp.LineRight(line);
                    if (r - l > 0.01f) AddBox(target, l, lineTop, r, lineBottom);
                    continue;
                }
                const bool paraRtl = mp.ParagraphDirection(lineStart) == ResolvedTextDirection::Rtl;
                const auto rtlAt = [&mp](int o) { return mp.BidiRunDirection(o) == ResolvedTextDirection::Rtl; };
                bool hasBidi = false;
                for (int o = segStart; o < segEnd && !hasBidi; ++o) hasBidi = rtlAt(o) != paraRtl;
                // The edge of a run at `offset`: where the character at offset - 1 ends when the
                // offset is the line's end (the position there belongs to the next line).
                const auto runEdge = [&](int offset, bool atEnd, bool rtlRun, bool usePrimary) {
                    if (atEnd && offset == lineEnd) {
                        const RectF box = mp.BoundingBox(offset - 1);
                        return rtlRun ? box.x : box.x + box.w;
                    }
                    return mp.HorizontalPosition(offset, usePrimary);
                };
                const int lineFirstBox = BoxCount(target);
                for (int runStart = segStart; runStart < segEnd;) {
                    const bool rtlRun = rtlAt(runStart);
                    int runEnd = hasBidi ? runStart + 1 : segEnd;
                    while (runEnd < segEnd && rtlAt(runEnd) == rtlRun) ++runEnd;
                    const bool usePrimary = paraRtl == rtlRun;
                    const float startX = runStart == lineStart && canUseLineBounds && !hasBidi ? (!rtlRun ? mp.LineLeft(line) : mp.LineRight(line))
                                                                                                : runEdge(runStart, false, rtlRun, usePrimary);
                    const float endX = runEnd == lineVisibleEnd && canUseLineBounds && !hasBidi ? (!rtlRun ? mp.LineRight(line) : mp.LineLeft(line))
                                                                                                 : runEdge(runEnd, true, rtlRun, usePrimary);
                    const float l = !rtlRun ? startX : endX, r = !rtlRun ? endX : startX;
                    if (r - l > 0.01f) AddBox(target, l, lineTop, r, lineBottom);
                    runStart = runEnd;
                }
                if (BoxCount(target) - lineFirstBox > 1) SortAndCoalesceLineBoxes(target, lineFirstBox, lineTop, lineBottom);
            }
        }
        if (cache.size() >= kMaxCachedRanges) cache.clear();
        cache[key] = target;
    }

    std::vector<ui::Rect> TextLayoutResult::GetBoundingBoxesForRange(int start, int end) const {
        std::vector<float> b;
        BoundingBoxCoords(start, end, b);
        std::vector<ui::Rect> out;
        out.reserve(std::size_t(BoxCount(b)));
        for (int i = 0; i < BoxCount(b); ++i) out.push_back({ Left(b, i), Top(b, i), Right(b, i), Bottom(b, i) });
        return out;
    }

    void TextLayoutResult::FillPathForRange(int start, int end, gfx::Path& target, ui::Offset cornerRadius, bool mergeLines) const {
        PathFromBoundingBoxes(start, end, target, cornerRadius, 0, 0, 0, 0, mergeLines);
    }

    void TextLayoutResult::PathFromBoundingBoxes(int start, int end, gfx::Path& target, ui::Offset radius, float spreadLeft, float spreadTop,
                                                 float spreadRight, float spreadBottom, bool mergeLines) const {
        const gfx::FillType fill = target.fillType;
        target.Reset();
        target.fillType = fill;
        std::vector<float> b;
        BoundingBoxCoords(start, end, b);
        if (b.empty()) return;
        if (spreadLeft != 0 || spreadRight != 0) SpreadAndCoalesceHorizontal(b, spreadLeft, spreadRight);
        const int count = BoxCount(b);
        if (count == 0) return;
        const bool rounded = radius.x > 0 || radius.y > 0;
        if (!mergeLines) {
            for (int i = 0; i < count; ++i) AddRect(target, Left(b, i), Top(b, i), Right(b, i), Bottom(b, i), radius, rounded, spreadTop, spreadBottom);
            return;
        }
        std::vector<float> vertices;
        int i = 0;
        while (i < count) {
            const int lineEnd = LineEndIndex(b, i);
            if (lineEnd - i > 1) {
                // A line with several runs: each its own rectangle.
                for (; i < lineEnd; ++i) AddRect(target, Left(b, i), Top(b, i), Right(b, i), Bottom(b, i), radius, rounded, spreadTop, spreadBottom);
                continue;
            }
            // canMergeWithPreviousLine: single-run lines on consecutive lines that overlap.
            const auto canMerge = [&](int prev, int curr) {
                const int prevLine = LineForVerticalPosition((Top(b, prev) + Bottom(b, prev)) / 2);
                const int currLine = LineForVerticalPosition((Top(b, curr) + Bottom(b, curr)) / 2);
                return currLine == prevLine + 1 && std::max(Left(b, prev), Left(b, curr)) < std::min(Right(b, prev), Right(b, curr));
            };
            const int stackStart = i++;
            while (i < count && LineEndIndex(b, i) == i + 1 && canMerge(i - 1, i)) ++i;
            AddLineStack(target, b, stackStart, i, vertices, radius, rounded, spreadTop, spreadBottom);
        }
    }

}
