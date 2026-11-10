#include "kiln/material3/SegmentedButton.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/OutlinedSegmentedButtonTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using namespace tokens;

    namespace {
        using Content = std::function<void()>;
        struct ZParams {
            bool active;
            std::shared_ptr<rt::State<int>> interactions;
            bool operator==(const ZParams&) const = default;
        };
        struct InteractionZNode final : ModifierNode, LayoutModifier {
            ZParams params;
            explicit InteractionZNode(ZParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& child, const Constraints& c) override {
                auto* p = &child.Measure(c);
                const auto state = params;
                return s.Layout(p->Width(), p->Height(), [p, state](PlacementScope& scope) {
                    scope.Place(*p, 0, 0, float(state.interactions->Get()) + (state.active ? 5.0f : 0.0f));
                });
            }
        };
        std::shared_ptr<rt::State<int>> InteractionCount(const std::shared_ptr<MutableInteractionSource>& source) {
            auto& c = *rt::Composition::Current();
            auto count = c.Remember<std::shared_ptr<rt::State<int>>>([] { return std::make_shared<rt::State<int>>(0); });
            c.DisposableEffect(rt::Keys(std::uintptr_t(source.get())), [=]() -> Content {
                const auto token = source->Subscribe([count](const Interaction& interaction) {
                    if (interaction.kind == Interaction::Kind::Press || interaction.kind == Interaction::Kind::Focus)
                        count->Set(count->Peek() + (interaction.phase == Interaction::Phase::Start ? 1 : -1));
                });
                return [source, token] { source->Unsubscribe(token); };
            });
            return count;
        }
        // The two Layout contents are independent lists, not Rows: each node in a slot is
        // measured with the same constraints and stacked, as in MultiContentMeasurePolicy.
        class SlotPolicy final : public MeasurePolicy {
        public:
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) override {
                std::vector<Placeable*> placeables;
                int width = 0, height = 0;
                for (auto* child : children) {
                    auto* p = &child->Measure(c);
                    placeables.push_back(p);
                    width = std::max(width, p->Width()); height = std::max(height, p->Height());
                }
                return s.Layout(width, height, [placeables, height](PlacementScope& place) {
                    for (auto* p : placeables) place.Place(*p, 0, (height - p->Height()) / 2);
                });
            }
        };
        rt::Task<> AnimateOffset(anim::Animatable* value, float target, anim::SpecPtr spec) {
            co_await value->AnimateTo(target, std::move(spec));
        }
        class ContentPolicy final : public MeasurePolicy {
        public:
            ContentPolicy(rt::Dispatcher& dispatcher, anim::SpecPtr spec) : m_Dispatcher(dispatcher), m_Spec(anim::WithSpringVisibilityThreshold(std::move(spec), 1)) {}
            void Cancel() { m_Job.Cancel(); }
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) override {
                auto* icon = &children[0]->Measure(c);
                auto* label = &children[1]->Measure(c);
                const int leading = s.RoundToPx(SegmentedButtonDefaults::IconSize) + s.RoundToPx(Dp(8));
                const int offset = icon->Width() == 0 ? -leading / 2 : 0;
                if (!m_Initial) m_Initial = offset;
                else {
                    if (!m_Offset) m_Offset = std::make_unique<anim::Animatable>(float(*m_Initial));
                    if (m_Offset->Target()[0] != float(offset)) {
                        m_Job.Cancel();
                        m_Job = rt::Launch(m_Dispatcher, AnimateOffset(m_Offset.get(), float(offset), m_Spec));
                    }
                }
                const int width = std::max(s.RoundToPx(SegmentedButtonDefaults::IconSize), icon->Width()) + s.RoundToPx(Dp(8)) + label->Width();
                const int height = label->Height();
                return s.Layout(width, height, [this, icon, label, leading, offset, height](PlacementScope& place) {
                    // Compose uses absolute placement here even in an RTL row.
                    place.Place(*icon, 0, (height - icon->Height()) / 2);
                    place.Place(*label, leading + (m_Offset ? RoundJava(m_Offset->Float()) : offset), (height - label->Height()) / 2);
                });
            }
            int MinIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) override { return IntrinsicWidth(s, ch, h, false); }
            int MaxIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h) override { return IntrinsicWidth(s, ch, h, true); }
            int MinIntrinsicHeight(MeasureScope&, std::span<Measurable* const> ch, int w) override { return ch[1]->MinIntrinsicHeight(w); }
            int MaxIntrinsicHeight(MeasureScope&, std::span<Measurable* const> ch, int w) override { return ch[1]->MaxIntrinsicHeight(w); }
        private:
            int IntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ch, int h, bool max) {
                return std::max(s.RoundToPx(SegmentedButtonDefaults::IconSize), max ? ch[0]->MaxIntrinsicWidth(h) : ch[0]->MinIntrinsicWidth(h)) +
                       s.RoundToPx(Dp(8)) + (max ? ch[1]->MaxIntrinsicWidth(h) : ch[1]->MinIntrinsicWidth(h));
            }
            rt::Dispatcher& m_Dispatcher;
            anim::SpecPtr m_Spec;
            std::optional<int> m_Initial;
            std::unique_ptr<anim::Animatable> m_Offset;
            rt::Job m_Job; // Cancel before the animatable is destroyed.
        };
        void ButtonContent(Content icon, Content label, PaddingValues padding) {
            Box(Padding({}, padding), Alignment::Center(), false, [=] {
                auto& c = *rt::Composition::Current();
                c.Provide(LocalTextStyle(), Value(OutlinedSegmentedButtonTokens::LabelTextFont), [=, &c] {
                    const auto spec = Value(MotionSchemeKeyTokens::FastSpatial);
                    auto policy = c.Remember<std::shared_ptr<ContentPolicy>>([&c, spec] { return std::make_shared<ContentPolicy>(c.GetDispatcher(), spec); });
                    c.DisposableEffect({}, [policy]() -> Content { return [policy] { policy->Cancel(); }; });
                    Layout(Height({}, IntrinsicSize::Min), policy, [=] {
                        Layout({}, std::make_shared<SlotPolicy>(), icon, rt::Key(0));
                        Layout({}, std::make_shared<SlotPolicy>(), label, rt::Key(1));
                    });
                });
            });
        }
        Modifier ButtonModifier(Modifier m, bool active, const std::shared_ptr<MutableInteractionSource>& source) {
            m = Weight(m, 1);
            m = m.With<InteractionZNode>(ZParams{ active, InteractionCount(source) });
            return DefaultMinSize(m, ButtonDefaults::MinWidth, ButtonDefaults::MinHeight());
        }
        SurfaceOptions Options(bool active, ShapePtr shape, const SegmentedButtonOptions& o) {
            const auto colors = o.colors.value_or(SegmentedButtonDefaults::colors());
            SurfaceOptions result;
            result.shape = std::move(shape); result.color = colors.ContainerColor(o.enabled, active); result.contentColor = colors.ContentColor(o.enabled, active);
            result.border = o.border.value_or(SegmentedButtonDefaults::borderStroke(colors.BorderColor(o.enabled, active)));
            return result;
        }
        Content IconContent(bool active, const SegmentedButtonOptions& o) {
            return o.icon ? *o.icon : Content([active] { SegmentedButtonDefaults::Icon(active); });
        }
        void ButtonRow(bool single, Modifier m, Content content, Dp space) {
            if (single) m = SelectableGroup(m);
            m = Width(DefaultMinSize(m, Dp::Unspecified(), OutlinedSegmentedButtonTokens::ContainerHeight), IntrinsicSize::Min);
            Row(m, Arrangement::SpacedBy(Dp(-space.value)), AlignmentV::CenterVertically(), std::move(content));
        }
    }
    void SingleChoiceSegmentedButtonRow(const Modifier& modifier, Content content, Dp space) { ButtonRow(true, modifier, std::move(content), space); }
    void MultiChoiceSegmentedButtonRow(const Modifier& modifier, Content content, Dp space) { ButtonRow(false, modifier, std::move(content), space); }
    void SegmentedButton(bool selected, Content onClick, ShapePtr shape, Content label, const Modifier& modifier, const SegmentedButtonOptions& o) {
        const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
        const auto m = Semantics(ButtonModifier(modifier, selected, source), [](SemanticsConfiguration& config) { config.role = Role::RadioButton; });
        Surface(selected, std::move(onClick), m, Options(selected, std::move(shape), o),
                [=] { ButtonContent(IconContent(selected, o), label, o.contentPadding.value_or(SegmentedButtonDefaults::ContentPadding)); }, o.enabled, source);
    }
    void SegmentedButton(bool checked, std::function<void(bool)> onChange, ShapePtr shape, Content label, const Modifier& modifier, const SegmentedButtonOptions& o) {
        const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
        Surface(checked, std::move(onChange), ButtonModifier(modifier, checked, source), Options(checked, std::move(shape), o),
                [=] { ButtonContent(IconContent(checked, o), label, o.contentPadding.value_or(SegmentedButtonDefaults::ContentPadding)); }, o.enabled, source);
    }
}
