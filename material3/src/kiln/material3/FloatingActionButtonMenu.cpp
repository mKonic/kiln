#include "kiln/material3/FloatingActionButtonMenu.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/tokens/FabMenuBaselineTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"

#include <limits>

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Dp;

    namespace internal {
        struct FabMenuState {
            rt::State<bool> composeItems{ false }, needsScroll{ false };
            rt::State<int> visibleCount{ 0 };
            int targetCount = -1, buttonHeight = 0, originalMaxHeight = ui::Constraints::Infinity;
            bool expanded = false;
            ui::FocusRequester focusRequester;
            std::unique_ptr<anim::Animatable> stagger;
            rt::Job staggerJob;
        };
    }
    namespace {
        using internal::FabMenuState;
        const ui::HorizontalRuler MenuItemRuler;
        struct ItemProgress {
            explicit ItemProgress(bool initiallyVisible) : width(initiallyVisible ? 1.0f : 0.0f), alpha(initiallyVisible ? 1.0f : 0.0f), visible(initiallyVisible) {}
            rt::State<float> width, alpha;
            rt::State<bool> visible;
        };
        rt::Task<> Stagger(FabMenuState& state, int target, anim::SpecPtr spec) {
            co_await state.stagger->AnimateTo(anim::Vec{ float(target), 0, 0, 0 }, std::move(spec), {},
                                            [&state](const anim::Vec& value) { state.visibleCount.Set(int(value[0])); });
        }
        int UpdateTarget(FabMenuState& state, int target, anim::SpecPtr spec) {
            if (state.targetCount < 0) {
                state.targetCount = target;
                state.visibleCount.Set(target);
            } else if (state.targetCount != target) {
                state.targetCount = target;
                if (!state.stagger) state.stagger = std::make_unique<anim::Animatable>(float(state.visibleCount.Peek()));
                state.staggerJob.Cancel();
                state.staggerJob = rt::Launch(rt::Dispatcher::Main(), Stagger(state, target, std::move(spec)));
            }
            return state.visibleCount.Get();
        }

        struct WidthParams {
            std::shared_ptr<ItemProgress> progress;
            ui::AlignmentH alignment;
            bool operator==(const WidthParams&) const = default;
        };
        struct WidthNode final : ui::ModifierNode, ui::LayoutModifier {
            WidthParams params;
            explicit WidthNode(WidthParams p) : params(std::move(p)) {}
            ui::LayoutModifier* AsLayout() override { return this; }
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                auto& p = m.Measure(c);
                const int width = ui::RoundJava(p.Width() * std::max(params.progress->width.Get(), 0.0f));
                return s.Layout(width, p.Height(), [this, &p, width, dir = s.Direction()](ui::PlacementScope& ps) {
                    ps.Place(p, params.alignment.Align(p.Width(), width, dir), 0);
                });
            }
        };
        struct AnimationParams {
            std::shared_ptr<ItemProgress> progress;
            anim::SpecPtr widthSpec, alphaSpec;
            bool operator==(const AnimationParams&) const = default;
        };
        struct ItemAnimationNode final : ui::ModifierNode, ui::LayoutModifier, ui::DrawModifier, ui::ParentDataModifier, ui::SemanticsModifier {
            AnimationParams params;
            std::shared_ptr<ItemProgress> current;
            float target = std::numeric_limits<float>::quiet_NaN();
            std::unique_ptr<anim::Animatable> widthAnim, alphaAnim;
            rt::Job widthJob, alphaJob;
            explicit ItemAnimationNode(AnimationParams p) : params(std::move(p)), current(params.progress) {}
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::DrawModifier* AsDraw() override { return this; }
            ui::ParentDataModifier* AsParentData() override { return this; }
            ui::SemanticsModifier* AsSemantics() override { return this; }
            bool IsImportantForBounds() const override { return false; }
            void ApplySemantics(ui::SemanticsConfiguration& c) override { c.clearAndSet = !current->visible.Get(); }
            void ModifyParentData(ui::ParentData& data) const override { data.extra = current; }
            void OnParamsChanged() {
                if (current != params.progress) {
                    OnDetach();
                    current = params.progress;
                    target = std::numeric_limits<float>::quiet_NaN();
                    widthAnim.reset(); alphaAnim.reset();
                    SetAlpha(current->width.Peek());
                    InvalidateMeasure();
                }
            }
            void OnDetach() override { widthJob.Cancel(); alphaJob.Cancel(); }
            void SetAlpha(float alpha) {
                current->alpha.Set(alpha);
                if (current->visible.Peek() != (alpha != 0)) {
                    current->visible.Set(alpha != 0);
                    InvalidateSemantics();
                }
            }
            static rt::Task<> AnimateWidth(ItemAnimationNode& node, float to, anim::SpecPtr spec) {
                co_await node.widthAnim->AnimateTo(anim::Vec{ to, 0, 0, 0 }, std::move(spec), {},
                                                  [&node](const anim::Vec& v) { node.current->width.Set(v[0]); });
            }
            static rt::Task<> AnimateAlpha(ItemAnimationNode& node, float to, anim::SpecPtr spec) {
                co_await node.alphaAnim->AnimateTo(anim::Vec{ to, 0, 0, 0 }, std::move(spec), {},
                                                  [&node](const anim::Vec& v) { node.SetAlpha(v[0]); });
            }
            void UpdateTarget(float to) {
                if (std::isnan(target)) {
                    target = to;
                    current->width.Set(to);
                    SetAlpha(to);
                } else if (target != to) {
                    target = to;
                    if (!widthAnim) widthAnim = std::make_unique<anim::Animatable>(current->width.Peek());
                    if (!alphaAnim) alphaAnim = std::make_unique<anim::Animatable>(current->alpha.Peek());
                    widthJob.Cancel(); alphaJob.Cancel();
                    widthJob = rt::Launch(rt::Dispatcher::Main(), AnimateWidth(*this, to, params.widthSpec));
                    alphaJob = rt::Launch(rt::Dispatcher::Main(), AnimateAlpha(*this, to, params.alphaSpec));
                }
            }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                auto& p = m.Measure(c);
                return s.Layout(p.Width(), p.Height(), [this, &p](ui::PlacementScope& ps) {
                    UpdateTarget(ps.Current(MenuItemRuler, std::numeric_limits<float>::infinity()) <= 0 ? 1.0f : 0.0f);
                    if (current->visible.Get()) ps.Place(p, 0, 0);
                });
            }
            float LayerAlpha() const override { return current->alpha.Peek(); }
            void Draw(ui::ContentDrawScope& s) override {
                const auto size = s.GetSize();
                s.BeginLayer({ 0, 0, size.width, size.height }, current->alpha.Get());
                s.DrawContent();
                s.EndLayer();
            }
        };
        struct CaptureParams { std::shared_ptr<FabMenuState> state; bool operator==(const CaptureParams&) const = default; };
        struct CaptureHeightNode final : ui::ModifierNode, ui::LayoutModifier {
            CaptureParams params;
            explicit CaptureHeightNode(CaptureParams p) : params(std::move(p)) {}
            ui::LayoutModifier* AsLayout() override { return this; }
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                params.state->originalMaxHeight = c.maxHeight;
                auto& p = m.Measure(c);
                params.state->originalMaxHeight = ui::Constraints::Infinity;
                return s.Layout(p.Width(), p.Height(), [&p](ui::PlacementScope& ps) { ps.Place(p, 0, 0); });
            }
        };
        bool Visible(ui::Measurable& measurable) {
            const auto* progress = std::any_cast<std::shared_ptr<ItemProgress>>(&measurable.GetParentData().extra);
            return !progress || (*progress)->visible.Get();
        }
        void ItemColumn(const std::shared_ptr<FabMenuState>& state, ui::AlignmentH alignment,
                        const std::function<void(const FloatingActionButtonMenuScope&)>& content) {
            const auto slow = Value(MotionSchemeKeyTokens::SlowEffects);
            auto& composition = *rt::Composition::Current();
            const auto spec = composition.Remember<anim::SpecPtr>([&] { return anim::WithSpringVisibilityThreshold(slow, 1); },
                                                                  rt::Keys(std::hash<const void*>{}(slow.get())));
            auto modifier = Semantics(ClipToBounds(FocusRequesterModifier({}, state->focusRequester)), [](ui::SemanticsConfiguration& c) {
                c.isTraversalGroup = true;
                c.traversalIndex = -0.9f;
            });
            if (state->needsScroll.Get()) {
                auto& scroll = RememberScrollState();
                modifier = VerticalScroll(modifier.With<CaptureHeightNode>(CaptureParams{ state }), scroll, state->expanded);
            }
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([state, alignment, spec](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                const int count = int(children.size());
                const int visible = UpdateTarget(*state, state->expanded ? count : 0, spec);
                std::vector<ui::Placeable*> placeables;
                int width = 0, height = 0;
                const int spacing = s.RoundToPx(FabMenuBaselineTokens::ListItemBetweenSpace);
                const int bottom = state->buttonHeight > 0 ? state->buttonHeight + s.RoundToPx(Dp(16)) + s.RoundToPx(FabMenuBaselineTokens::CloseButtonBetweenSpace) : 0;
                float visibleHeight = float(bottom);
                bool anyVisible = false;
                for (int i = 0; i < count; ++i) {
                    auto& p = children[i]->Measure(c);
                    placeables.push_back(&p);
                    width = std::max(width, p.Width());
                    height += p.Height();
                    if (i < count - 1) height += spacing;
                    if (visible > 0 && i >= count - visible) {
                        visibleHeight += p.Height();
                        if (i < count - 1) visibleHeight += spacing;
                    }
                    anyVisible = anyVisible || Visible(*children[i]);
                }
                height += bottom;
                const int finalHeight = anyVisible ? height : 0;
                state->needsScroll.Set(finalHeight > std::min(state->originalMaxHeight, c.maxHeight));
                return s.Layout(width, finalHeight, {}, [ruler = float(height) - visibleHeight](ui::RulerScope& r) { r.Provides(MenuItemRuler, ruler); },
                                [placeables, width, spacing, alignment, dir = s.Direction()](ui::PlacementScope& ps) {
                    int y = 0;
                    for (auto* p : placeables) {
                        ps.Place(*p, alignment.Align(p->Width(), width, dir), y);
                        y += p->Height() + spacing;
                    }
                });
            });
            Layout(modifier, std::move(policy), [=] {
                if (state->composeItems.Get()) content({ alignment, state });
            });
        }
    }

    void FloatingActionButtonMenu(bool expanded, std::function<void()> button, const ui::Modifier& modifier,
                                  ui::AlignmentH alignment, std::function<void(const FloatingActionButtonMenuScope&)> content) {
        auto& c = *rt::Composition::Current();
        const auto state = c.Remember<std::shared_ptr<FabMenuState>>([] { return std::make_shared<FabMenuState>(); });
        state->expanded = expanded;
        if (expanded) state->composeItems.Set(true);
        c.DisposableEffect({}, [state] { return [state] { state->staggerJob.Cancel(); }; });
        auto policy = std::make_shared<ui::LambdaMeasurePolicy>([state, alignment](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& constraints) {
            const int padding = s.RoundToPx(Dp(16));
            auto& button = children[1]->Measure(constraints);
            state->buttonHeight = button.Height();
            auto& items = children[0]->Measure(constraints);
            const int width = std::min(std::max(button.Width(), items.Width()), constraints.maxWidth);
            const int height = std::min(std::max(button.Height() + padding, items.Height()), constraints.maxHeight);
            return s.Layout(width, height, [&button, &items, width, height, padding, alignment, dir = s.Direction()](ui::PlacementScope& ps) {
                ps.Place(items, alignment.Align(items.Width(), width, dir), 0);
                ps.Place(button, alignment.Align(button.Width(), width, dir), height - button.Height() - padding);
            });
        });
        Layout(Padding(modifier, Dp(16), Dp(0)), std::move(policy), [=] {
            ItemColumn(state, alignment, content);
            Box(OnKeyEvent({}, [state](const ui::KeyEvent& key) {
                if (state->expanded && key.type == ui::KeyEventType::KeyDown &&
                    ((key.keyCode == ui::keys::Tab && !key.Shift()) || key.keyCode == ui::keys::DpadDown || key.keyCode == -1000000002)) {
                    state->focusRequester.RequestFocus();
                    return true;
                }
                return false;
            }), ui::Alignment::TopStart(), false, button);
        });
    }

    void FloatingActionButtonMenuItem(const FloatingActionButtonMenuScope& scope, std::function<void()> click, std::function<void()> text,
                                      std::function<void()> icon, const ui::Modifier& modifier, const FloatingActionButtonMenuItemOptions& options) {
        auto& c = *rt::Composition::Current();
        const auto progress = c.Remember<std::shared_ptr<ItemProgress>>([&] { return std::make_shared<ItemProgress>(scope.state && scope.state->targetCount < 0); });
        const auto container = options.containerColor.value_or(MaterialTheme::colorScheme().primaryContainer);
        const auto foreground = options.contentColor.value_or(ContentColorFor(container));
        SurfaceOptions surface;
        surface.color = container;
        surface.contentColor = foreground;
        surface.shape = Value(FabMenuBaselineTokens::ListItemContainerShape);
        const auto animated = modifier.With<ItemAnimationNode>(AnimationParams{ progress, Value(MotionSchemeKeyTokens::FastSpatial), Value(MotionSchemeKeyTokens::FastEffects) });
        const auto style = MaterialTheme::typography().titleMedium;
        c.Provide(LocalMinimumInteractiveComponentSize, Dp(0), [&] {
            Surface(click, animated, surface, [=] {
                auto row = ui::Modifier{}.With<WidthNode>(WidthParams{ progress, scope.horizontalAlignment });
                row = SizeIn(row, FabMenuBaselineTokens::ListItemContainerHeight, FabMenuBaselineTokens::ListItemContainerHeight, Dp::Unspecified(), Dp::Unspecified());
                row = Padding(row, PaddingValues{ FabMenuBaselineTokens::ListItemLeadingSpace, Dp(0), FabMenuBaselineTokens::ListItemTrailingSpace, Dp(0) });
                Row(row, Arrangement::SpacedBy(FabMenuBaselineTokens::ListItemIconLabelSpace, ui::AlignmentH::CenterHorizontally()), ui::AlignmentV::CenterVertically(), [=] {
                    icon();
                    rt::Composition::Current()->Provide(LocalTextStyle(), style, text);
                });
            });
        });
    }
}
