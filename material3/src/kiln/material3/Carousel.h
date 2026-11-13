#pragma once

// Carousel (carousel/*.kt): a pager of same-size items that are masked and moved as they scroll
// so they grow and shrink between keylines: multi-browse (large, medium and small items),
// uncontained (full-size items with one cut off at the end) and centred hero (a large item
// between small ones). carouselParallaxScrollEffect gives lazy lists and grids of items of any
// size the look of an uncontained multi-aspect carousel.

#include "kiln/foundation/Border.h"
#include <kiln/Function.h>
#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Pager.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    class CarouselState {
    public:
        // currentItemOffsetFraction: -0.5..0.5, how far from its snapped position the current
        // item starts.
        CarouselState(int currentItem, float currentItemOffsetFraction, kiln::Function<int()> itemCount);
        CarouselState(const CarouselState&) = delete;
        CarouselState& operator=(const CarouselState&) = delete;

        // The item closest to its snapped position (observable).
        int CurrentItem() const { return m_Pager.CurrentPage(); }
        bool IsScrollInProgress() const { return m_Pager.IsScrollInProgress(); }
        void ScrollToItem(int item) { m_Pager.ScrollToPage(item, 0); }
        // Animates to the item's own snapped position (items near the ends snap elsewhere),
        // jumping close first when it is far.
        rt::Task<> AnimateScrollToItem(int item, anim::SpecPtr animationSpec = anim::Spring());

        foundation::PagerState& PagerState() { return m_Pager; }
        const foundation::PagerState& PagerState() const { return m_Pager; }

    private:
        foundation::PagerState m_Pager;
    };

    // rememberCarouselState: the current item and its offset survive re-creation; the count is
    // read from itemCount (refreshed every composition).
    std::shared_ptr<CarouselState> RememberCarouselState(int initialItem, kiln::Function<int()> itemCount, rt::Key key = {});
    inline std::shared_ptr<CarouselState> RememberCarouselState(kiln::Function<int()> itemCount, rt::Key key = {}) {
        return RememberCarouselState(0, std::move(itemCount), key);
    }

    // CarouselItemDrawInfo: an item's current size along the scroll axis, the smallest and
    // largest it gets, and the rect (in the item's own bounds) it is masked to. Updated on every
    // scroll: read it while drawing or in a layer, not in composition.
    class CarouselItemDrawInfo {
    public:
        float Size() const { return m_Size.Get(); }
        float MinSize() const { return m_MinSize.Get(); }
        float MaxSize() const { return m_MaxSize.Get(); }
        ui::Rect MaskRect() const { return { m_MaskLeft.Get(), m_MaskTop.Get(), m_MaskRight.Get(), m_MaskBottom.Get() }; }

        // Internal.
        void Set(float size, float minSize, float maxSize, ui::Rect mask);

    private:
        rt::State<float> m_Size{ 0 }, m_MinSize{ 0 }, m_MaxSize{ 0 };
        rt::State<float> m_MaskLeft{ 0 }, m_MaskTop{ 0 }, m_MaskRight{ 0 }, m_MaskBottom{ 0 };
    };

    class CarouselItemScope {
    public:
        explicit CarouselItemScope(std::shared_ptr<CarouselItemDrawInfo> info) : m_Info(std::move(info)) {}
        const CarouselItemDrawInfo& DrawInfo() const { return *m_Info; }
        // Clips to `shape` fitted to the mask (the item's cross-axis size and its mask along the
        // scroll axis); maskBorder draws a border on that same shape.
        ui::Modifier MaskClip(ui::Modifier m, foundation::ShapePtr shape) const;
        ui::Modifier MaskBorder(ui::Modifier m, const foundation::BorderStroke& border, foundation::ShapePtr shape) const;
        // `shape` laid out in the intersection of the mask and the bounds it is drawn at.
        foundation::ShapePtr MaskShape(foundation::ShapePtr shape) const;

    private:
        std::shared_ptr<CarouselItemDrawInfo> m_Info;
    };

    using CarouselContent = kiln::Function<void(const CarouselItemScope&, int itemIndex)>;

    struct CarouselOptions {
        ui::Dp itemSpacing{ 0 };
        foundation::FlingBehavior flingBehavior;   // null: the carousel's default
        bool userScrollEnabled = true;
        // Main-axis padding is applied by the keylines (inside the mask); cross-axis by the pager.
        foundation::PaddingValues contentPadding;
    };

    namespace CarouselDefaults {
        inline constexpr ui::Dp MinSmallItemSize{ 40 }, MaxSmallItemSize{ 56 }, ItemSpacing{ 0 };
        // One item per fling (PagerSnapDistance.atMost(1)), snapping with a StiffnessMediumLow
        // spring.
        foundation::FlingBehavior singleAdvanceFlingBehavior(CarouselState& state);
        // As far as the fling's decay goes, then a snap to the item there.
        foundation::FlingBehavior multiBrowseFlingBehavior(CarouselState& state);
        // A decay that does not snap.
        foundation::FlingBehavior noSnapFlingBehavior();
    }

    // Large items aim for preferredItemWidth; medium and small ones fill the rest (small between
    // minSmallItemWidth and maxSmallItemWidth). Default fling: singleAdvanceFlingBehavior.
    void HorizontalMultiBrowseCarousel(CarouselState& state, ui::Dp preferredItemWidth, const ui::Modifier& modifier,
                                       CarouselContent content, CarouselOptions options = {},
                                       ui::Dp minSmallItemWidth = CarouselDefaults::MinSmallItemSize,
                                       ui::Dp maxSmallItemWidth = CarouselDefaults::MaxSmallItemSize);
    // Items of itemWidth, with one cut off at the end. Default fling: noSnapFlingBehavior.
    void HorizontalUncontainedCarousel(CarouselState& state, ui::Dp itemWidth, const ui::Modifier& modifier,
                                       CarouselContent content, CarouselOptions options = {});
    // A large item centred between two small ones (start- and end-aligned at the list's ends).
    // preferredItemWidth unset: the large item takes the viewport less the small items.
    void HorizontalCenteredHeroCarousel(CarouselState& state, const ui::Modifier& modifier, CarouselContent content,
                                        CarouselOptions options = {}, std::optional<ui::Dp> preferredItemWidth = std::nullopt,
                                        ui::Dp minSmallItemWidth = CarouselDefaults::MinSmallItemSize,
                                        ui::Dp maxSmallItemWidth = CarouselDefaults::MaxSmallItemSize);

    // --- carouselParallaxScrollEffect ------------------------------------------------------------

    struct CarouselParallaxScrollEffectItemInfo {
        float maskStart = 0, maskEnd = 0;   // from the start of the item's bounds (px)
        float parallax = 0;                 // how far the content moves along the scroll axis
        float maskProgress = 0;             // 0 unmasked .. 1 masked the most
        float Size() const { return std::max(0.0f, maskEnd - maskStart); }
        bool operator==(const CarouselParallaxScrollEffectItemInfo&) const = default;
    };

    class CarouselParallaxScrollEffectState {
    public:
        virtual ~CarouselParallaxScrollEffectState() = default;
        // Read in a layer, layout or draw, not in composition.
        virtual CarouselParallaxScrollEffectItemInfo CalculateItemInfo(int index) const = 0;
        virtual foundation::Orientation Orientation() const = 0;
    };
    std::shared_ptr<CarouselParallaxScrollEffectState> MakeCarouselParallaxScrollEffectState(foundation::LazyListState& state);
    std::shared_ptr<CarouselParallaxScrollEffectState> MakeCarouselParallaxScrollEffectState(foundation::LazyGridState& state);

    // The item at `index` is clipped to `shape` over its unmasked part and its content shifted
    // as it enters and leaves the viewport.
    ui::Modifier CarouselParallaxScrollEffect(ui::Modifier m, int index, std::shared_ptr<CarouselParallaxScrollEffectState> state,
                                              foundation::ShapePtr shape, std::optional<foundation::BorderStroke> border = std::nullopt);

    namespace internal {
        // The pure item info (calculateItemInfo), for tests.
        CarouselParallaxScrollEffectItemInfo CalculateParallaxItemInfo(bool isItemVisible, float itemMainAxisSize, float itemCrossAxisSize,
                                                                       float itemOffset, float viewportStartOffset, float viewportEndOffset);
    }

}
