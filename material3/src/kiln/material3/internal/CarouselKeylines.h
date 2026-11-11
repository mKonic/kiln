#pragma once

// The carousel's keyline model (carousel/KeylineList.kt, Keylines.kt, Arrangement.kt,
// Strategy.kt, KeylineSnapPosition.kt). All sizes and offsets are pixels along the scroll axis.

#include "kiln/material3/Carousel.h"

#include <cstdint>

#include <optional>
#include <vector>

namespace kiln::material3::internal::carousel {

    // MinSmallItemSize, MaxSmallItemSize, AnchorSize (dp) and MediumLargeItemDiffThreshold.
    inline constexpr float MinSmallItemSizeDp = 40, MaxSmallItemSizeDp = 56, AnchorSizeDp = 10;
    inline constexpr float MediumLargeItemDiffThreshold = 0.85f;

    // A fixed position along the scroll axis: an item whose centre is at `offset` is `size` large.
    // `unadjustedOffset` is where that centre sits when all items lie end to end at full size.
    struct Keyline {
        float size = 0, offset = 0, unadjustedOffset = 0;
        bool isFocal = false, isAnchor = false, isPivot = false;
        float cutoff = 0;   // how far the item bleeds past the container's edge
        bool operator==(const Keyline&) const = default;
    };

    enum class CarouselAlignment { Start, Center, End };

    class KeylineList {
    public:
        KeylineList() = default;
        explicit KeylineList(std::vector<Keyline> keylines);

        const std::vector<Keyline>& Items() const { return m_Keylines; }
        bool Empty() const { return m_Keylines.empty(); }
        int Size() const { return int(m_Keylines.size()); }
        int LastIndex() const { return Size() - 1; }
        const Keyline& operator[](int i) const { return m_Keylines[std::size_t(i)]; }
        const Keyline& First() const { return m_Keylines.front(); }
        const Keyline& Last() const { return m_Keylines.back(); }

        float minSize = 0, maxSize = 0;
        int pivotIndex = -1, firstNonAnchorIndex = -1, lastNonAnchorIndex = -1, firstFocalIndex = -1, lastFocalIndex = -1;
        const Keyline& Pivot() const { return (*this)[pivotIndex]; }
        const Keyline& FirstNonAnchor() const { return (*this)[firstNonAnchorIndex]; }
        const Keyline& LastNonAnchor() const { return (*this)[lastNonAnchorIndex]; }
        const Keyline& FirstFocal() const { return (*this)[firstFocalIndex]; }
        const Keyline& LastFocal() const { return (*this)[lastFocalIndex]; }
        int FocalCount() const { return lastFocalIndex - firstFocalIndex + 1; }

        bool IsFirstFocalItemAtStartOfContainer() const;
        bool IsLastFocalItemAtEndOfContainer(float carouselMainAxisSize) const;
        int FirstIndexAfterFocalRangeWithSize(float size) const;
        int LastIndexBeforeFocalRangeWithSize(float size) const;
        // The last keyline before `unadjustedOffset` (else the first); the first at or after it
        // (else the last).
        const Keyline& KeylineBefore(float unadjustedOffset) const;
        const Keyline& KeylineAfter(float unadjustedOffset) const;

        bool operator==(const KeylineList& o) const { return m_Keylines == o.m_Keylines; }

    private:
        std::vector<Keyline> m_Keylines;
    };

    // keylineListOf { add(size, isAnchor) }: the sizes in order, with offsets from the alignment
    // (the first focal keyline is the pivot) or from an explicit pivot.
    struct KeylineSpec { float size; bool isAnchor = false; };
    KeylineList KeylineListOf(float carouselMainAxisSize, float itemSpacing, CarouselAlignment alignment, const std::vector<KeylineSpec>& keylines);
    KeylineList KeylineListOf(float carouselMainAxisSize, float itemSpacing, int pivotIndex, float pivotOffset, const std::vector<KeylineSpec>& keylines);

    Keyline Lerp(const Keyline& start, const Keyline& end, float fraction);
    KeylineList Lerp(const KeylineList& from, const KeylineList& to, float fraction);
    // lerp(outputMin, outputMax, inputMin, inputMax, value), clamped to the input range.
    float Lerp(float outputMin, float outputMax, float inputMin, float inputMax, float value);

    // Arrangement: counts and sizes of large, medium and small items fitted to a space.
    struct Arrangement {
        int priority = 0;
        float smallSize = 0;
        int smallCount = 0;
        float mediumSize = 0;
        int mediumCount = 0;
        float largeSize = 0;
        int largeCount = 0;

        int ItemCount() const { return largeCount + mediumCount + smallCount; }
        bool IsValid() const;
        float Cost(float targetLargeSize) const;

        static std::optional<Arrangement> FindLowestCostArrangement(float availableSpace, float itemSpacing, float targetSmallSize,
                                                                    float minSmallSize, float maxSmallSize, const std::vector<int>& smallCounts,
                                                                    float targetMediumSize, const std::vector<int>& mediumCounts,
                                                                    float targetLargeSize, const std::vector<int>& largeCounts);
    };

    // The three strategies' default keylines. density: px per dp.
    KeylineList MultiBrowseKeylineList(float density, float carouselMainAxisSize, float preferredItemSize, float itemSpacing,
                                       int itemCount, float minSmallItemSize, float maxSmallItemSize);
    KeylineList UncontainedKeylineList(float density, float carouselMainAxisSize, float itemSize, float itemSpacing);
    KeylineList HeroKeylineList(float density, float carouselMainAxisSize, std::optional<float> preferredItemSize, float itemSpacing,
                                int itemCount, bool isCentered, float minSmallItemSize, float maxSmallItemSize);
    KeylineList CreateLeftAlignedKeylineList(float carouselMainAxisSize, float itemSpacing, float leftAnchorSize, float rightAnchorSize,
                                             const Arrangement& arrangement);
    KeylineList CreateCenterAlignedKeylineList(float carouselMainAxisSize, float itemSpacing, float leftAnchorSize, float rightAnchorSize,
                                               const Arrangement& arrangement);

    // Strategy: the default keylines plus the steps that shift the focal range to the container's
    // start and end, and the keylines for a scroll offset.
    class Strategy {
    public:
        Strategy() = default;   // Strategy.Empty
        Strategy(KeylineList defaultKeylines, float availableSpace, float itemSpacing, float beforeContentPadding, float afterContentPadding);

        KeylineList defaultKeylines;
        std::vector<KeylineList> startKeylineSteps, endKeylineSteps;
        float availableSpace = 0, itemSpacing = 0, beforeContentPadding = 0, afterContentPadding = 0;
        float minItemSize = 0, maxItemSize = 0;
        bool isValid = false;

        float ItemMainAxisSize() const { return defaultKeylines.Empty() ? 0.0f : defaultKeylines.FirstFocal().size; }
        KeylineList KeylineListForScrollOffset(float scrollOffset, float maxScrollOffset, bool roundToNearestStep = false) const;

        bool operator==(const Strategy& o) const;

    private:
        float m_StartShiftDistance = 0, m_EndShiftDistance = 0;
        std::vector<float> m_StartShiftPoints, m_EndShiftPoints;
    };

    // getSnapPositionOffset: the scroll offset from the container start at which item
    // `itemIndex` snaps.
    int SnapPositionOffset(const Strategy& strategy, int itemIndex, int itemCount);

    // calculateMaxScrollOffset: the items end to end less the available space.
    float MaxScrollOffset(const foundation::PagerState& state, const Strategy& strategy);

    // Carousel(orientation, keylineList, ...): what the public carousels build on.
    // maxNonFocalVisibleItemCount: pages composed beyond the viewport on each side.
    void Carousel(CarouselState& state, bool vertical, std::function<KeylineList(float availableSpace, float itemSpacing)> keylineList,
                  const CarouselOptions& options, foundation::FlingBehavior defaultFling, int maxNonFocalVisibleItemCount,
                  const ui::Modifier& modifier, CarouselContent content);

}
