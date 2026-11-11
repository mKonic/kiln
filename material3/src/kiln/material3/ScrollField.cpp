#include "kiln/material3/ScrollField.h"

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/MotionScheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Shapes.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/DatePicker.h"
#include "kiln/ui/Configuration.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using ui::Color;
    using ui::Dp;

    namespace {
        constexpr int InfinitePageCount = 100'000;
    }

    ScrollFieldState::ScrollFieldState(PagerState& pager, int itemCount) : m_Pager(&pager), m_ItemCount(std::max(1, itemCount)) {}

    int ScrollFieldState::SelectedOption() const { return m_Pager->CurrentPage() % m_ItemCount; }
    int ScrollFieldState::TargetOption() const { return m_Pager->TargetPage() % m_ItemCount; }

    int ScrollFieldState::TargetPage(int option) const {
        const int page = m_Pager->CurrentPage();
        return page + (std::clamp(option, 0, m_ItemCount - 1) - page % m_ItemCount);
    }

    void ScrollFieldState::SetSelectedOption(int option) { m_Pager->RequestScrollToPage(TargetPage(option)); }

    rt::Task<> ScrollFieldState::ScrollToOption(int option) {
        m_Pager->ScrollToPage(TargetPage(option));
        co_return;
    }

    rt::Task<> ScrollFieldState::AnimateScrollToOption(int option) { co_await m_Pager->AnimateScrollToPage(TargetPage(option)); }

    std::shared_ptr<ScrollFieldState> RememberScrollFieldState(int itemCount, int index) {
        auto& c = *rt::Composition::Current();
        const int count = std::max(1, itemCount);
        // Around the middle of a long run of pages, on a page of `index`.
        const int initial = InfinitePageCount / 2 - (InfinitePageCount / 2 % count) + std::clamp(index, 0, count - 1);
        auto& pager = RememberPagerState(initial, 0, [] { return InfinitePageCount; });
        return c.Remember<std::shared_ptr<ScrollFieldState>>([&] { return std::make_shared<ScrollFieldState>(pager, count); },
                                                             rt::Keys(reinterpret_cast<std::uintptr_t>(&pager), count));
    }

    namespace ScrollFieldDefaults {
        ShapePtr shape() { return ShapeDefaults::Large(); }

        ScrollFieldColors colors() {
            const auto& cs = MaterialTheme::colorScheme();
            return { cs.surfaceContainerLowest, cs.outline, cs.onSurface, cs.surfaceContainerLowest.WithAlpha(DisabledAlpha),
                     cs.onSurface.WithAlpha(DisabledAlpha), cs.onSurface.WithAlpha(DisabledAlpha) };
        }

        void Item(int index, bool selected, bool enabled, std::optional<ScrollFieldColors> colors) {
            const ScrollFieldColors c = colors.value_or(ScrollFieldDefaults::colors());
            const Color target = enabled ? (selected ? c.selectedContentColor : c.contentColor)
                                         : (selected ? c.disabledSelectedContentColor : c.disabledContentColor);
            const auto spec = MaterialTheme::motionScheme()->FastSpatialSpec();
            const float fraction = AnimateFloatAsState(selected ? 1.0f : 0.0f, spec);
            const Color color = AnimateColorAsState(target, spec);
            const auto& t = MaterialTheme::typography();
            TextOptions o;
            o.style = text::Lerp(t.displayMedium, t.displayLargeEmphasized, std::clamp(fraction, 0.0f, 1.0f));
            o.color = color;
            Text(internal::ToLocalString(index, ui::PlatformLocale(), 2), {}, o);
        }
    }

    void ScrollField(ScrollFieldState& state, std::optional<std::string> contentDescription, const ui::Modifier& modifier,
                     const ScrollFieldOptions& options) {
        auto& c = *rt::Composition::Current();
        const ScrollFieldColors colors = options.colors.value_or(ScrollFieldDefaults::colors());
        auto source = options.interactionSource ? options.interactionSource : RememberInteractionSource();
        auto scope = internal::RememberCoroutineScope();
        const bool enabled = options.enabled;
        const std::string locale = ui::PlatformLocale();
        auto describe = options.fieldAccessibilityDescription ? options.fieldAccessibilityDescription
                                                              : std::function<std::string(int)>([locale](int i) { return internal::ToLocalString(i, locale); });
        auto field = options.field ? options.field : std::function<void(int, bool, bool)>([colors](int i, bool selected, bool enabled) {
            ScrollFieldDefaults::Item(i, selected, enabled, colors);
        });
        ScrollFieldState* st = &state;
        PagerState* pager = &state.Pager();
        const ShapePtr shape = ScrollFieldDefaults::shape();

        ui::Modifier m = Clip(modifier, shape);
        RippleOptions ro;
        ro.focusRingShape = shape;
        m = Ripple(ro)->Create(std::move(m), source);
        m = Background(std::move(m), enabled ? colors.containerColor : colors.disabledContainerColor, shape);
        // Up and down step through the options.
        m = OnKeyEvent(std::move(m), [pager, scope](const ui::KeyEvent& e) {
            if (e.type != ui::KeyEventType::KeyDown) return false;
            int direction = 0;
            if (e.keyCode == ui::keys::DpadDown) direction = 1;
            else if (e.keyCode == ui::keys::DpadUp) direction = -1;
            else return false;
            scope->Launch(pager->AnimateScrollToPage(pager->TargetPage() + direction));
            return true;
        });
        m = Focusable(std::move(m), enabled ? ui::Focusability::Always : ui::Focusability::Never, {}, source);
        // One node: its description, the option as its state, and a progress to set.
        m = ClearAndSetSemantics(std::move(m), [contentDescription, describe, st, pager, scope](ui::SemanticsConfiguration& sc) {
            if (contentDescription) sc.contentDescription = { *contentDescription };
            sc.stateDescription = describe(st->TargetOption());
            sc.progressBarRangeInfo = ui::ProgressBarRangeInfo{ float(pager->TargetPage()), 0.0f, float(InfinitePageCount - 1), InfinitePageCount - 2 };
            sc.setProgress = [pager, scope](float target) {
                const int page = int(std::lround(target));
                if (page == pager->TargetPage()) return false;
                scope->Launch(pager->AnimateScrollToPage(page));
                return true;
            };
        });

        PagerOptions po;
        po.pageSize = PageSize::Fixed(options.itemHeight);
        po.horizontalAlignment = ui::AlignmentH::CenterHorizontally();
        po.snapPosition = SnapPosition::Center();
        po.userScrollEnabled = enabled;
        VerticalPager(m, *pager, [=](int page) {
            const int index = page % st->ItemCount();
            const bool selected = pager->CurrentPage() == page;
            ui::Modifier item = FocusPropertiesMod(FillMax({}, Axis::Vertical), [](ui::FocusProperties& p) { p.canFocus = false; });
            ClickableOptions co;
            co.enabled = enabled;
            co.indication = nullptr;
            item = Clickable(std::move(item), [scope, st, index] { scope->Launch(st->AnimateScrollToOption(index)); }, co);
            item = Semantics(std::move(item), [selected](ui::SemanticsConfiguration& sc) { sc.selected = selected; });
            // Last: taller content (the larger selected text) stays centred over its neighbours
            // instead of being clipped, while the click area stays one item tall.
            item = WrapContent(std::move(item), Axis::Vertical, ui::Alignment::Center(), true);
            Box(item, ui::Alignment::Center(), false, [field, index, selected, enabled] { field(index, selected, enabled); });
        }, po);
    }

}
