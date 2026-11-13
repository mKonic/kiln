#include "kiln/material3/FloatingActionButton.h"
#include <kiln/Function.h>

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/tokens/ExtendedFabLargeTokens.h"
#include "kiln/material3/tokens/ExtendedFabMediumTokens.h"
#include "kiln/material3/tokens/ExtendedFabPrimaryTokens.h"
#include "kiln/material3/tokens/ExtendedFabSmallTokens.h"
#include "kiln/material3/tokens/FabBaselineTokens.h"
#include "kiln/material3/tokens/FabLargeTokens.h"
#include "kiln/material3/tokens/FabMediumTokens.h"
#include "kiln/material3/tokens/FabPrimaryContainerTokens.h"
#include "kiln/material3/tokens/FabSmallTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/TypographyKeyTokens.h"

namespace kiln::material3 {
    using namespace tokens;
    using namespace foundation;
    using ui::Dp;

    namespace {
        enum class FabSize { Baseline, Small, Medium, Large };
        struct Metrics {
            Dp width, height, start, end, iconGap;
            ShapePtr shape;
            TypographyToken font;
        };
        Metrics IconMetrics(FabSize size) {
            switch (size) {
                case FabSize::Small: return { FabSmallTokens::ContainerWidth, FabSmallTokens::ContainerHeight, {}, {}, {},
                                              FloatingActionButtonDefaults::smallShape(), ExtendedFabPrimaryTokens::LabelTextFont };
                case FabSize::Medium: return { FabMediumTokens::ContainerWidth, FabMediumTokens::ContainerHeight, {}, {}, {},
                                               FloatingActionButtonDefaults::mediumShape(), ExtendedFabPrimaryTokens::LabelTextFont };
                case FabSize::Large: return { FabLargeTokens::ContainerWidth, FabLargeTokens::ContainerHeight, {}, {}, {},
                                              FloatingActionButtonDefaults::largeShape(), ExtendedFabPrimaryTokens::LabelTextFont };
                default: return { FabBaselineTokens::ContainerWidth, FabBaselineTokens::ContainerHeight, {}, {}, {},
                                  FloatingActionButtonDefaults::shape(), ExtendedFabPrimaryTokens::LabelTextFont };
            }
        }
        Metrics ExtendedMetrics(FabSize size) {
            switch (size) {
                case FabSize::Small: return { ExtendedFabSmallTokens::ContainerHeight, ExtendedFabSmallTokens::ContainerHeight,
                    ExtendedFabSmallTokens::LeadingSpace, ExtendedFabSmallTokens::TrailingSpace, ExtendedFabSmallTokens::IconLabelSpace,
                    FloatingActionButtonDefaults::smallExtendedFabShape(), TypographyKeyTokens::TitleMedium };
                case FabSize::Medium: return { ExtendedFabMediumTokens::ContainerHeight, ExtendedFabMediumTokens::ContainerHeight,
                    ExtendedFabMediumTokens::LeadingSpace, ExtendedFabMediumTokens::TrailingSpace, Dp(12),
                    FloatingActionButtonDefaults::mediumExtendedFabShape(), TypographyKeyTokens::TitleLarge };
                case FabSize::Large: return { ExtendedFabLargeTokens::ContainerHeight, ExtendedFabLargeTokens::ContainerHeight,
                    ExtendedFabLargeTokens::LeadingSpace, ExtendedFabLargeTokens::TrailingSpace, Dp(16),
                    FloatingActionButtonDefaults::largeExtendedFabShape(), TypographyKeyTokens::HeadlineSmall };
                default: return { Dp(80), FabBaselineTokens::ContainerHeight, Dp(16), Dp(20), Dp(12),
                    FloatingActionButtonDefaults::extendedFabShape(), ExtendedFabPrimaryTokens::LabelTextFont };
            }
        }
        void Fab(kiln::Function<void()> click, const ui::Modifier& m, const FloatingActionButtonOptions& o,
                 const Metrics& metrics, kiln::Function<void()> content) {
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const auto elevation = o.elevation.value_or(FloatingActionButtonDefaults::elevation());
            SurfaceOptions surface;
            surface.shape = o.shape ? o.shape : metrics.shape;
            surface.color = o.containerColor.value_or(FloatingActionButtonDefaults::containerColor());
            surface.contentColor = o.contentColor ? *o.contentColor : ContentColorFor(*surface.color);
            surface.tonalElevation = elevation.TonalElevation();
            surface.shadowElevation = ShadowElevation(elevation, source);
            const auto modifier = Semantics(m, [](ui::SemanticsConfiguration& c) { c.role = ui::Role::Button; });
            const auto foreground = *surface.contentColor;
            const auto style = Value(metrics.font);
            Surface(std::move(click), modifier, surface, [=] {
                internal::ProvideContentColorTextStyle(foreground, style, [=] {
                    Box(DefaultMinSize({}, metrics.width, metrics.height), ui::Alignment::Center(), false, content);
                });
            }, true, source);
        }
        void IconFab(kiln::Function<void()> click, const ui::Modifier& m, const FloatingActionButtonOptions& o,
                     kiln::Function<void()> content, FabSize size) {
            auto metrics = IconMetrics(size);
            auto modifier = m;
            if (size != FabSize::Baseline) {
                modifier = SizeIn(modifier, metrics.width, metrics.height, Dp::Unspecified(), Dp::Unspecified());
                metrics.width = FabBaselineTokens::ContainerWidth;
                metrics.height = FabBaselineTokens::ContainerHeight;
            }
            Fab(std::move(click), modifier, o, metrics, std::move(content));
        }
        void TextFab(kiln::Function<void()> click, const ui::Modifier& m, const FloatingActionButtonOptions& o,
                     kiln::Function<void()> content, FabSize size) {
            auto metrics = ExtendedMetrics(size);
            const auto rowMetrics = metrics;
            if (size == FabSize::Baseline) metrics.width = FabBaselineTokens::ContainerWidth;
            Fab(std::move(click), m, o, metrics, [=] {
                ui::Modifier row;
                if (size == FabSize::Baseline) row = SizeIn(row, rowMetrics.width, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified());
                const Dp start = size == FabSize::Baseline ? Dp(20) : rowMetrics.start;
                Row(Padding(row, PaddingValues{ start, Dp(0), rowMetrics.end, Dp(0) }), Arrangement::Center(), ui::AlignmentV::CenterVertically(), content);
            });
        }
        struct WidthParams { Dp minWidth; float progress; bool operator==(const WidthParams&) const = default; };
        struct WidthNode final : ui::ModifierNode, ui::LayoutModifier {
            WidthParams params;
            explicit WidthNode(WidthParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                const int expanded = m.MaxIntrinsicWidth(c.maxHeight), collapsed = s.RoundToPx(params.minWidth);
                const int width = ui::RoundJava(collapsed + (expanded - collapsed) * params.progress);
                auto& p = m.Measure(c);
                return s.Layout(width, p.Height(), [&p](ui::PlacementScope& ps) { ps.Place(p, 0, 0); });
            }
        };
        void ExpandableFab(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> click,
                           const ui::Modifier& m, bool expanded, const FloatingActionButtonOptions& o, FabSize size) {
            const auto metrics = ExtendedMetrics(size);
            auto outer = metrics;
            outer.width = size == FabSize::Baseline ? FabBaselineTokens::ContainerWidth : Dp::Unspecified();
            if (size != FabSize::Baseline) outer.height = Dp::Unspecified();
            Fab(std::move(click), m, o, outer, [=] {
                if (size == FabSize::Baseline) {
                    const auto enter = FadeIn(Value(MotionSchemeKeyTokens::DefaultEffects)) +
                        ExpandHorizontally(Value(MotionSchemeKeyTokens::FastSpatial), ui::AlignmentH::Start());
                    const auto exit = FadeOut(Value(MotionSchemeKeyTokens::FastEffects)) +
                        ShrinkHorizontally(Value(MotionSchemeKeyTokens::DefaultSpatial), ui::AlignmentH::Start());
                    Row(Padding(SizeIn({}, expanded ? metrics.width : FabBaselineTokens::ContainerWidth, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified()),
                                PaddingValues{ expanded ? metrics.start : Dp(0), Dp(0), expanded ? metrics.end : Dp(0), Dp(0) }),
                        expanded ? Arrangement::Start() : Arrangement::Center(), ui::AlignmentV::CenterVertically(), [=] {
                        icon();
                        AnimatedVisibilityInRow(expanded, {}, [=] {
                            Row(ClearAndSetSemantics({}, [](ui::SemanticsConfiguration&) {}), [=] {
                                Spacer(Width({}, metrics.iconGap)); text();
                            });
                        }, enter, exit);
                    });
                } else {
                    auto& t = UpdateTransition<float>(expanded ? 1.0f : 0.0f);
                    const auto spatial = Value(MotionSchemeKeyTokens::FastSpatial), effects = Value(MotionSchemeKeyTokens::FastEffects);
                    const float progress = AnimateFloat<float>(t, [](float x) { return x; }, [spatial](const auto&) { return spatial; });
                    const float alpha = AnimateFloat<float>(t, [](float x) { return x; }, [effects](const auto&) { return effects; });
                    const bool collapsed = t.CurrentState() == 0 && !t.IsRunning();
                    auto row = ui::Modifier{}.With<WidthNode>(WidthParams{ metrics.width, progress });
                    row = Padding(SizeIn(row, metrics.width, metrics.height, Dp::Unspecified(), Dp::Unspecified()),
                                  PaddingValues{ metrics.start, Dp(0), metrics.end, Dp(0) });
                    Row(row, Arrangement::Start(), ui::AlignmentV::CenterVertically(), [=] {
                        icon();
                        if (!collapsed) {
                            Row(Alpha(ClearAndSetSemantics({}, [](ui::SemanticsConfiguration&) {}), alpha), [=] {
                                Spacer(Width({}, metrics.iconGap)); text();
                            });
                        }
                    });
                }
            });
        }
    }

    void FloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { IconFab(std::move(c), m, o, std::move(body), FabSize::Baseline); }
    void SmallFloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { IconFab(std::move(c), m, o, std::move(body), FabSize::Small); }
    void MediumFloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { IconFab(std::move(c), m, o, std::move(body), FabSize::Medium); }
    void LargeFloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { IconFab(std::move(c), m, o, std::move(body), FabSize::Large); }
    void ExtendedFloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { TextFab(std::move(c), m, o, std::move(body), FabSize::Baseline); }
    void SmallExtendedFloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { TextFab(std::move(c), m, o, std::move(body), FabSize::Small); }
    void MediumExtendedFloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { TextFab(std::move(c), m, o, std::move(body), FabSize::Medium); }
    void LargeExtendedFloatingActionButton(kiln::Function<void()> c, const ui::Modifier& m, const FloatingActionButtonOptions& o, kiln::Function<void()> body) { TextFab(std::move(c), m, o, std::move(body), FabSize::Large); }
    void ExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> c, const ui::Modifier& m, bool expanded, const FloatingActionButtonOptions& o) { ExpandableFab(std::move(text), std::move(icon), std::move(c), m, expanded, o, FabSize::Baseline); }
    void SmallExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> c, const ui::Modifier& m, bool expanded, const FloatingActionButtonOptions& o) { ExpandableFab(std::move(text), std::move(icon), std::move(c), m, expanded, o, FabSize::Small); }
    void MediumExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> c, const ui::Modifier& m, bool expanded, const FloatingActionButtonOptions& o) { ExpandableFab(std::move(text), std::move(icon), std::move(c), m, expanded, o, FabSize::Medium); }
    void LargeExtendedFloatingActionButton(kiln::Function<void()> text, kiln::Function<void()> icon, kiln::Function<void()> c, const ui::Modifier& m, bool expanded, const FloatingActionButtonOptions& o) { ExpandableFab(std::move(text), std::move(icon), std::move(c), m, expanded, o, FabSize::Large); }

    namespace FloatingActionButtonDefaults {
        const Dp MediumIconSize = FabMediumTokens::IconSize;
        ShapePtr shape() { return Value(FabBaselineTokens::ContainerShape); }
        ShapePtr smallShape() { return Value(FabSmallTokens::ContainerShape); }
        ShapePtr mediumShape() { return ShapeDefaults::LargeIncreased(); }
        ShapePtr largeShape() { return Value(FabLargeTokens::ContainerShape); }
        ShapePtr extendedFabShape() { return Value(ExtendedFabPrimaryTokens::ContainerShape); }
        ShapePtr smallExtendedFabShape() { return Value(ExtendedFabSmallTokens::ContainerShape); }
        ShapePtr mediumExtendedFabShape() { return ShapeDefaults::LargeIncreased(); }
        ShapePtr largeExtendedFabShape() { return Value(ExtendedFabLargeTokens::ContainerShape); }
        ui::Color containerColor() { return Value(FabPrimaryContainerTokens::ContainerColor); }
        FloatingActionButtonElevation elevation() { return { FabPrimaryContainerTokens::ContainerElevation, FabPrimaryContainerTokens::PressedContainerElevation,
                                                            FabPrimaryContainerTokens::FocusedContainerElevation, FabPrimaryContainerTokens::HoveredContainerElevation }; }
        FloatingActionButtonElevation loweredElevation() { return { Dp(1), Dp(1), Dp(1), Dp(3) }; }
        FloatingActionButtonElevation bottomAppBarFabElevation() { return { Dp(0), Dp(0), Dp(0), Dp(0) }; }
    }
}
