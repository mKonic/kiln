#include "kiln/material3/internal/CarouselKeylines.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kiln::material3::internal::carousel {

    namespace {
        float LerpF(float a, float b, float t) { return a + (b - a) * t; }
        int RoundToInt(float v) { return int(std::lround(v)); }
    }

    // --- KeylineList ----------------------------------------------------------------------------

    KeylineList::KeylineList(std::vector<Keyline> keylines) : m_Keylines(std::move(keylines)) {
        float mn = std::numeric_limits<float>::max(), mx = 0;
        for (const auto& k : m_Keylines) {
            if (k.size < mn) mn = k.size;
            if (k.size > mx) mx = k.size;
        }
        minSize = mn;
        maxSize = mx;
        for (int i = 0; i < Size(); ++i) {
            const Keyline& k = m_Keylines[std::size_t(i)];
            if (pivotIndex < 0 && k.isPivot) pivotIndex = i;
            if (firstNonAnchorIndex < 0 && !k.isAnchor) firstNonAnchorIndex = i;
            if (!k.isAnchor) lastNonAnchorIndex = i;
            if (firstFocalIndex < 0 && k.isFocal) firstFocalIndex = i;
            if (k.isFocal) lastFocalIndex = i;
        }
    }

    bool KeylineList::IsFirstFocalItemAtStartOfContainer() const {
        const float left = FirstFocal().offset - FirstFocal().size / 2;
        return left >= 0 && FirstFocal() == FirstNonAnchor();
    }

    bool KeylineList::IsLastFocalItemAtEndOfContainer(float carouselMainAxisSize) const {
        const float right = LastFocal().offset + LastFocal().size / 2;
        return right <= carouselMainAxisSize && LastFocal() == LastNonAnchor();
    }

    int KeylineList::FirstIndexAfterFocalRangeWithSize(float size) const {
        for (int i = lastFocalIndex; i <= LastIndex(); ++i) if ((*this)[i].size == size) return i;
        return LastIndex();
    }

    int KeylineList::LastIndexBeforeFocalRangeWithSize(float size) const {
        for (int i = firstFocalIndex - 1; i >= 0; --i) if ((*this)[i].size == size) return i;
        return 0;
    }

    const Keyline& KeylineList::KeylineBefore(float unadjustedOffset) const {
        for (int i = LastIndex(); i >= 0; --i) if ((*this)[i].unadjustedOffset < unadjustedOffset) return (*this)[i];
        return First();
    }

    const Keyline& KeylineList::KeylineAfter(float unadjustedOffset) const {
        for (const auto& k : m_Keylines) if (k.unadjustedOffset >= unadjustedOffset) return k;
        return Last();
    }

    // --- keylineListOf --------------------------------------------------------------------------

    namespace {
        bool IsCutoffLeft(float size, float offset) { return offset - size / 2 < 0 && offset + size / 2 > 0; }
        bool IsCutoffRight(float size, float offset, float carousel) { return offset - size / 2 < carousel && offset + size / 2 > carousel; }

        struct Tmp {
            const std::vector<KeylineSpec>& keylines;
            int firstFocalIndex = -1;
            float focalItemSize = 0;
            explicit Tmp(const std::vector<KeylineSpec>& k) : keylines(k) {
                // The first focal item: the first of the largest non-anchor items.
                for (int i = 0; i < int(k.size()); ++i)
                    if (!k[std::size_t(i)].isAnchor && k[std::size_t(i)].size > focalItemSize) { firstFocalIndex = i; focalItemSize = k[std::size_t(i)].size; }
            }
            int LastFocalIndex() const {
                if (firstFocalIndex < 0) return -1;
                int last = firstFocalIndex;
                while (last < int(keylines.size()) - 1 && keylines[std::size_t(last + 1)].size == focalItemSize) ++last;
                return last;
            }
        };

        std::vector<Keyline> CreateKeylinesWithPivot(int pivotIndex, float pivotOffset, int firstFocalIndex, int lastFocalIndex,
                                                     float itemMainAxisSize, float carousel, float itemSpacing,
                                                     const std::vector<KeylineSpec>& tmp) {
            if (tmp.empty() || pivotIndex < 0 || pivotIndex >= int(tmp.size())) return {};
            const KeylineSpec& pivot = tmp[std::size_t(pivotIndex)];
            const auto focal = [&](int i) { return i >= firstFocalIndex && i <= lastFocalIndex; };
            std::vector<Keyline> out;
            float pivotCutoff = 0;
            if (IsCutoffLeft(pivot.size, pivotOffset)) pivotCutoff = pivotOffset - pivot.size / 2;
            else if (IsCutoffRight(pivot.size, pivotOffset, carousel)) pivotCutoff = pivotOffset + pivot.size / 2 - carousel;
            out.push_back({ pivot.size, pivotOffset, pivotOffset, focal(pivotIndex), pivot.isAnchor, true, pivotCutoff });

            float offset = pivotOffset - itemMainAxisSize / 2 - itemSpacing;
            float unadjusted = pivotOffset - itemMainAxisSize / 2 - itemSpacing;
            for (int i = pivotIndex - 1; i >= 0; --i) {
                const KeylineSpec& t = tmp[std::size_t(i)];
                const float o = offset - t.size / 2;
                const float u = unadjusted - itemMainAxisSize / 2;
                const float cutoff = IsCutoffLeft(t.size, o) ? std::abs(o - t.size / 2) : 0.0f;
                out.insert(out.begin(), Keyline{ t.size, o, u, focal(i), t.isAnchor, false, cutoff });
                offset -= t.size + itemSpacing;
                unadjusted -= itemMainAxisSize + itemSpacing;
            }

            offset = pivotOffset + itemMainAxisSize / 2 + itemSpacing;
            unadjusted = pivotOffset + itemMainAxisSize / 2 + itemSpacing;
            for (int i = pivotIndex + 1; i < int(tmp.size()); ++i) {
                const KeylineSpec& t = tmp[std::size_t(i)];
                const float o = offset + t.size / 2;
                const float u = unadjusted + itemMainAxisSize / 2;
                const float cutoff = IsCutoffRight(t.size, o, carousel) ? o + t.size / 2 - carousel : 0.0f;
                out.push_back({ t.size, o, u, focal(i), t.isAnchor, false, cutoff });
                offset += t.size + itemSpacing;
                unadjusted += itemMainAxisSize + itemSpacing;
            }
            return out;
        }
    }

    KeylineList KeylineListOf(float carousel, float itemSpacing, CarouselAlignment alignment, const std::vector<KeylineSpec>& keylines) {
        const Tmp t(keylines);
        const int lastFocal = t.LastFocalIndex();
        const int focalItemCount = lastFocal - t.firstFocalIndex;
        float pivotOffset;
        switch (alignment) {
            case CarouselAlignment::Center: {
                // An even number of keylines puts the spacing in the container's centre.
                const float split = itemSpacing == 0 || focalItemCount % 2 == 0 ? 0.0f : itemSpacing / 2;
                const float spaces = float(focalItemCount / 2) * itemSpacing;
                pivotOffset = carousel / 2 - (t.focalItemSize / 2) * float(focalItemCount) - split - spaces;
                break;
            }
            case CarouselAlignment::End: pivotOffset = carousel - t.focalItemSize / 2; break;
            default: pivotOffset = t.focalItemSize / 2; break;
        }
        return KeylineList(CreateKeylinesWithPivot(t.firstFocalIndex, pivotOffset, t.firstFocalIndex, lastFocal, t.focalItemSize,
                                                   carousel, itemSpacing, keylines));
    }

    KeylineList KeylineListOf(float carousel, float itemSpacing, int pivotIndex, float pivotOffset, const std::vector<KeylineSpec>& keylines) {
        const Tmp t(keylines);
        return KeylineList(CreateKeylinesWithPivot(pivotIndex, pivotOffset, t.firstFocalIndex, t.LastFocalIndex(), t.focalItemSize,
                                                   carousel, itemSpacing, keylines));
    }

    Keyline Lerp(const Keyline& a, const Keyline& b, float f) {
        const bool first = f < 0.5f;
        return { LerpF(a.size, b.size, f), LerpF(a.offset, b.offset, f), LerpF(a.unadjustedOffset, b.unadjustedOffset, f),
                 first ? a.isFocal : b.isFocal, first ? a.isAnchor : b.isAnchor, first ? a.isPivot : b.isPivot,
                 LerpF(a.cutoff, b.cutoff, f) };
    }

    KeylineList Lerp(const KeylineList& from, const KeylineList& to, float f) {
        std::vector<Keyline> out;
        out.reserve(std::size_t(from.Size()));
        for (int i = 0; i < from.Size(); ++i) out.push_back(Lerp(from[i], to[i], f));
        return KeylineList(std::move(out));
    }

    float Lerp(float outMin, float outMax, float inMin, float inMax, float value) {
        if (value <= inMin) return outMin;
        if (value >= inMax) return outMax;
        return LerpF(outMin, outMax, (value - inMin) / (inMax - inMin));
    }

    // --- Arrangement ----------------------------------------------------------------------------

    namespace {
        constexpr float MediumItemFlexPercentage = 0.1f;

        float CalculateLargeSize(float available, int smallCount, float smallSize, int mediumCount, int largeCount) {
            return (available - (float(smallCount) + float(mediumCount) / 2) * smallSize) / (float(largeCount) + float(mediumCount) / 2);
        }

        Arrangement Fit(int priority, float availableSpace, float itemSpacing, int smallCount, float smallSize, float minSmallSize,
                        float maxSmallSize, int mediumCount, float mediumSize, int largeCount, float largeSize) {
            const int total = largeCount + mediumCount + smallCount;
            const float available = availableSpace - float(total - 1) * itemSpacing;
            float s = std::clamp(smallSize, minSmallSize, maxSmallSize);
            const float taken = largeSize * float(largeCount) + mediumSize * float(mediumCount) + s * float(smallCount);
            const float delta = available - taken;
            // Resize small items within their range first.
            if (smallCount > 0 && delta > 0) s += std::min(delta / float(smallCount), maxSmallSize - s);
            else if (smallCount > 0 && delta < 0) s += std::max(delta / float(smallCount), minSmallSize - s);
            s = smallCount > 0 ? s : 0.0f;
            float l = CalculateLargeSize(available, smallCount, s, mediumCount, largeCount);
            float m = (l + s) / 2;
            // Win back the large size from the medium items' flex.
            if (mediumCount > 0 && l != largeSize) {
                const float target = (largeSize - l) * float(largeCount);
                const float flex = m * MediumItemFlexPercentage * float(mediumCount);
                const float distribute = std::min(std::abs(target), flex);
                if (target > 0) { m -= distribute / float(mediumCount); l += distribute / float(largeCount); }
                else { m += distribute / float(mediumCount); l -= distribute / float(largeCount); }
            }
            return { priority, s, smallCount, m, mediumCount, l, largeCount };
        }
    }

    bool Arrangement::IsValid() const {
        if (largeCount > 0 && smallCount > 0 && mediumCount > 0) return largeSize > mediumSize && mediumSize > smallSize;
        if (largeCount > 0 && smallCount > 0) return largeSize > smallSize;
        return true;
    }

    float Arrangement::Cost(float targetLargeSize) const {
        if (!IsValid()) return std::numeric_limits<float>::max();
        return std::abs(targetLargeSize - largeSize) * float(priority);
    }

    std::optional<Arrangement> Arrangement::FindLowestCostArrangement(float availableSpace, float itemSpacing, float targetSmallSize,
                                                                      float minSmallSize, float maxSmallSize, const std::vector<int>& smallCounts,
                                                                      float targetMediumSize, const std::vector<int>& mediumCounts,
                                                                      float targetLargeSize, const std::vector<int>& largeCounts) {
        std::optional<Arrangement> best;
        int priority = 1;
        for (int large : largeCounts)
            for (int medium : mediumCounts)
                for (int small : smallCounts) {
                    const Arrangement a = Fit(priority, availableSpace, itemSpacing, small, targetSmallSize, minSmallSize, maxSmallSize,
                                              medium, targetMediumSize, large, targetLargeSize);
                    if (!best || a.Cost(targetLargeSize) < best->Cost(targetLargeSize)) {
                        best = a;
                        // Unchanged large size: nothing later can do better.
                        if (best->Cost(targetLargeSize) == 0) return best;
                    }
                    ++priority;
                }
        return best;
    }

    // --- Keylines.kt ----------------------------------------------------------------------------

    namespace {
        std::vector<int> Descending(int maxCount, int minCount) {
            std::vector<int> v;
            for (int i = 0; i < maxCount - minCount + 1; ++i) v.push_back(maxCount - i);
            return v;
        }
        int MaxOf(const std::vector<int>& v) { return *std::max_element(v.begin(), v.end()); }

        float CalculateMediumChildSize(float minimumMediumSize, float largeItemSize, float remainingSpace) {
            float medium = std::max(remainingSpace * 1.5f, minimumMediumSize);
            const float threshold = largeItemSize * MediumLargeItemDiffThreshold;
            if (medium > threshold) medium = std::min(std::max(threshold, remainingSpace * 1.2f), largeItemSize);
            return medium;
        }
    }

    KeylineList MultiBrowseKeylineList(float density, float carousel, float preferredItemSize, float itemSpacing, int itemCount,
                                       float minSmallItemSize, float maxSmallItemSize) {
        if (carousel == 0 || preferredItemSize == 0) return {};
        std::vector<int> smallCounts{ 1 };
        const std::vector<int> mediumCounts{ 1, 0 };
        const float targetLarge = std::min(preferredItemSize, carousel);
        const float targetSmall = std::clamp(targetLarge / 3, minSmallItemSize, maxSmallItemSize);
        const float targetMedium = (targetLarge + targetSmall) / 2;
        if (carousel < minSmallItemSize * 2) smallCounts = { 0 };
        const float minAvailableLargeSpace = carousel - targetMedium * float(MaxOf(mediumCounts)) - maxSmallItemSize * float(MaxOf(smallCounts));
        const int minLarge = std::max(1, int(std::floor(minAvailableLargeSpace / targetLarge)));
        const int maxLarge = int(std::ceil(carousel / targetLarge));
        const std::vector<int> largeCounts = Descending(maxLarge, minLarge);
        const float anchor = AnchorSizeDp * density;
        auto arrangement = Arrangement::FindLowestCostArrangement(carousel, itemSpacing, targetSmall, minSmallItemSize, maxSmallItemSize,
                                                                  smallCounts, targetMedium, mediumCounts, targetLarge, largeCounts);
        if (arrangement && arrangement->ItemCount() > itemCount) {
            int surplus = arrangement->ItemCount() - itemCount;
            int small = arrangement->smallCount, medium = arrangement->mediumCount;
            while (surplus > 0) {
                if (small > 0) --small;
                else if (medium > 1) --medium;   // keep one medium
                --surplus;
            }
            arrangement = Arrangement::FindLowestCostArrangement(carousel, itemSpacing, targetSmall, minSmallItemSize, maxSmallItemSize,
                                                                 { small }, targetMedium, { medium }, targetLarge, largeCounts);
        }
        if (!arrangement) return {};
        return CreateLeftAlignedKeylineList(carousel, itemSpacing, anchor, anchor, *arrangement);
    }

    KeylineList UncontainedKeylineList(float density, float carousel, float itemSize, float itemSpacing) {
        if (carousel == 0 || itemSize == 0) return {};
        const float largeItemSize = std::min(itemSize + itemSpacing, carousel);
        const int largeCount = std::max(1, int(std::floor(carousel / largeItemSize)));
        const float remaining = carousel - float(largeCount) * largeItemSize;
        const int mediumCount = remaining > 0 ? 1 : 0;
        const float defaultAnchor = AnchorSizeDp * density;
        const float mediumSize = CalculateMediumChildSize(defaultAnchor, largeItemSize, remaining);
        const Arrangement arrangement{ 0, 0, 0, mediumSize, mediumCount, largeItemSize, largeCount };
        const float xSmall = std::min(defaultAnchor, itemSize);
        // Half the cut-off item, so the start's motion matches the end's.
        const float leftAnchor = std::max(xSmall, mediumSize * 0.5f);
        return CreateLeftAlignedKeylineList(carousel, itemSpacing, leftAnchor, defaultAnchor, arrangement);
    }

    KeylineList HeroKeylineList(float density, float carousel, std::optional<float> preferredItemSize, float itemSpacing, int itemCount,
                                bool isCentered, float minSmallItemSize, float maxSmallItemSize) {
        if (carousel == 0) return {};
        const bool center = isCentered && itemCount >= 3;   // fewer than 3 cannot be centred
        std::vector<int> smallCounts = itemCount <= 1 ? std::vector<int>{ 0 } : center ? std::vector<int>{ 2 } : std::vector<int>{ 1 };
        const float targetLarge = std::min(preferredItemSize.value_or(carousel), carousel);
        const float targetSmall = std::clamp(targetLarge / 3, minSmallItemSize, maxSmallItemSize);
        // Room for the small items and a large item at least 25% bigger than them.
        const float fullscreenThreshold = minSmallItemSize * float(MaxOf(smallCounts)) + minSmallItemSize * 1.25f;
        if (carousel < fullscreenThreshold) smallCounts = { 0 };
        const float minAvailableLargeSpace = carousel - minSmallItemSize * float(MaxOf(smallCounts));
        const int minLarge = std::max(1, int(std::floor(minAvailableLargeSpace / targetLarge)));
        const int maxLarge = int(std::ceil(carousel / targetLarge));
        const std::vector<int> largeCounts = Descending(maxLarge, minLarge);
        const float anchor = AnchorSizeDp * density;
        const auto arrangement = Arrangement::FindLowestCostArrangement(carousel, itemSpacing, targetSmall, minSmallItemSize, maxSmallItemSize,
                                                                        smallCounts, 0, { 0 }, targetLarge, largeCounts);
        if (!arrangement) return {};
        if (center && itemCount >= arrangement->ItemCount())
            return CreateCenterAlignedKeylineList(carousel, itemSpacing, anchor, anchor, *arrangement);
        return CreateLeftAlignedKeylineList(carousel, itemSpacing, anchor, anchor, *arrangement);
    }

    KeylineList CreateLeftAlignedKeylineList(float carousel, float itemSpacing, float leftAnchorSize, float rightAnchorSize,
                                             const Arrangement& a) {
        std::vector<KeylineSpec> k{ { leftAnchorSize, true } };
        for (int i = 0; i < a.largeCount; ++i) k.push_back({ a.largeSize });
        for (int i = 0; i < a.mediumCount; ++i) k.push_back({ a.mediumSize });
        for (int i = 0; i < a.smallCount; ++i) k.push_back({ a.smallSize });
        k.push_back({ rightAnchorSize, true });
        return KeylineListOf(carousel, itemSpacing, CarouselAlignment::Start, k);
    }

    KeylineList CreateCenterAlignedKeylineList(float carousel, float itemSpacing, float leftAnchorSize, float rightAnchorSize,
                                               const Arrangement& a) {
        std::vector<KeylineSpec> k{ { leftAnchorSize, true } };
        for (int i = 0; i < a.smallCount / 2; ++i) k.push_back({ a.smallSize });
        for (int i = 0; i < a.mediumCount / 2; ++i) k.push_back({ a.mediumSize });
        for (int i = 0; i < a.largeCount; ++i) k.push_back({ a.largeSize });
        for (int i = 0; i < a.mediumCount / 2; ++i) k.push_back({ a.mediumSize });
        for (int i = 0; i < a.smallCount / 2; ++i) k.push_back({ a.smallSize });
        k.push_back({ rightAnchorSize, true });
        return KeylineListOf(carousel, itemSpacing, CarouselAlignment::Center, k);
    }

    // --- Strategy -------------------------------------------------------------------------------

    namespace {
        std::vector<KeylineSpec> Specs(const KeylineList& l) {
            std::vector<KeylineSpec> v;
            for (const auto& k : l.Items()) v.push_back({ k.size, k.isAnchor });
            return v;
        }

        KeylineList ShiftedForContentPadding(const KeylineList& from, float carousel, float itemSpacing, float contentPadding,
                                             const Keyline& pivot, int pivotIndex) {
            int nonAnchors = 0;
            for (const auto& k : from.Items()) if (!k.isAnchor) ++nonAnchors;
            const float reduction = contentPadding / float(nonAnchors);
            std::vector<KeylineSpec> specs;
            for (const auto& k : from.Items()) specs.push_back({ k.size - std::abs(reduction), k.isAnchor });
            KeylineList shifted = KeylineListOf(carousel, itemSpacing, pivotIndex, pivot.offset - reduction / 2 + contentPadding, specs);
            // The pager still lays items out end to end at full size: keep the unadjusted offsets.
            std::vector<Keyline> out = shifted.Items();
            for (int i = 0; i < int(out.size()); ++i) out[std::size_t(i)].unadjustedOffset = from[i].unadjustedOffset;
            return KeylineList(std::move(out));
        }

        KeylineList MoveKeylineAndShift(const KeylineList& from, int src, int dst, float carousel, float itemSpacing) {
            const float dir = src > dst ? 1.0f : -1.0f;
            const float pivotDelta = (from[src].size - from[src].cutoff + itemSpacing) * dir;
            const int newPivotIndex = from.pivotIndex + int(dir);
            const float newPivotOffset = from.Pivot().offset + pivotDelta;
            std::vector<KeylineSpec> specs = Specs(from);
            const KeylineSpec moved = specs[std::size_t(src)];
            specs.erase(specs.begin() + src);
            specs.insert(specs.begin() + dst, moved);
            return KeylineListOf(carousel, itemSpacing, newPivotIndex, newPivotOffset, specs);
        }

        std::vector<KeylineList> StartSteps(const KeylineList& def, float carousel, float itemSpacing, float before) {
            if (def.Empty()) return {};
            std::vector<KeylineList> steps{ def };
            if (def.IsFirstFocalItemAtStartOfContainer()) {
                if (before != 0) steps.push_back(ShiftedForContentPadding(def, carousel, itemSpacing, before, def.FirstFocal(), def.firstFocalIndex));
                return steps;
            }
            const int start = def.firstNonAnchorIndex, end = def.firstFocalIndex;
            const int count = end - start;
            // No steps, but a cut-off to shift for.
            if (count <= 0 && def.FirstFocal().cutoff > 0) {
                steps.push_back(MoveKeylineAndShift(def, 0, 0, carousel, itemSpacing));
                return steps;
            }
            for (int i = 0; i < count; ++i) {
                const KeylineList prev = steps.back();
                const int original = start + i;
                int dst = def.LastIndex();
                if (original > 0) dst = prev.FirstIndexAfterFocalRangeWithSize(def[original - 1].size) - 1;
                steps.push_back(MoveKeylineAndShift(prev, def.firstNonAnchorIndex, dst, carousel, itemSpacing));
            }
            if (before != 0) {
                const KeylineList last = steps.back();
                steps.back() = ShiftedForContentPadding(last, carousel, itemSpacing, before, last.FirstFocal(), last.firstFocalIndex);
            }
            return steps;
        }

        std::vector<KeylineList> EndSteps(const KeylineList& def, float carousel, float itemSpacing, float after) {
            if (def.Empty()) return {};
            std::vector<KeylineList> steps{ def };
            if (def.IsLastFocalItemAtEndOfContainer(carousel)) {
                if (after != 0) steps.push_back(ShiftedForContentPadding(def, carousel, itemSpacing, -after, def.LastFocal(), def.lastFocalIndex));
                return steps;
            }
            const int start = def.lastFocalIndex, end = def.lastNonAnchorIndex;
            const int count = end - start;
            if (count <= 0 && def.LastFocal().cutoff > 0) {
                steps.push_back(MoveKeylineAndShift(def, 0, 0, carousel, itemSpacing));
                return steps;
            }
            for (int i = 0; i < count; ++i) {
                const KeylineList prev = steps.back();
                const int original = end - i;
                int dst = 0;
                if (original < def.LastIndex()) dst = prev.LastIndexBeforeFocalRangeWithSize(def[original + 1].size) + 1;
                steps.push_back(MoveKeylineAndShift(prev, def.lastNonAnchorIndex, dst, carousel, itemSpacing));
            }
            if (after != 0) {
                const KeylineList last = steps.back();
                steps.back() = ShiftedForContentPadding(last, carousel, itemSpacing, -after, last.LastFocal(), last.lastFocalIndex);
            }
            return steps;
        }

        float StartShiftDistance(const std::vector<KeylineList>& steps, float before) {
            if (steps.empty()) return 0;
            return std::max(steps.back().First().unadjustedOffset - steps.front().First().unadjustedOffset, before);
        }
        float EndShiftDistance(const std::vector<KeylineList>& steps, float after) {
            if (steps.empty()) return 0;
            return std::max(steps.front().Last().unadjustedOffset - steps.back().Last().unadjustedOffset, after);
        }

        std::vector<float> StepInterpolationPoints(float total, const std::vector<KeylineList>& steps, bool shiftingLeft) {
            std::vector<float> points{ 0 };
            if (total == 0 || steps.empty()) return points;
            for (int i = 1; i < int(steps.size()); ++i) {
                const KeylineList& prev = steps[std::size_t(i - 1)];
                const KeylineList& cur = steps[std::size_t(i)];
                const float shifted = shiftingLeft ? cur.First().unadjustedOffset - prev.First().unadjustedOffset
                                                   : prev.Last().unadjustedOffset - cur.Last().unadjustedOffset;
                points.push_back(i == int(steps.size()) - 1 ? 1.0f : points[std::size_t(i - 1)] + shifted / total);
            }
            return points;
        }
    }

    Strategy::Strategy(KeylineList def, float available, float spacing, float before, float after)
        : defaultKeylines(std::move(def)), availableSpace(available), itemSpacing(spacing), beforeContentPadding(before), afterContentPadding(after) {
        startKeylineSteps = StartSteps(defaultKeylines, available, spacing, before);
        endKeylineSteps = EndSteps(defaultKeylines, available, spacing, after);
        float mn = defaultKeylines.minSize, mx = defaultKeylines.maxSize;
        for (const auto* list : { &startKeylineSteps, &endKeylineSteps })
            for (const auto& s : *list) { mn = std::min(mn, s.minSize); mx = std::max(mx, s.maxSize); }
        minItemSize = mn;
        maxItemSize = mx;
        m_StartShiftDistance = StartShiftDistance(startKeylineSteps, before);
        m_EndShiftDistance = EndShiftDistance(endKeylineSteps, after);
        m_StartShiftPoints = StepInterpolationPoints(m_StartShiftDistance, startKeylineSteps, true);
        m_EndShiftPoints = StepInterpolationPoints(m_EndShiftDistance, endKeylineSteps, false);
        isValid = !defaultKeylines.Empty() && availableSpace != 0 && ItemMainAxisSize() != 0;
    }

    KeylineList Strategy::KeylineListForScrollOffset(float scrollOffset, float maxScrollOffset, bool roundToNearestStep) const {
        const float offset = std::max(0.0f, scrollOffset);   // slightly negative from rounding
        const float startShift = m_StartShiftDistance;
        const float endShift = std::max(0.0f, maxScrollOffset - m_EndShiftDistance);
        if (offset >= startShift && offset <= endShift) return defaultKeylines;

        float interpolation = Lerp(1.0f, 0.0f, 0.0f, startShift, offset);
        const std::vector<float>* points = &m_StartShiftPoints;
        const std::vector<KeylineList>* steps = &startKeylineSteps;
        std::vector<KeylineList> skip;
        if (offset > endShift) {
            interpolation = Lerp(0.0f, 1.0f, endShift, maxScrollOffset, offset);
            points = &m_EndShiftPoints;
            steps = &endKeylineSteps;
            // End shifting from the very start: go from the last start step straight to the last
            // end step, skipping the defaults.
            if (endShift < 0.01f && startKeylineSteps.size() == 2 && endKeylineSteps.size() == 2) {
                skip = { startKeylineSteps.back(), endKeylineSteps.back() };
                steps = &skip;
            }
        }
        int from = 0, to = 0;
        float stepped = 0;
        float lower = (*points)[0];
        for (int i = 1; i < int(steps->size()); ++i) {
            const float upper = (*points)[std::size_t(i)];
            if (interpolation <= upper) {
                from = i - 1;
                to = i;
                stepped = Lerp(0.0f, 1.0f, lower, upper, interpolation);
                break;
            }
            lower = upper;
        }
        if (roundToNearestStep) return (*steps)[std::size_t(RoundToInt(stepped) == 0 ? from : to)];
        return Lerp((*steps)[std::size_t(from)], (*steps)[std::size_t(to)], stepped);
    }

    bool Strategy::operator==(const Strategy& o) const {
        if (!isValid && !o.isValid) return true;
        return isValid == o.isValid && availableSpace == o.availableSpace && itemSpacing == o.itemSpacing &&
               beforeContentPadding == o.beforeContentPadding && afterContentPadding == o.afterContentPadding &&
               ItemMainAxisSize() == o.ItemMainAxisSize() && m_StartShiftDistance == o.m_StartShiftDistance &&
               m_EndShiftDistance == o.m_EndShiftDistance && m_StartShiftPoints == o.m_StartShiftPoints &&
               m_EndShiftPoints == o.m_EndShiftPoints && defaultKeylines == o.defaultKeylines;
    }

    int SnapPositionOffset(const Strategy& s, int itemIndex, int itemCount) {
        if (!s.isValid) return 0;
        const float half = s.ItemMainAxisSize() / 2;
        int offset = RoundToInt(s.defaultKeylines.FirstFocal().unadjustedOffset - half);
        const int startLast = int(s.startKeylineSteps.size()) - 1;
        if (itemIndex <= startLast) {
            // Steps run from the default to the start state: the first item takes the last.
            const int step = std::clamp(startLast - itemIndex, 0, startLast);
            offset = RoundToInt(s.startKeylineSteps[std::size_t(step)].FirstFocal().unadjustedOffset - half);
        }
        const int endLast = int(s.endKeylineSteps.size()) - 1;
        const int lastItem = itemCount - 1;
        if (itemIndex >= lastItem - endLast && itemCount > s.defaultKeylines.FocalCount()) {
            const int step = std::clamp(endLast - (lastItem - itemIndex), 0, endLast);
            offset = RoundToInt(s.endKeylineSteps[std::size_t(step)].LastFocal().unadjustedOffset - half);
        }
        return offset;
    }

}
