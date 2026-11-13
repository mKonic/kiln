#include "kiln/material3/Scaffold.h"
#include <kiln/Function.h>

#include "kiln/foundation/Subcompose.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/ui/Locals.h"

#include <algorithm>

namespace kiln::material3 {

    using namespace foundation;
    using ui::Dp;
    using ui::LayoutDirection;

    namespace {
        constexpr Dp FabSpacing{ 16 };
        // Drawing order, matching each slot's default elevation.
        constexpr float ContentZIndex = 0, TopBarZIndex = 1, SnackbarZIndex = 2, BottomBarZIndex = 3, FabZIndex = 4;
        enum Slot : int { TopBar = 1, MainContent, Snackbar, Fab, BottomBar };

        int MaxHeight(const std::vector<ui::Placeable*>& ps) {
            int h = 0;
            for (auto* p : ps) h = std::max(h, p->Height());
            return h;
        }
        int MaxWidth(const std::vector<ui::Placeable*>& ps) {
            int w = 0;
            for (auto* p : ps) w = std::max(w, p->Width());
            return w;
        }
        std::vector<ui::Placeable*> MeasureAll(const std::vector<ui::Measurable*>& ms, const ui::Constraints& c) {
            std::vector<ui::Placeable*> out;
            for (auto* m : ms) out.push_back(&m->Measure(c));
            return out;
        }

        void ScaffoldLayout(FabPosition fabPosition, kiln::Function<void()> topBar, kiln::Function<void(const PaddingValues&)> content,
                            kiln::Function<void()> snackbar, kiln::Function<void()> fab, WindowInsets contentWindowInsets,
                            kiln::Function<void()> bottomBar) {
            auto& c = *rt::Composition::Current();
            // The content padding, updated while measuring before the body is subcomposed: the
            // body reads it, so it recomposes when the padding changes and not on every measure.
            const auto padding = c.Remember<std::shared_ptr<rt::State<PaddingValues>>>([] {
                return std::make_shared<rt::State<PaddingValues>>(PaddingValues{});
            });
            const auto body = std::make_shared<const kiln::Function<void()>>([content, padding] { content(padding->Get()); });
            ui::Owner* owner = c.Read(ui::LocalOwner);
            SubcomposeLayout({}, [=](SubcomposeMeasureScope& s, const ui::Constraints& constraints) {
                const int layoutWidth = constraints.maxWidth, layoutHeight = constraints.maxHeight;
                const ui::Constraints loose = constraints.CopyMaxDimensions();
                const LayoutDirection dir = s.Direction();
                const InsetsContext insetsContext{ owner ? &owner->Insets() : nullptr, s, dir };
                // Only the bottom and horizontal insets apply to the snackbar and the FAB.
                const int leftInset = contentWindowInsets.Left(insetsContext), rightInset = contentWindowInsets.Right(insetsContext);
                const int bottomInset = contentWindowInsets.Bottom(insetsContext);
                const int fabSpacing = s.RoundToPx(FabSpacing);

                const auto topBarPlaceables = MeasureAll(s.Subcompose(rt::Key(TopBar), topBar), loose);
                const int topBarHeight = MaxHeight(topBarPlaceables);

                const auto snackbarPlaceables = MeasureAll(s.Subcompose(rt::Key(Snackbar), snackbar), loose.Offset(-leftInset - rightInset, -bottomInset));
                const int snackbarHeight = MaxHeight(snackbarPlaceables), snackbarWidth = MaxWidth(snackbarPlaceables);

                std::vector<ui::Placeable*> fabPlaceables;
                for (auto* m : s.Subcompose(rt::Key(Fab), fab)) {
                    auto& p = m->Measure(loose.Offset(-leftInset - rightInset, -bottomInset));
                    if (p.Width() != 0 && p.Height() != 0) fabPlaceables.push_back(&p);
                }
                struct FabPlacement { int left, width, height; };
                std::optional<FabPlacement> fabPlacement;
                if (!fabPlaceables.empty()) {
                    const int fabWidth = MaxWidth(fabPlaceables), fabHeight = MaxHeight(fabPlaceables);
                    // From the left of the layout, for either direction.
                    int left;
                    switch (fabPosition) {
                        case FabPosition::Start:
                            left = dir == LayoutDirection::Ltr ? fabSpacing + leftInset : layoutWidth - fabSpacing - fabWidth - rightInset;
                            break;
                        case FabPosition::End:
                        case FabPosition::EndOverlay:
                            left = dir == LayoutDirection::Ltr ? layoutWidth - fabSpacing - fabWidth - rightInset : fabSpacing + leftInset;
                            break;
                        default: left = (layoutWidth - fabWidth + leftInset - rightInset) / 2; break;
                    }
                    fabPlacement = FabPlacement{ left, fabWidth, fabHeight };
                }

                const auto bottomBarPlaceables = MeasureAll(s.Subcompose(rt::Key(BottomBar), bottomBar), loose);
                const std::optional<int> bottomBarHeight = bottomBarPlaceables.empty() ? std::nullopt : std::optional<int>(MaxHeight(bottomBarPlaceables));

                std::optional<int> fabOffsetFromBottom;
                if (fabPlacement) {
                    if (!bottomBarHeight || fabPosition == FabPosition::EndOverlay) fabOffsetFromBottom = fabPlacement->height + fabSpacing + bottomInset;
                    else fabOffsetFromBottom = *bottomBarHeight + fabPlacement->height + fabSpacing;   // above the bottom bar
                }
                const int snackbarOffsetFromBottom =
                    snackbarHeight != 0 ? snackbarHeight + (fabOffsetFromBottom ? *fabOffsetFromBottom : bottomBarHeight ? *bottomBarHeight : bottomInset) : 0;

                // The padding the body reads, before the body is subcomposed.
                const PaddingValues insets = contentWindowInsets.AsPaddingValues(insetsContext);   // absolute
                const bool ltr = dir == LayoutDirection::Ltr;
                const float d = s.density;
                const PaddingValues next{ ltr ? insets.start : insets.end,
                                          topBarPlaceables.empty() ? insets.top : Dp(float(topBarHeight) / d),
                                          ltr ? insets.end : insets.start,
                                          bottomBarHeight ? Dp(float(*bottomBarHeight) / d) : insets.bottom, true };
                if (!(padding->Peek() == next)) padding->Set(next);

                const auto bodyPlaceables = MeasureAll(s.Subcompose(rt::Key(MainContent), body), loose);

                return s.Layout(layoutWidth, layoutHeight, [=](ui::PlacementScope& place) {
                    // Placed in keyboard focus order; zIndex sets the drawing order.
                    for (auto* p : topBarPlaceables) place.Place(*p, 0, 0, TopBarZIndex);
                    for (auto* p : bodyPlaceables) place.Place(*p, 0, 0, ContentZIndex);
                    for (auto* p : snackbarPlaceables)
                        place.Place(*p, (layoutWidth - snackbarWidth + leftInset - rightInset) / 2, layoutHeight - snackbarOffsetFromBottom, SnackbarZIndex);
                    // Not relative: the left offset already accounts for RTL.
                    if (fabPlacement)
                        for (auto* p : fabPlaceables) place.Place(*p, fabPlacement->left, layoutHeight - *fabOffsetFromBottom, FabZIndex);
                    for (auto* p : bottomBarPlaceables) place.Place(*p, 0, layoutHeight - bottomBarHeight.value_or(0), BottomBarZIndex);
                });
            });
        }
    }

    namespace ScaffoldDefaults {
        // systemBarsForVisualComponents (Android): systemBars.union(displayCutout).
        WindowInsets contentWindowInsets() { return WindowInsets::SystemBars().Union(WindowInsets::DisplayCutout()); }
    }

    void Scaffold(const ui::Modifier& modifier, const ScaffoldOptions& options, kiln::Function<void(const PaddingValues&)> content) {
        auto& c = *rt::Composition::Current();
        const WindowInsets contentWindowInsets = options.contentWindowInsets.value_or(ScaffoldDefaults::contentWindowInsets());
        // MutableWindowInsets remembered with the given insets; consumed insets are excluded.
        struct Safe {
            std::optional<WindowInsets> given;
            std::shared_ptr<rt::State<WindowInsets>> insets = std::make_shared<rt::State<WindowInsets>>(WindowInsets{});
        };
        auto& safe = c.Remember<std::shared_ptr<Safe>>([] { return std::make_shared<Safe>(); });
        if (!safe->given || !(*safe->given == contentWindowInsets)) {
            safe = std::make_shared<Safe>();
            safe->given = contentWindowInsets;
            safe->insets->Set(contentWindowInsets);
        }
        const auto state = safe->insets;
        const ui::Color container = options.containerColor.value_or(MaterialTheme::colorScheme().background);
        SurfaceOptions surface;
        surface.color = container;
        surface.contentColor = options.contentColor.value_or(ContentColorFor(container));
        const auto noop = [] {};
        Surface(OnConsumedWindowInsetsChanged(modifier, [state, contentWindowInsets](const WindowInsets& consumed) {
                    const WindowInsets next = contentWindowInsets.Exclude(consumed);
                    if (!(state->Peek() == next)) state->Set(next);
                }),
                surface, [=] {
                    ScaffoldLayout(options.floatingActionButtonPosition, options.topBar ? options.topBar : noop, content,
                                   options.snackbarHost ? options.snackbarHost : noop,
                                   options.floatingActionButton ? options.floatingActionButton : noop,
                                   WindowInsets::FromState(state), options.bottomBar ? options.bottomBar : noop);
                });
    }

}
