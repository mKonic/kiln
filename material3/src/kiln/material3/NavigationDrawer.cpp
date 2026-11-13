#include "kiln/material3/NavigationDrawer.h"
#include <kiln/Function.h>

#include "kiln/animation/Easing.h"
#include "kiln/foundation/Back.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Scrim.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/tokens/ElevationTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/NavigationDrawerTokens.h"
#include "kiln/material3/tokens/ScrimTokens.h"
#include "kiln/runtime/Saveable.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        constexpr float DrawerPositionalThreshold = 0.5f;
        constexpr Dp DrawerVelocityThreshold{ 400 }, MinimumDrawerWidth{ 240 };
        constexpr Dp PredictiveBackDrawerMaxScaleXDistanceGrow{ 12 }, PredictiveBackDrawerMaxScaleXDistanceShrink{ 24 },
            PredictiveBackDrawerMaxScaleYDistance{ 48 };
        // The state's settle animation (AnchoredDraggableDefaultAnimationSpec, from DrawerLayout's
        // minimum duration). Compose builds the state with it, so the motion scheme never reaches it.
        anim::SpecPtr DrawerSettleSpec() {
            static const anim::SpecPtr spec = anim::Tween(256);
            return spec;
        }

        float CalculateFraction(float a, float b, float pos) { return std::clamp((pos - a) / (b - a), 0.0f, 1.0f); }

        rt::Task<> CloseDrawer(std::shared_ptr<DrawerState> s) { co_await s->Close(); }
        rt::Task<> FocusWhenOpen(std::shared_ptr<ui::FocusRequester> focus) {
            // Keyboard focus goes to the drawer's first element when it opens.
            focus->RequestFocus();
            co_return;
        }

        // The drawer's semantics, Escape key and focus requester (shared by the modal and
        // dismissible drawers).
        ui::Modifier DrawerPaneModifier(ui::Modifier m, std::shared_ptr<DrawerState> state, std::shared_ptr<internal::CoroutineScope> scope,
                                        std::shared_ptr<ui::FocusRequester> focus) {
            const std::string navigationMenu = GetString(Strings::NavigationMenu);
            const bool open = state->IsOpen();
            m = Semantics(std::move(m), [navigationMenu, open, state, scope](ui::SemanticsConfiguration& s) {
                s.paneTitle = navigationMenu;
                if (open)
                    s.dismiss = [state, scope] {
                        if (state->Confirm(DrawerValue::Closed)) scope->Launch(CloseDrawer(state));
                        return true;
                    };
            });
            m = OnKeyEvent(std::move(m), [state, scope](const ui::KeyEvent& e) {
                // The drawer closes with Escape.
                if (state->IsOpen() && e.type == ui::KeyEventType::KeyUp && e.keyCode == ui::keys::Escape) {
                    scope->Launch(CloseDrawer(state));
                    return true;
                }
                return false;
            });
            return FocusRequesterModifier(std::move(m), *focus);
        }

        AnchoredDraggableOptions DrawerDrag(bool enabled, bool rtl) {
            AnchoredDraggableOptions drag;
            drag.enabled = enabled;
            drag.reverseDirection = rtl;
            drag.positionalThreshold = [](float distance) { return distance * DrawerPositionalThreshold; };
            drag.velocityThreshold = DrawerVelocityThreshold;
            drag.snapAnimationSpec = DrawerSettleSpec();
            return drag;
        }

        // Shared by the modal and dismissible drawers: the motion specs, density and the focus
        // request when the drawer opens.
        std::shared_ptr<ui::FocusRequester> DrawerCommon(const std::shared_ptr<DrawerState>& state, bool withDraggableSpec) {
            auto& c = *rt::Composition::Current();
            const auto focus = c.Remember<std::shared_ptr<ui::FocusRequester>>([] { return std::make_shared<ui::FocusRequester>(); });
            const anim::SpecPtr openMotion = Value(MotionSchemeKeyTokens::DefaultSpatial);
            const anim::SpecPtr closeMotion = Value(MotionSchemeKeyTokens::FastEffects);
            (void)withDraggableSpec;   // anchoredDraggableMotionSpec never reaches Compose's state either
            c.SideEffect([state, openMotion, closeMotion] {
                state->openDrawerMotionSpec = openMotion;
                state->closeDrawerMotionSpec = closeMotion;
            });
            const bool open = state->IsOpen();
            c.LaunchedEffect(rt::Keys(open), [open, focus]() -> rt::Task<> {
                if (open) return FocusWhenOpen(focus);
                return []() -> rt::Task<> { co_return; }();
            });
            return focus;
        }

        // DrawerPredictiveBackState: how far the sheet stretches or shrinks across and down.
        struct DrawerPredictiveBackState {
            rt::State<bool> swipeEdgeMatchesDrawer{ true };
            rt::State<float> scaleXDistance{ 0 }, scaleYDistance{ 0 };
            void Update(float progress, bool swipeEdgeLeft, bool isRtl, float maxGrow, float maxShrink, float maxY) {
                swipeEdgeMatchesDrawer.Set(swipeEdgeLeft != isRtl);
                const float maxX = swipeEdgeMatchesDrawer.Peek() ? maxGrow : maxShrink;
                scaleXDistance.Set(maxX * progress);
                scaleYDistance.Set(maxY * progress);
            }
            void Clear() {
                swipeEdgeMatchesDrawer.Set(true);
                scaleXDistance.Set(0);
                scaleYDistance.Set(0);
            }
        };

        // Un-stretches the sheet smoothly so it hides completely as the drawer closes.
        rt::Task<> Unstretch(std::shared_ptr<DrawerPredictiveBackState> back) {
            auto a = std::make_shared<anim::Animatable>(back->scaleXDistance.Peek());
            co_await a->AnimateTo(anim::Vec{ 0, 0, 0, 0 }, anim::Spring(), std::nullopt, [back](const anim::Vec& v) { back->scaleXDistance.Set(v[0]); });
            back->Clear();
        }

        rt::Task<> DrawerBack(BackProgress& events, std::shared_ptr<DrawerPredictiveBackState> back, std::shared_ptr<internal::CoroutineScope> scope,
                              bool rtl, float maxGrow, float maxShrink, float maxY, std::shared_ptr<DrawerState> state) {
            // A cancelled gesture puts the sheet back; nothing closes (Compose's close() in its
            // finally block cannot run in the cancelled coroutine).
            struct OnCancel {
                std::shared_ptr<DrawerPredictiveBackState> back;
                bool completed = false;
                ~OnCancel() { if (!completed) back->Clear(); }
            } onCancel{ back };
            static const anim::Easing easing = anim::Easing::CubicBezier(0.1f, 0.1f, 0.0f, 1.0f);   // PredictiveBack.transform
            while (auto e = co_await events.Next())
                back->Update(easing.Transform(e->progress), e->swipeEdge == ui::SwipeEdge::Left, rtl, maxGrow, maxShrink, maxY);
            onCancel.completed = true;
            if (back->swipeEdgeMatchesDrawer.Peek()) scope->Launch(Unstretch(back));
            co_await state->Close();
        }

        std::shared_ptr<DrawerPredictiveBackState> DrawerPredictiveBackHandler(const std::shared_ptr<DrawerState>& state) {
            auto& c = *rt::Composition::Current();
            const auto back = c.Remember<std::shared_ptr<DrawerPredictiveBackState>>([] { return std::make_shared<DrawerPredictiveBackState>(); });
            const auto scope = internal::RememberCoroutineScope();
            const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
            const ui::Density d = c.Read(ui::LocalDensity);
            const float maxGrow = d.ToPx(PredictiveBackDrawerMaxScaleXDistanceGrow), maxShrink = d.ToPx(PredictiveBackDrawerMaxScaleXDistanceShrink),
                        maxY = d.ToPx(PredictiveBackDrawerMaxScaleYDistance);
            PredictiveBackHandler(state->TargetValue() == DrawerValue::Open, [=](BackProgress& events) {
                return DrawerBack(events, back, scope, rtl, maxGrow, maxShrink, maxY, state);
            });
            const bool closed = state->IsClosed();
            c.LaunchedEffect(rt::Keys(closed), [closed, back]() -> rt::Task<> {
                if (closed) back->Clear();
                co_return;
            });
            return back;
        }

        float PredictiveBackScaleX(const DrawerPredictiveBackState& back, float width) {
            if (std::isnan(width) || width == 0) return 1;
            const float direction = back.swipeEdgeMatchesDrawer.Get() ? 1.0f : -1.0f;
            return 1 + back.scaleXDistance.Get() * direction / width;
        }
        float PredictiveBackScaleY(const DrawerPredictiveBackState& back, float height) {
            if (std::isnan(height) || height == 0) return 1;
            return 1 - back.scaleYDistance.Get() / height;
        }

        struct DrawerSheetParams {
            std::shared_ptr<DrawerPredictiveBackState> back;   // null: no predictive back
            WindowInsets windowInsets;
            ShapePtr shape;
            Color containerColor, contentColor;
            Dp tonalElevation;
            kiln::Function<float()> drawerOffset = [] { return 0.0f; };
        };

        void DrawerSheet(const ui::Modifier& modifier, const DrawerSheetParams& p, kiln::Function<void()> content) {
            auto& c = *rt::Composition::Current();
            const ui::Density density = c.Read(ui::LocalDensity);
            const Dp maxWidth = NavigationDrawerTokens::ContainerWidth;
            const float maxWidthPx = density.ToPx(maxWidth);
            const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
            const auto offset = p.drawerOffset;
            ui::Modifier m = SizeIn(modifier, MinimumDrawerWidth, Dp::Unspecified(), maxWidth, Dp::Unspecified());
            // The sheet stretches when a bouncy motion carries it past open, so no gap shows at
            // its start; its content is scaled back to keep its aspect.
            m = GraphicsLayer(std::move(m), [offset, maxWidthPx, rtl](GraphicsLayerParams& l) {
                const float o = offset();
                l.scaleX = o > 0 ? 1 + o / maxWidthPx : 1.0f;
                l.originX = rtl ? 0.0f : 1.0f;
                l.originY = 0.5f;
            });
            if (const auto back = p.back) {
                m = GraphicsLayer(std::move(m), [back, rtl](GraphicsLayerParams& l) {
                    l.scaleX = PredictiveBackScaleX(*back, l.size.width);
                    l.scaleY = PredictiveBackScaleY(*back, l.size.height);
                    l.originX = rtl ? 1.0f : 0.0f;
                    l.originY = 0.5f;
                });
            }
            m = FillMax(std::move(m), Axis::Vertical);
            SurfaceOptions surface;
            surface.shape = p.shape;
            surface.color = p.containerColor;
            surface.contentColor = p.contentColor;
            surface.tonalElevation = p.tonalElevation;
            const auto back = p.back;
            const WindowInsets insets = p.windowInsets;
            Surface(m, surface, [=] {
                ui::Modifier col = SizeIn({}, MinimumDrawerWidth, Dp::Unspecified(), maxWidth, Dp::Unspecified());
                col = GraphicsLayer(std::move(col), [offset, maxWidthPx, rtl](GraphicsLayerParams& l) {
                    const float o = offset();
                    l.scaleX = o > 0 ? 1 / (1 + o / maxWidthPx) : 1.0f;
                    l.originX = rtl ? 0.0f : 1.0f;
                    l.originY = 0;
                });
                if (back) {
                    // Keeps the content's aspect and its place in the shrinking container.
                    col = GraphicsLayer(std::move(col), [back, rtl](GraphicsLayerParams& l) {
                        const float sx = PredictiveBackScaleX(*back, l.size.width), sy = PredictiveBackScaleY(*back, l.size.height);
                        l.scaleX = sx != 0 ? sy / sx : 1.0f;
                        l.originX = rtl ? 0.0f : 1.0f;
                        l.originY = 0;
                    });
                }
                col = WindowInsetsPadding(std::move(col), insets);
                Column(col, Arrangement::Top(), ui::AlignmentH::Start(), content);
            });
        }

        void SheetWithBack(const ui::Modifier& modifier, DrawerSheetParams p, kiln::Function<void()> content, const std::shared_ptr<DrawerState>& state) {
            if (state) {
                p.back = DrawerPredictiveBackHandler(state);
                p.drawerOffset = [state] { return state->Anchored().Offset(); };
            }
            DrawerSheet(modifier, p, std::move(content));
        }

        // ModalNavigationDrawer's sheet layout: placed only while it shows, so keyboard focus
        // doesn't reach it off screen; it sets the anchors from the sheet's width.
        struct ModalDrawerPolicy final : ui::MeasurePolicy {
            std::shared_ptr<DrawerState> state;
            std::shared_ptr<rt::State<float>> minValue;
            std::shared_ptr<bool> anchorsInitialized;
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                ui::Constraints loose = c;
                loose.minWidth = loose.minHeight = 0;
                std::vector<ui::Placeable*> placeables;
                int width = 0, height = 0;
                for (ui::Measurable* m : children) {
                    ui::Placeable& p = m->Measure(loose);
                    placeables.push_back(&p);
                    width = std::max(width, p.Width());
                    height = std::max(height, p.Height());
                }
                return s.Layout(width, height, [st = state, minValue = minValue, init = anchorsInitialized, placeables, width](ui::PlacementScope& place) {
                    const float current = st->Anchored().Anchors().PositionOf(DrawerValue::Closed);
                    const float calculated = -float(width);
                    if (!*init || current != calculated) {
                        *init = true;
                        minValue->Set(calculated);
                        foundation::DraggableAnchors<DrawerValue> a;
                        a.At(DrawerValue::Closed, calculated);
                        a.At(DrawerValue::Open, 0);
                        st->Anchored().UpdateAnchors(std::move(a));
                    }
                    if (CalculateFraction(minValue->Peek(), 0, st->Anchored().Offset()) != 0)
                        for (ui::Placeable* p : placeables) place.PlaceRelative(*p, 0, 0);
                });
            }
        };

        // DismissibleNavigationDrawer: the content moves aside by the sheet's width as it opens.
        struct DismissibleDrawerPolicy final : ui::MeasurePolicy {
            std::shared_ptr<DrawerState> state;
            std::shared_ptr<bool> anchorsInitialized;
            ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override {
                ui::Placeable& sheet = children[0]->Measure(c);
                ui::Placeable& content = children[1]->Measure(c);
                return s.Layout(content.Width(), content.Height(), [st = state, init = anchorsInitialized, sheetP = &sheet, contentP = &content](ui::PlacementScope& place) {
                    const float current = st->Anchored().Anchors().PositionOf(DrawerValue::Closed);
                    const float calculated = -float(sheetP->Width());
                    if (!*init || current != calculated) {
                        *init = true;
                        foundation::DraggableAnchors<DrawerValue> a;
                        a.At(DrawerValue::Closed, calculated);
                        a.At(DrawerValue::Open, 0);
                        st->Anchored().UpdateAnchors(std::move(a));
                    }
                    const int offset = int(std::lround(st->Anchored().Offset()));
                    const int contentX = sheetP->Width() + offset;
                    place.PlaceRelative(*contentP, contentX, 0);
                    // The drawer shows once the content has moved.
                    if (contentX != 0) place.PlaceRelative(*sheetP, offset, 0);
                });
            }
        };
    }

    DrawerState::DrawerState(DrawerValue initialValue, kiln::Function<bool(DrawerValue)> confirmStateChange)
        : openDrawerMotionSpec(anim::Snap()), closeDrawerMotionSpec(anim::Snap()), m_Confirm(confirmStateChange),
          m_Anchored(initialValue, [confirmStateChange](const DrawerValue& v) { return !confirmStateChange || confirmStateChange(v); }) {}

    rt::Task<> DrawerState::Open() { co_await AnimateTo(DrawerValue::Open, openDrawerMotionSpec); }
    rt::Task<> DrawerState::Close() { co_await AnimateTo(DrawerValue::Closed, closeDrawerMotionSpec); }
    rt::Task<> DrawerState::AnimateTo(DrawerValue target, anim::SpecPtr spec) { co_await m_Anchored.AnimateTo(target, std::move(spec)); }
    rt::Task<> DrawerState::SnapTo(DrawerValue target) { co_await m_Anchored.SnapTo(target); }

    std::shared_ptr<DrawerState> RememberDrawerState(DrawerValue initialValue, kiln::Function<bool(DrawerValue)> confirmStateChange, rt::Key key) {
        // DrawerState.Saver keeps the current value.
        rt::Saver<std::shared_ptr<DrawerState>> saver{
            [](const std::shared_ptr<DrawerState>& s) { return rt::SavedValue(double(int(s->CurrentValue()))); },
            [confirmStateChange](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<DrawerState>> {
                if (auto n = rt::SavedNumber(saved)) return std::make_shared<DrawerState>(DrawerValue(int(*n)), confirmStateChange);
                return std::nullopt;
            }
        };
        return rt::RememberSaveable<std::shared_ptr<DrawerState>>([&] { return std::make_shared<DrawerState>(initialValue, confirmStateChange); }, saver,
                                                                  rt::Keys(), key);
    }

    namespace DrawerDefaults {
        const Dp ModalDrawerElevation = ElevationTokens::Level0;
        const Dp PermanentDrawerElevation = NavigationDrawerTokens::StandardContainerElevation;
        const Dp DismissibleDrawerElevation = NavigationDrawerTokens::StandardContainerElevation;
        const Dp MaximumDrawerWidth = NavigationDrawerTokens::ContainerWidth;
        ShapePtr shape() { return Value(NavigationDrawerTokens::ContainerShape); }
        Color scrimColor() { return Value(ScrimTokens::ContainerColor).WithAlpha(ScrimTokens::ContainerOpacity); }
        Color standardContainerColor() { return Value(NavigationDrawerTokens::StandardContainerColor); }
        Color modalContainerColor() { return Value(NavigationDrawerTokens::ModalContainerColor); }
        // systemBarsForVisualComponents (Android): systemBars.union(displayCutout).
        WindowInsets windowInsets() {
            return WindowInsets::SystemBars().Union(WindowInsets::DisplayCutout()).Only(WindowInsetsSides::Vertical() + WindowInsetsSides::Start());
        }
    }

    void ModalNavigationDrawer(kiln::Function<void()> drawerContent, const ui::Modifier& modifier, const ModalNavigationDrawerOptions& o,
                               kiln::Function<void()> content) {
        auto& c = *rt::Composition::Current();
        const auto state = o.drawerState ? o.drawerState : RememberDrawerState(DrawerValue::Closed);
        const auto scope = internal::RememberCoroutineScope();
        const ui::Density density = c.Read(ui::LocalDensity);
        const auto anchorsInitialized = c.Remember<std::shared_ptr<bool>>([] { return std::make_shared<bool>(false); });
        const auto minValue = c.Remember<std::shared_ptr<rt::State<float>>>([] { return std::make_shared<rt::State<float>>(0.0f); },
                                                                            rt::Keys(density.density, density.fontScale));
        const auto focus = DrawerCommon(state, true);
        const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
        const bool gestures = o.gesturesEnabled;
        const Color scrimColor = o.scrimColor.value_or(DrawerDefaults::scrimColor());
        const bool open = state->IsOpen();
        const auto policy = c.Remember<std::shared_ptr<ModalDrawerPolicy>>([] { return std::make_shared<ModalDrawerPolicy>(); });
        policy->state = state;
        policy->minValue = minValue;
        policy->anchorsInitialized = anchorsInitialized;
        const Dp maxDrawerWidth = DrawerDefaults::MaximumDrawerWidth;
        Box(AnchoredDraggable(FillMax(modifier, Axis::Both), state->Anchored(), Orientation::Horizontal, DrawerDrag(gestures, rtl)),
            ui::Alignment::TopStart(), false, [=] {
                Box({}, ui::Alignment::TopStart(), false, content);
                const auto onDismissRequest = [gestures, state, scope] {
                    if (gestures && state->Confirm(DrawerValue::Closed)) scope->Launch(CloseDrawer(state));
                };
                ScrimOptions scrim;
                scrim.onClick = open ? kiln::Function<void()>(onDismissRequest) : nullptr;
                scrim.alpha = [minValue, state] { return CalculateFraction(minValue->Get(), 0, state->Anchored().Offset()); };
                scrim.color = scrimColor;
                Scrim(GetString(Strings::CloseDrawer), {}, scrim);
                ui::Modifier sheet = OffsetMod({}, [state, maxDrawerWidth](const ui::Density& d) {
                    const float offset = state->CurrentOffset();
                    // Before the anchors are known, from the open or closed state.
                    const int x = !std::isnan(offset) ? int(std::lround(offset)) : state->IsOpen() ? 0 : -int(std::lround(d.ToPx(maxDrawerWidth)));
                    return ui::IntOffset{ x, 0 };
                });
                sheet = DrawerPaneModifier(std::move(sheet), state, scope, focus);
                Layout(sheet, policy, drawerContent);
            });
    }

    void DismissibleNavigationDrawer(kiln::Function<void()> drawerContent, const ui::Modifier& modifier, const DismissibleNavigationDrawerOptions& o,
                                     kiln::Function<void()> content) {
        auto& c = *rt::Composition::Current();
        const auto state = o.drawerState ? o.drawerState : RememberDrawerState(DrawerValue::Closed);
        const auto scope = internal::RememberCoroutineScope();
        const auto anchorsInitialized = c.Remember<std::shared_ptr<bool>>([] { return std::make_shared<bool>(false); });
        const auto focus = DrawerCommon(state, false);
        const bool rtl = c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
        const auto policy = c.Remember<std::shared_ptr<DismissibleDrawerPolicy>>([] { return std::make_shared<DismissibleDrawerPolicy>(); });
        policy->state = state;
        policy->anchorsInitialized = anchorsInitialized;
        Box(AnchoredDraggable(modifier, state->Anchored(), Orientation::Horizontal, DrawerDrag(o.gesturesEnabled, rtl)), ui::Alignment::TopStart(), false,
            [=] {
                Layout({}, policy, [=] {
                    Box(DrawerPaneModifier({}, state, scope, focus), ui::Alignment::TopStart(), false, drawerContent);
                    Box({}, ui::Alignment::TopStart(), false, content);
                });
            });
    }

    void PermanentNavigationDrawer(kiln::Function<void()> drawerContent, const ui::Modifier& modifier, kiln::Function<void()> content) {
        Row(FillMax(modifier, Axis::Both), [=] {
            drawerContent();
            Box({}, ui::Alignment::TopStart(), false, content);
        });
    }

    namespace {
        DrawerSheetParams SheetParams(const DrawerSheetOptions& o, ShapePtr shape, Color container, Dp elevation) {
            DrawerSheetParams p;
            p.windowInsets = o.windowInsets.value_or(DrawerDefaults::windowInsets());
            p.shape = o.drawerShape ? o.drawerShape : std::move(shape);
            p.containerColor = o.drawerContainerColor.value_or(container);
            p.contentColor = o.drawerContentColor.value_or(ContentColorFor(p.containerColor));
            p.tonalElevation = o.drawerTonalElevation.value_or(elevation);
            return p;
        }
    }

    void ModalDrawerSheet(const ui::Modifier& modifier, const DrawerSheetOptions& o, kiln::Function<void()> content, std::shared_ptr<DrawerState> drawerState) {
        SheetWithBack(modifier, SheetParams(o, DrawerDefaults::shape(), DrawerDefaults::modalContainerColor(), DrawerDefaults::ModalDrawerElevation),
                      std::move(content), drawerState);
    }

    void DismissibleDrawerSheet(const ui::Modifier& modifier, const DrawerSheetOptions& o, kiln::Function<void()> content,
                                std::shared_ptr<DrawerState> drawerState) {
        SheetWithBack(modifier, SheetParams(o, RectangleShape(), DrawerDefaults::standardContainerColor(), DrawerDefaults::DismissibleDrawerElevation),
                      std::move(content), drawerState);
    }

    void PermanentDrawerSheet(const ui::Modifier& modifier, const DrawerSheetOptions& o, kiln::Function<void()> content) {
        const std::string navigationMenu = GetString(Strings::NavigationMenu);
        DrawerSheet(Semantics(modifier, [navigationMenu](ui::SemanticsConfiguration& s) { s.paneTitle = navigationMenu; }),
                    SheetParams(o, RectangleShape(), DrawerDefaults::standardContainerColor(), DrawerDefaults::PermanentDrawerElevation),
                    std::move(content));
    }

    namespace NavigationDrawerItemDefaults {
        NavigationDrawerItemColors colors() {
            NavigationDrawerItemColors c;
            c.selectedContainerColor = Value(NavigationDrawerTokens::ActiveIndicatorColor);
            c.unselectedContainerColor = Color::Transparent();
            c.selectedIconColor = Value(NavigationDrawerTokens::ActiveIconColor);
            c.unselectedIconColor = Value(NavigationDrawerTokens::InactiveIconColor);
            c.selectedTextColor = Value(NavigationDrawerTokens::ActiveLabelTextColor);
            c.unselectedTextColor = Value(NavigationDrawerTokens::InactiveLabelTextColor);
            c.selectedBadgeColor = c.selectedTextColor;
            c.unselectedBadgeColor = c.unselectedTextColor;
            return c;
        }
        NavigationDrawerItemColors colors(Color selectedContainer, Color unselectedContainer, Color selectedIcon, Color unselectedIcon, Color selectedText,
                                          Color unselectedText, Color selectedBadge, Color unselectedBadge) {
            NavigationDrawerItemColors c = colors();
            auto set = [](Color& field, Color v) { if (v.Specified()) field = v; };
            set(c.selectedContainerColor, selectedContainer);
            set(c.unselectedContainerColor, unselectedContainer);
            set(c.selectedIconColor, selectedIcon);
            set(c.unselectedIconColor, unselectedIcon);
            set(c.selectedTextColor, selectedText);
            set(c.unselectedTextColor, unselectedText);
            c.selectedBadgeColor = selectedBadge.Specified() ? selectedBadge : c.selectedTextColor;
            c.unselectedBadgeColor = unselectedBadge.Specified() ? unselectedBadge : c.unselectedTextColor;
            return c;
        }
    }

    void NavigationDrawerItem(kiln::Function<void()> label, bool selected, kiln::Function<void()> onClick, const ui::Modifier& modifier,
                              const NavigationDrawerItemOptions& o) {
        auto& c = *rt::Composition::Current();
        const NavigationDrawerItemColors colors = o.colors.value_or(NavigationDrawerItemDefaults::colors());
        ui::Modifier m = Semantics(modifier, [](ui::SemanticsConfiguration& s) { s.role = ui::Role::Tab; });
        m = SizeIn(std::move(m), Dp::Unspecified(), NavigationDrawerTokens::ActiveIndicatorHeight, Dp::Unspecified(), Dp::Unspecified());
        m = FillMax(std::move(m), Axis::Horizontal);
        SurfaceOptions surface;
        surface.shape = o.shape ? o.shape : Value(NavigationDrawerTokens::ActiveIndicatorShape);
        surface.color = colors.ContainerColor(selected);
        const auto icon = o.icon, badge = o.badge;
        (void)c;
        Surface(selected, std::move(onClick), m, surface, [=] {
            Row(Padding({}, PaddingValues{ Dp(16), Dp(0), Dp(24), Dp(0) }), Arrangement::Start(), ui::AlignmentV::CenterVertically(), [=] {
                auto& cc = *rt::Composition::Current();
                if (icon) {
                    cc.Provide(LocalContentColor, colors.IconColor(selected), icon);
                    Spacer(Width({}, Dp(12)));
                }
                Box(Weight({}, 1), ui::Alignment::TopStart(), false, [=] {
                    rt::Composition::Current()->Provide(LocalContentColor, colors.TextColor(selected), label);
                });
                if (badge) {
                    Spacer(Width({}, Dp(12)));
                    cc.Provide(LocalContentColor, colors.BadgeColor(selected), badge);
                }
            });
        }, true, o.interactionSource);
    }

}
