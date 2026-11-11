#include "kiln/material3/Carousel.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Snapping.h"
#include "kiln/graphics/Shape.h"
#include "kiln/material3/internal/CarouselKeylines.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    namespace kc = internal::carousel;
    using ui::Dp;

    // --- state ----------------------------------------------------------------------------------

    CarouselState::CarouselState(int currentItem, float fraction, std::function<int()> itemCount)
        : m_Pager(currentItem, fraction, std::move(itemCount)) {}

    rt::Task<> CarouselState::AnimateScrollToItem(int item, anim::SpecPtr spec) {
        // The pager's animateScrollToPage already lands on the target's own snap position.
        co_await m_Pager.AnimateScrollToPage(item, 0, std::move(spec));
    }

    std::shared_ptr<CarouselState> RememberCarouselState(int initialItem, std::function<int()> itemCount) {
        // [currentItem, currentItemOffsetFraction, itemCount] (CarouselState.Saver).
        auto saver = rt::ListSaver<std::shared_ptr<CarouselState>>(
            [](const std::shared_ptr<CarouselState>& s) {
                const auto& p = s->PagerState();
                return rt::SavedValue::List{ p.CurrentPage(), double(std::clamp(p.CurrentPageOffsetFraction(), -0.5f, 0.5f)), p.PageCount() };
            },
            [itemCount](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<CarouselState>> {
                if (l.size() < 3) return std::nullopt;
                return std::make_shared<CarouselState>(int(rt::SavedNumber(l[0]).value_or(0)), float(rt::SavedNumber(l[1]).value_or(0)), itemCount);
            });
        auto& s = rt::RememberSaveable<std::shared_ptr<CarouselState>>(
            [&] { return std::make_shared<CarouselState>(initialItem, 0.0f, itemCount); }, saver);
        s->PagerState().SetPageCountProvider(std::move(itemCount));
        return s;
    }

    void CarouselItemDrawInfo::Set(float size, float minSize, float maxSize, ui::Rect mask) {
        const auto set = [](rt::State<float>& st, float v) { if (st.Peek() != v) st.Set(v); };
        set(m_Size, size);
        set(m_MinSize, minSize);
        set(m_MaxSize, maxSize);
        set(m_MaskLeft, mask.left);
        set(m_MaskTop, mask.top);
        set(m_MaskRight, mask.right);
        set(m_MaskBottom, mask.bottom);
    }

    // --- item scope -----------------------------------------------------------------------------

    namespace {
        gfx::Outline Translated(gfx::Outline o, ui::Offset by) {
            if (auto* r = std::get_if<ui::Rect>(&o.value)) *r = r->Translate(by);
            else if (auto* rr = std::get_if<gfx::RoundRect>(&o.value)) rr->rect = rr->rect.Translate(by);
            else std::get<gfx::Path>(o.value).Translate(by);
            return o;
        }

        // rememberMaskShape: `shape` in the intersection of the mask and the drawn bounds.
        class ItemMaskShape final : public gfx::Shape {
        public:
            ItemMaskShape(std::shared_ptr<CarouselItemDrawInfo> info, ShapePtr shape) : m_Info(std::move(info)), m_Shape(std::move(shape)) {}
            gfx::Outline CreateOutline(ui::Size size, ui::LayoutDirection dir, const ui::Density& d) const override {
                const ui::Rect rect = m_Info->MaskRect().Intersect({ 0, 0, size.width, size.height });
                const ui::Size inner{ std::max(0.0f, rect.Width()), std::max(0.0f, rect.Height()) };
                return Translated(m_Shape->CreateOutline(inner, dir, d), { rect.left, rect.top });
            }
        private:
            std::shared_ptr<CarouselItemDrawInfo> m_Info;
            ShapePtr m_Shape;
        };

        class RectOutline final : public gfx::Shape {
        public:
            explicit RectOutline(ui::Rect r) : m_Rect(r) {}
            gfx::Outline CreateOutline(ui::Size, ui::LayoutDirection, const ui::Density&) const override { return { m_Rect }; }
        private:
            ui::Rect m_Rect;
        };
    }

    ShapePtr CarouselItemScope::MaskShape(ShapePtr shape) const { return std::make_shared<ItemMaskShape>(m_Info, std::move(shape)); }

    ui::Modifier CarouselItemScope::MaskClip(ui::Modifier m, ShapePtr shape) const {
        auto info = m_Info;
        auto mask = MaskShape(std::move(shape));
        return GraphicsLayer(std::move(m), [info, mask](GraphicsLayerParams& p) {
            (void)info->MaskRect();   // follow the mask
            p.clip = true;
            p.clipShape = mask;
        });
    }

    ui::Modifier CarouselItemScope::MaskBorder(ui::Modifier m, const BorderStroke& border, ShapePtr shape) const {
        return Border(std::move(m), border, MaskShape(std::move(shape)));
    }

    // --- the carousel ---------------------------------------------------------------------------

    namespace {
        // CarouselPageSize: the strategy for the latest available space.
        struct PageSizeHolder {
            std::function<kc::KeylineList(float availableSpace, float itemSpacing)> keylineList;
            float before = 0, after = 0;
            rt::State<kc::Strategy> strategy{ kc::Strategy{} };
        };

        float CurrentScrollOffset(const PagerState& ps, const kc::Strategy& s) {
            const float step = s.ItemMainAxisSize() + s.itemSpacing;
            const int page = ps.CurrentPage();
            return float(page) * step + ps.CurrentPageOffsetFraction() * step - float(kc::SnapPositionOffset(s, page, ps.PageCount()));
        }


        float Progress(const kc::Keyline& before, const kc::Keyline& after, float unadjustedOffset) {
            if (before == after) return 1;
            return (unadjustedOffset - before.unadjustedOffset) / (after.unadjustedOffset - before.unadjustedOffset);
        }

        // Modifier.carouselItem: the strategy's item size along the axis, then masked and moved
        // to its keyline as it scrolls; the current item draws on top.
        ui::Modifier CarouselItem(ui::Modifier m, int index, PagerState* ps, std::shared_ptr<PageSizeHolder> h,
                                  std::shared_ptr<CarouselItemDrawInfo> info, bool vertical, bool rtl) {
            m = LayoutMod(std::move(m), [index, ps, h, vertical](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                const kc::Strategy& st = h->strategy.Get();
                if (!st.isValid) return s.Layout(0, 0);   // nothing to show without a strategy
                const int main = int(std::lround(st.ItemMainAxisSize()));
                ui::Constraints ic = c;
                if (vertical) ic.minHeight = ic.maxHeight = main;
                else ic.minWidth = ic.maxWidth = main;
                ui::Placeable& p = measurable.Measure(ic);
                const float z = 1.0f / float(1 + std::abs(index - ps->CurrentPage()));
                return s.Layout(p.Width(), p.Height(), [&p, z](ui::PlacementScope& place) { place.Place(p, 0, 0, z); });
            });
            return GraphicsLayer(std::move(m), [index, ps, h, info, vertical, rtl](GraphicsLayerParams& p) {
                const kc::Strategy& st = h->strategy.Get();
                if (!st.isValid) return;
                const float width = p.size.width, height = p.size.height;
                const float scroll = CurrentScrollOffset(*ps, st);
                const kc::KeylineList keylines = st.KeylineListForScrollOffset(scroll, kc::MaxScrollOffset(*ps, st));
                if (keylines.Empty()) return;
                const float itemSize = st.ItemMainAxisSize();
                const float center = float(index) * (itemSize + st.itemSpacing) + itemSize / 2 - scroll;
                const kc::Keyline& before = keylines.KeylineBefore(center);
                const kc::Keyline& after = keylines.KeylineAfter(center);
                const kc::Keyline k = kc::Lerp(before, after, Progress(before, after, center));
                const bool outOfBounds = before == after;

                const float cx = vertical ? width / 2 : itemSize / 2, cy = vertical ? itemSize / 2 : height / 2;
                const float hw = vertical ? width / 2 : k.size / 2, hh = vertical ? k.size / 2 : height / 2;
                const ui::Rect mask{ cx - hw, cy - hh, cx + hw, cy + hh };
                info->Set(k.size, st.minItemSize, st.maxItemSize, mask);

                // Pin the masked items' edges together; past the outer keylines keep moving by
                // the masked size.
                float translation = k.offset - center;
                if (outOfBounds) translation += (center - k.unadjustedOffset) / k.size;
                p.clip = mask.left != 0 || mask.top != 0 || mask.right != width || mask.bottom != height;
                p.clipShape = std::make_shared<RectOutline>(mask);
                if (vertical) p.translationY = translation;
                else p.translationX = rtl ? -translation : translation;
            });
        }

    }

    float kc::MaxScrollOffset(const PagerState& ps, const kc::Strategy& s) {
        const float count = float(ps.PageCount());
        return std::max(0.0f, s.ItemMainAxisSize() * count + s.itemSpacing * (count - 1) - s.availableSpace);
    }

    void kc::Carousel(CarouselState& state, bool vertical, std::function<kc::KeylineList(float, float)> keylineList,
                      const CarouselOptions& options, FlingBehavior defaultFling, int maxNonFocalVisibleItemCount,
                      const ui::Modifier& modifier, CarouselContent content) {
            auto& c = *rt::Composition::Current();
            const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
            const auto density = c.Read(ui::LocalDensity);
            const PaddingValues& pad = options.contentPadding;
            // calculateStart/EndPadding: logical padding follows the direction; absolute does not.
            const Dp start = pad.rtlAware || !rtl ? pad.start : pad.end, end = pad.rtlAware || !rtl ? pad.end : pad.start;
            auto& h = c.Remember<std::shared_ptr<PageSizeHolder>>([] { return std::make_shared<PageSizeHolder>(); });
            h->keylineList = std::move(keylineList);
            h->before = density.ToPx(vertical ? pad.top : start);
            h->after = density.ToPx(vertical ? pad.bottom : end);

            PagerOptions po;
            // Main-axis padding is the strategy's; only the cross axis goes to the pager.
            po.contentPadding = vertical ? PaddingValues{ pad.start, Dp{ 0 }, pad.end, Dp{ 0 }, pad.rtlAware }
                                         : PaddingValues{ Dp{ 0 }, pad.top, Dp{ 0 }, pad.bottom, pad.rtlAware };
            std::shared_ptr<PageSizeHolder> holder = h;
            po.pageSize = PageSize::Custom([holder](const ui::Density&, int available, int spacing) {
                kc::Strategy s(holder->keylineList(float(available), float(spacing)), float(available), float(spacing), holder->before, holder->after);
                if (!(holder->strategy.Peek() == s)) holder->strategy.Set(s);
                return s.isValid ? int(std::lround(s.ItemMainAxisSize())) : available;
            });
            po.pageSpacing = options.itemSpacing;
            po.beyondViewportPageCount = maxNonFocalVisibleItemCount;
            po.snapPosition = SnapPosition::Custom([holder](int, int, int, int, int index, int count) {
                return kc::SnapPositionOffset(holder->strategy.Peek(), index, count);
            });
            po.flingBehavior = options.flingBehavior ? options.flingBehavior : std::move(defaultFling);
            po.userScrollEnabled = options.userScrollEnabled;

            ui::Modifier m = Semantics(modifier, [](SemanticsConfiguration& sc) { sc.role = ui::Role::Carousel; });
            PagerState* ps = &state.PagerState();
            auto page = [ps, holder, content, vertical, rtl](int index) {
                auto& comp = *rt::Composition::Current();
                auto info = comp.Remember<std::shared_ptr<CarouselItemDrawInfo>>([] { return std::make_shared<CarouselItemDrawInfo>(); });
                Box(CarouselItem({}, index, ps, holder, info, vertical, rtl), ui::Alignment::TopStart(), false, [content, info, index] {
                    const CarouselItemScope scope(info);
                    content(scope, index);
                });
            };
            if (vertical) VerticalPager(m, *ps, std::move(page), std::move(po));
            else HorizontalPager(m, *ps, std::move(page), std::move(po));
    }

    namespace {

        float Px(Dp d) { return rt::Composition::Current()->Read(ui::LocalDensity).ToPx(d); }
        float DensityScale() { return rt::Composition::Current()->Read(ui::LocalDensity).density; }
    }

    namespace CarouselDefaults {
        FlingBehavior singleAdvanceFlingBehavior(CarouselState& state) {
            return PagerFlingBehavior(state.PagerState(), PagerSnapDistance::AtMost(1));
        }

        FlingBehavior multiBrowseFlingBehavior(CarouselState& state) {
            // Whatever page the decay suggests.
            return PagerFlingBehavior(state.PagerState(), PagerSnapDistance{ [](int, int suggested, float, int, int) { return suggested; } });
        }

        FlingBehavior noSnapFlingBehavior() {
            struct NoSnap final : SnapLayoutInfoProvider {
                float CalculateSnapOffset(float) override { return 0; }
            };
            const float density = DensityScale();
            return SnapFlingBehavior(std::make_shared<NoSnap>(), [density] { return std::make_shared<anim::SplineDecay>(density); });
        }
    }

    void HorizontalMultiBrowseCarousel(CarouselState& state, Dp preferredItemWidth, const ui::Modifier& modifier, CarouselContent content,
                                       CarouselOptions options, Dp minSmallItemWidth, Dp maxSmallItemWidth) {
        const float density = DensityScale(), preferred = Px(preferredItemWidth), minSmall = Px(minSmallItemWidth), maxSmall = Px(maxSmallItemWidth);
        CarouselState* st = &state;
        // At most two medium and small items are visible besides the focal ones.
        kc::Carousel(state, false, [=](float available, float spacing) {
            return kc::MultiBrowseKeylineList(density, available, preferred, spacing, st->PagerState().PageCount(), minSmall, maxSmall);
        }, options, CarouselDefaults::singleAdvanceFlingBehavior(state), 2, modifier, std::move(content));
    }

    void HorizontalUncontainedCarousel(CarouselState& state, Dp itemWidth, const ui::Modifier& modifier, CarouselContent content,
                                       CarouselOptions options) {
        const float density = DensityScale(), width = Px(itemWidth);
        // Only the one cut-off item is masked.
        kc::Carousel(state, false, [=](float available, float spacing) {
            return kc::UncontainedKeylineList(density, available, width, spacing);
        }, options, CarouselDefaults::noSnapFlingBehavior(), 0, modifier, std::move(content));
    }

    void HorizontalCenteredHeroCarousel(CarouselState& state, const ui::Modifier& modifier, CarouselContent content, CarouselOptions options,
                                        std::optional<Dp> preferredItemWidth, Dp minSmallItemWidth, Dp maxSmallItemWidth) {
        const float density = DensityScale(), minSmall = Px(minSmallItemWidth), maxSmall = Px(maxSmallItemWidth);
        std::optional<float> preferred;
        if (preferredItemWidth && preferredItemWidth->Specified()) preferred = Px(*preferredItemWidth);
        CarouselState* st = &state;
        kc::Carousel(state, false, [=](float available, float spacing) {
            return kc::HeroKeylineList(density, available, preferred, spacing, st->PagerState().PageCount(), true, minSmall, maxSmall);
        }, options, CarouselDefaults::singleAdvanceFlingBehavior(state), 2, modifier, std::move(content));
    }

    // --- carouselParallaxScrollEffect ------------------------------------------------------------

    namespace {
        float MaskIntensity(float main, float cross) {
            // From the item's aspect ratio.
            const float ar = main / cross;
            if (ar > 16.0f / 9) return 0.5f;
            if (ar < 16.0f / 9 && ar > 1) return kc::Lerp(1.0f / 3, 0.5f, 1.0f, 16.0f / 9, ar);
            if (ar < 1 && ar > 9.0f / 16) return kc::Lerp(0.25f, 1.0f / 3, 9.0f / 16, 1.0f, ar);
            return 0.25f;
        }
        float LerpF(float a, float b, float t) { return a + (b - a) * t; }
    }

    namespace internal {
        CarouselParallaxScrollEffectItemInfo CalculateParallaxItemInfo(bool visible, float main, float cross, float offset, float viewportStart,
                                                                       float viewportEnd) {
            if (!visible || main == 0) return {};
            const float intensity = MaskIntensity(main, cross);
            const float distance = main * intensity;
            if (offset < viewportStart) {   // entering or leaving at the start
                const float progress = std::clamp((viewportStart - offset) / main, 0.0f, 1.0f);
                return { LerpF(0, main * (1 - intensity), progress), main, LerpF(0, distance, progress), progress };
            }
            if (offset > viewportEnd - main) {   // at the end
                const float progress = std::clamp(-(viewportEnd - offset - main) / main, 0.0f, 1.0f);
                return { 0, LerpF(main, main * intensity, progress), -LerpF(0, distance, progress), progress };
            }
            return { 0, main, 0, 0 };
        }
    }

    namespace {
        class ListParallaxState final : public CarouselParallaxScrollEffectState {
        public:
            explicit ListParallaxState(LazyListState& s) : m_State(&s) {}
            CarouselParallaxScrollEffectItemInfo CalculateItemInfo(int index) const override {
                const auto& info = m_State->LayoutInfo();
                const LazyListItemInfo* item = nullptr;
                for (const auto& i : info.visibleItemsInfo) if (i.index == index) { item = &i; break; }
                const bool horizontal = info.orientation == foundation::Orientation::Horizontal;
                const float cross = float(horizontal ? info.viewportSize.height : info.viewportSize.width);
                return internal::CalculateParallaxItemInfo(item != nullptr, item ? float(item->size) : 0, cross, item ? float(item->offset) : 0,
                                                           float(info.viewportStartOffset), float(info.viewportEndOffset));
            }
            foundation::Orientation Orientation() const override { return m_State->LayoutInfo().orientation; }
        private:
            LazyListState* m_State;
        };

        class GridParallaxState final : public CarouselParallaxScrollEffectState {
        public:
            explicit GridParallaxState(LazyGridState& s) : m_State(&s) {}
            CarouselParallaxScrollEffectItemInfo CalculateItemInfo(int index) const override {
                const auto& info = m_State->LayoutInfo();
                const LazyGridItemInfo* item = nullptr;
                for (const auto& i : info.visibleItemsInfo) if (i.index == index) { item = &i; break; }
                const bool horizontal = info.orientation == foundation::Orientation::Horizontal;
                if (!item) return internal::CalculateParallaxItemInfo(false, 0, 0, 0, float(info.viewportStartOffset), float(info.viewportEndOffset));
                const float main = float(horizontal ? item->size.width : item->size.height);
                const float cross = float(horizontal ? item->size.height : item->size.width);
                const float offset = float(horizontal ? item->offset.x : item->offset.y);
                return internal::CalculateParallaxItemInfo(true, main, cross, offset, float(info.viewportStartOffset), float(info.viewportEndOffset));
            }
            foundation::Orientation Orientation() const override { return m_State->LayoutInfo().orientation; }
        private:
            LazyGridState* m_State;
        };

        // MaskShape: `shape` over the unmasked part along the scroll axis.
        class ParallaxMaskShape final : public gfx::Shape {
        public:
            ParallaxMaskShape(int index, std::shared_ptr<CarouselParallaxScrollEffectState> state, ShapePtr shape)
                : m_Index(index), m_State(std::move(state)), m_Shape(std::move(shape)) {}
            gfx::Outline CreateOutline(ui::Size size, ui::LayoutDirection dir, const ui::Density& d) const override {
                const auto info = m_State->CalculateItemInfo(m_Index);
                ui::Rect rect{ 0, 0, size.width, size.height };
                if (info.Size() != 0) {
                    if (m_State->Orientation() == foundation::Orientation::Horizontal) rect = { info.maskStart, 0, info.maskEnd, size.height };
                    else rect = { 0, info.maskStart, size.width, info.maskEnd };
                }
                return Translated(m_Shape->CreateOutline({ rect.Width(), rect.Height() }, dir, d), { rect.left, rect.top });
            }
        private:
            int m_Index;
            std::shared_ptr<CarouselParallaxScrollEffectState> m_State;
            ShapePtr m_Shape;
        };
    }

    std::shared_ptr<CarouselParallaxScrollEffectState> MakeCarouselParallaxScrollEffectState(LazyListState& state) {
        return std::make_shared<ListParallaxState>(state);
    }
    std::shared_ptr<CarouselParallaxScrollEffectState> MakeCarouselParallaxScrollEffectState(LazyGridState& state) {
        return std::make_shared<GridParallaxState>(state);
    }

    ui::Modifier CarouselParallaxScrollEffect(ui::Modifier m, int index, std::shared_ptr<CarouselParallaxScrollEffectState> state, ShapePtr shape,
                                              std::optional<BorderStroke> border) {
        auto mask = std::make_shared<ParallaxMaskShape>(index, state, std::move(shape));
        if (border) m = Border(std::move(m), *border, mask);
        m = GraphicsLayer(std::move(m), [index, state, mask](GraphicsLayerParams& p) {
            (void)state->CalculateItemInfo(index);   // follow the scroll
            p.clip = true;
            p.clipShape = mask;
        });
        // The content shifts inside the mask (a draw translation: hit testing stays put).
        return DrawWithContent(std::move(m), [index, state](ui::ContentDrawScope& s) {
            const auto info = state->CalculateItemInfo(index);
            s.Save();
            if (state->Orientation() == foundation::Orientation::Horizontal) s.Translate({ info.parallax, 0 });
            else s.Translate({ 0, info.parallax });
            s.DrawContent();
            s.Restore();
        });
    }

}
