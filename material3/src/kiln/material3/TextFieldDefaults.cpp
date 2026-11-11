#include "kiln/material3/TextFieldDefaults.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Compose.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/FilledTextFieldTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/OutlinedTextFieldTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"
#include "kiln/ui/ColorSpace.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>

namespace kiln::material3 {
    using namespace foundation;
    using ui::Color;
    using ui::Dp;

    namespace {
        Color Disabled(tokens::ColorToken c, float alpha) { return Value(c).WithAlpha(alpha); }
        TextSelectionColors CurrentSelectionColors() {
            auto* c = rt::Composition::Current();
            return c ? c->Read(LocalTextSelectionColors) : kDefaultTextSelectionColors;
        }
    }

    // --- filled ---------------------------------------------------------------------------------

    ShapePtr TextFieldDefaults::shape() { return Value(tokens::FilledTextFieldTokens::ContainerShape); }
    ShapePtr TextFieldDefaults::roundedShape() { return Value(tokens::ShapeKeyTokens::CornerMedium); }
    PaddingValues TextFieldDefaults::contentPaddingWithLabel(Dp start, Dp end, Dp top, Dp bottom) { return { start, top, end, bottom }; }
    PaddingValues TextFieldDefaults::contentPaddingWithoutLabel(Dp start, Dp top, Dp end, Dp bottom) { return { start, top, end, bottom }; }

    TextFieldColors TextFieldDefaults::colors() {
        namespace T = tokens::FilledTextFieldTokens;
        TextFieldColors c{};
        c.focusedTextColor = Value(T::FocusInputColor);
        c.unfocusedTextColor = Value(T::InputColor);
        c.disabledTextColor = Disabled(T::DisabledInputColor, T::DisabledInputOpacity);
        c.errorTextColor = Value(T::ErrorInputColor);
        c.focusedContainerColor = c.unfocusedContainerColor = c.disabledContainerColor = c.errorContainerColor = Value(T::ContainerColor);
        c.cursorColor = Value(T::CaretColor);
        c.errorCursorColor = Value(T::ErrorFocusCaretColor);
        c.textSelectionColors = CurrentSelectionColors();
        c.focusedIndicatorColor = Value(T::FocusActiveIndicatorColor);
        c.unfocusedIndicatorColor = Value(T::ActiveIndicatorColor);
        c.disabledIndicatorColor = Disabled(T::DisabledActiveIndicatorColor, T::DisabledActiveIndicatorOpacity);
        c.errorIndicatorColor = Value(T::ErrorActiveIndicatorColor);
        c.focusedLeadingIconColor = Value(T::FocusLeadingIconColor);
        c.unfocusedLeadingIconColor = Value(T::LeadingIconColor);
        c.disabledLeadingIconColor = Disabled(T::DisabledLeadingIconColor, T::DisabledLeadingIconOpacity);
        c.errorLeadingIconColor = Value(T::ErrorLeadingIconColor);
        c.focusedTrailingIconColor = Value(T::FocusTrailingIconColor);
        c.unfocusedTrailingIconColor = Value(T::TrailingIconColor);
        c.disabledTrailingIconColor = Disabled(T::DisabledTrailingIconColor, T::DisabledTrailingIconOpacity);
        c.errorTrailingIconColor = Value(T::ErrorTrailingIconColor);
        c.focusedLabelColor = Value(T::FocusLabelColor);
        c.unfocusedLabelColor = Value(T::LabelColor);
        c.disabledLabelColor = Disabled(T::DisabledLabelColor, T::DisabledLabelOpacity);
        c.errorLabelColor = Value(T::ErrorLabelColor);
        c.focusedPlaceholderColor = c.unfocusedPlaceholderColor = c.errorPlaceholderColor = Value(T::InputPlaceholderColor);
        c.disabledPlaceholderColor = Disabled(T::DisabledInputColor, T::DisabledInputOpacity);
        c.focusedSupportingTextColor = Value(T::FocusSupportingColor);
        c.unfocusedSupportingTextColor = Value(T::SupportingColor);
        c.disabledSupportingTextColor = Disabled(T::DisabledSupportingColor, T::DisabledSupportingOpacity);
        c.errorSupportingTextColor = Value(T::ErrorSupportingColor);
        c.focusedPrefixColor = c.unfocusedPrefixColor = c.errorPrefixColor = Value(T::InputPrefixColor);
        c.disabledPrefixColor = Disabled(T::InputPrefixColor, T::DisabledInputOpacity);
        c.focusedSuffixColor = c.unfocusedSuffixColor = c.errorSuffixColor = Value(T::InputSuffixColor);
        c.disabledSuffixColor = Disabled(T::InputSuffixColor, T::DisabledInputOpacity);
        return c;
    }

    namespace {
        // IndicatorLineNode: the colour (FastEffects) and width (FastSpatial) animate towards the
        // state's; disabled fields snap.
        struct IndicatorParams {
            bool enabled, isError;
            std::shared_ptr<MutableInteractionSource> source;
            std::optional<TextFieldColors> colors;
            ShapePtr shape;
            Dp focusedWidth, unfocusedWidth;
            TextFieldColors resolvedColors;   // the theme's when colors is unset
            ShapePtr resolvedShape;
            anim::SpecPtr colorSpec, widthSpec;
            bool operator==(const IndicatorParams& o) const {
                return enabled == o.enabled && isError == o.isError && source == o.source && colors == o.colors && shape == o.shape &&
                       focusedWidth == o.focusedWidth && unfocusedWidth == o.unfocusedWidth && resolvedColors == o.resolvedColors;
            }
        };
        rt::Task<> AnimateIndicator(anim::Animatable* a, anim::Vec target, anim::SpecPtr spec) { co_await a->AnimateTo(target, spec); }

        struct IndicatorLineNode final : ui::ModifierNode, ui::DrawModifier {
            IndicatorParams params;
            bool focused = false;
            // The colour animates as a lerp from where it was (Animatable<Color> in Compose).
            Color from, to;
            anim::Animatable colorProgress{ 1.0f };
            anim::Animatable width{ 0.0f };
            rt::Job colorJob, widthJob;
            explicit IndicatorLineNode(IndicatorParams p) : params(std::move(p)) {
                rt::Observation::Untracked([&] { focused = params.source && params.source->IsFocused(); });
                from = to = params.resolvedColors.IndicatorColor(params.enabled, params.isError, focused);
                width.SnapTo((focused && params.enabled ? params.focusedWidth : params.unfocusedWidth).value);
            }
            ui::DrawModifier* AsDraw() override { return this; }
            void OnDetach() override { colorJob.Cancel(); widthJob.Cancel(); }
            void OnParamsChanged() { Invalidate(); InvalidateDraw(); }
            Color Current() const { return ui::Lerp(from, to, colorProgress.Float()); }
            void Invalidate() {
                rt::Observation::Untracked([&] {
                    const Color target = params.resolvedColors.IndicatorColor(params.enabled, params.isError, focused);
                    const float w = (focused && params.enabled ? params.focusedWidth : params.unfocusedWidth).value;
                    if (target != to) {
                        from = Current();
                        to = target;
                        colorProgress.SnapTo(0.0f);
                    }
                    if (params.enabled) {
                        colorJob = rt::Launch(rt::Dispatcher::Main(), AnimateIndicator(&colorProgress, { 1, 0, 0, 0 }, params.colorSpec));
                        widthJob = rt::Launch(rt::Dispatcher::Main(), AnimateIndicator(&width, { w, 0, 0, 0 }, params.widthSpec));
                    } else {
                        colorJob.Cancel(); widthJob.Cancel();
                        colorProgress.SnapTo(1.0f);
                        width.SnapTo(w);
                    }
                });
            }
            void Draw(ui::ContentDrawScope& s) override {
                // trackFocusState: the source's focus, read while drawing.
                const bool nowFocused = params.source && params.source->IsFocused();
                if (nowFocused != focused) {
                    focused = nowFocused;
                    Invalidate();
                }
                s.DrawContent();
                const ui::Size size = s.GetSize();
                const float stroke = s.ToPx(Dp(width.Float()));
                gfx::Path line;
                line.AddRect({ 0, size.height - stroke, size.width, size.height });
                const gfx::Path shapePath = params.resolvedShape->CreateOutline(size, s.Direction(), s).ToPath();
                s.DrawPath(gfx::Path::Combine(gfx::PathOperation::Intersect, line, shapePath), Current());
            }
        };
    }

    ui::Modifier TextFieldDefaults::IndicatorLine(ui::Modifier m, bool enabled, bool isError, std::shared_ptr<MutableInteractionSource> source,
                                                  std::optional<TextFieldColors> colors, ShapePtr textFieldShape, Dp focused, Dp unfocused) {
        IndicatorParams p{ enabled, isError, std::move(source), colors, textFieldShape, focused, unfocused,
                           colors ? *colors : TextFieldDefaults::colors(), textFieldShape ? textFieldShape : TextFieldDefaults::shape(),
                           Value(tokens::MotionSchemeKeyTokens::FastEffects), Value(tokens::MotionSchemeKeyTokens::FastSpatial) };
        return m.With<IndicatorLineNode>(std::move(p));
    }

    void TextFieldDefaults::Container(bool enabled, bool isError, std::shared_ptr<MutableInteractionSource> source, const ui::Modifier& modifier,
                                      std::optional<TextFieldColors> colors, ShapePtr shape, Dp focusedThickness, Dp unfocusedThickness) {
        const TextFieldColors c = colors ? *colors : TextFieldDefaults::colors();
        if (!shape) shape = TextFieldDefaults::shape();
        const bool focused = source && source->IsFocused();
        const Color container = AnimateColorAsState(c.ContainerColor(enabled, isError, focused), Value(tokens::MotionSchemeKeyTokens::FastEffects));
        Box(TextFieldDefaults::IndicatorLine(internal::TextFieldBackground(modifier, [container] { return container; }, shape), enabled, isError, source, c,
                                             shape, focusedThickness, unfocusedThickness));
    }

    // --- outlined -------------------------------------------------------------------------------

    ShapePtr OutlinedTextFieldDefaults::shape() { return Value(tokens::OutlinedTextFieldTokens::ContainerShape); }
    ShapePtr OutlinedTextFieldDefaults::roundedShape() { return Value(tokens::ShapeKeyTokens::CornerMedium); }
    PaddingValues OutlinedTextFieldDefaults::contentPaddingWithLabel(Dp start, Dp top, Dp end, Dp bottom) { return { start, top, end, bottom }; }
    PaddingValues OutlinedTextFieldDefaults::contentPaddingWithoutLabel(Dp start, Dp top, Dp end, Dp bottom) { return { start, top, end, bottom }; }

    TextFieldColors OutlinedTextFieldDefaults::colors() {
        namespace T = tokens::OutlinedTextFieldTokens;
        TextFieldColors c{};
        c.focusedTextColor = Value(T::FocusInputColor);
        c.unfocusedTextColor = Value(T::InputColor);
        c.disabledTextColor = Disabled(T::DisabledInputColor, T::DisabledInputOpacity);
        c.errorTextColor = Value(T::ErrorInputColor);
        c.focusedContainerColor = c.unfocusedContainerColor = c.disabledContainerColor = c.errorContainerColor = Color::Transparent();
        c.cursorColor = Value(T::CaretColor);
        c.errorCursorColor = Value(T::ErrorFocusCaretColor);
        c.textSelectionColors = CurrentSelectionColors();
        c.focusedIndicatorColor = Value(T::FocusOutlineColor);
        c.unfocusedIndicatorColor = Value(T::OutlineColor);
        c.disabledIndicatorColor = Disabled(T::DisabledOutlineColor, T::DisabledOutlineOpacity);
        c.errorIndicatorColor = Value(T::ErrorOutlineColor);
        c.focusedLeadingIconColor = Value(T::FocusLeadingIconColor);
        c.unfocusedLeadingIconColor = Value(T::LeadingIconColor);
        c.disabledLeadingIconColor = Disabled(T::DisabledLeadingIconColor, T::DisabledLeadingIconOpacity);
        c.errorLeadingIconColor = Value(T::ErrorLeadingIconColor);
        c.focusedTrailingIconColor = Value(T::FocusTrailingIconColor);
        c.unfocusedTrailingIconColor = Value(T::TrailingIconColor);
        c.disabledTrailingIconColor = Disabled(T::DisabledTrailingIconColor, T::DisabledTrailingIconOpacity);
        c.errorTrailingIconColor = Value(T::ErrorTrailingIconColor);
        c.focusedLabelColor = Value(T::FocusLabelColor);
        c.unfocusedLabelColor = Value(T::LabelColor);
        c.disabledLabelColor = Disabled(T::DisabledLabelColor, T::DisabledLabelOpacity);
        c.errorLabelColor = Value(T::ErrorLabelColor);
        c.focusedPlaceholderColor = c.unfocusedPlaceholderColor = c.errorPlaceholderColor = Value(T::InputPlaceholderColor);
        c.disabledPlaceholderColor = Disabled(T::DisabledInputColor, T::DisabledInputOpacity);
        c.focusedSupportingTextColor = Value(T::FocusSupportingColor);
        c.unfocusedSupportingTextColor = Value(T::SupportingColor);
        c.disabledSupportingTextColor = Disabled(T::DisabledSupportingColor, T::DisabledSupportingOpacity);
        c.errorSupportingTextColor = Value(T::ErrorSupportingColor);
        c.focusedPrefixColor = c.unfocusedPrefixColor = c.errorPrefixColor = Value(T::InputPrefixColor);
        c.disabledPrefixColor = Disabled(T::InputPrefixColor, T::DisabledInputOpacity);
        c.focusedSuffixColor = c.unfocusedSuffixColor = c.errorSuffixColor = Value(T::InputSuffixColor);
        c.disabledSuffixColor = Disabled(T::InputSuffixColor, T::DisabledInputOpacity);
        return c;
    }

    void OutlinedTextFieldDefaults::Container(bool enabled, bool isError, std::shared_ptr<MutableInteractionSource> source, const ui::Modifier& modifier,
                                              std::optional<TextFieldColors> colors, ShapePtr shape, Dp focusedThickness, Dp unfocusedThickness) {
        const TextFieldColors c = colors ? *colors : OutlinedTextFieldDefaults::colors();
        if (!shape) shape = OutlinedTextFieldDefaults::shape();
        const bool focused = source && source->IsFocused();
        // animateBorderStrokeAsState: animated while enabled, the state's own value otherwise.
        const Color target = c.IndicatorColor(enabled, isError, focused);
        const Color borderColor = enabled ? AnimateColorAsState(target, Value(tokens::MotionSchemeKeyTokens::FastEffects)) : target;
        const Dp thickness = enabled ? AnimateDpAsState(focused ? focusedThickness : unfocusedThickness, Value(tokens::MotionSchemeKeyTokens::FastSpatial))
                                     : unfocusedThickness;
        const Color container = AnimateColorAsState(c.ContainerColor(enabled, isError, focused), Value(tokens::MotionSchemeKeyTokens::FastEffects));
        Box(internal::TextFieldBackground(Border(modifier, BorderStroke{ thickness, gfx::Brush::SolidColor(borderColor) }, shape), [container] { return container; },
                                          shape));
    }

    // --- shared ---------------------------------------------------------------------------------

    namespace {
        struct BackgroundParams {
            std::function<Color()> color;
            ShapePtr shape;
            bool operator==(const BackgroundParams& o) const { return false && o.shape == shape; }   // lambdas never compare equal
        };
        struct BackgroundNode final : ui::ModifierNode, ui::DrawModifier {
            BackgroundParams params;
            explicit BackgroundNode(BackgroundParams p) : params(std::move(p)) {}
            ui::DrawModifier* AsDraw() override { return this; }
            void OnParamsChanged() { InvalidateDraw(); }
            void Draw(ui::ContentDrawScope& s) override {
                const gfx::Outline outline = params.shape->CreateOutline(s.GetSize(), s.Direction(), s);
                s.DrawPath(outline.ToPath(), params.color());
                s.DrawContent();
            }
        };

        struct CutoutParams {
            std::function<ui::Size()> labelSize;
            ui::AlignmentH alignment;
            PaddingValues padding;
            bool operator==(const CutoutParams&) const { return false; }
        };
        struct CutoutNode final : ui::ModifierNode, ui::DrawModifier {
            CutoutParams params;
            explicit CutoutNode(CutoutParams p) : params(std::move(p)) {}
            ui::DrawModifier* AsDraw() override { return this; }
            void OnParamsChanged() { InvalidateDraw(); }
            void Draw(ui::ContentDrawScope& s) override {
                const ui::Size label = params.labelSize();
                if (label.width <= 0) {
                    s.DrawContent();
                    return;
                }
                const ui::Size size = s.GetSize();
                const bool ltr = s.Direction() == ui::LayoutDirection::Ltr;
                const auto& p = params.padding;
                const float leftPadding = s.ToPx(p.rtlAware && !ltr ? p.end : p.start);
                const float rightPadding = s.ToPx(p.rtlAware && !ltr ? p.start : p.end);
                const float innerPadding = s.ToPx(Dp(4));   // OutlinedTextFieldInnerPadding
                const float labelCenter = float(params.alignment.Align(int(std::floor(label.width + 0.5f)),
                                                                       int(std::floor(size.width - leftPadding - rightPadding + 0.5f)), s.Direction())) +
                                          leftPadding + label.width / 2;
                const float left = std::max(labelCenter - label.width / 2 - innerPadding, 0.0f);
                const float right = std::min(labelCenter + label.width / 2 + innerPadding, size.width);
                // clipRect(..., ClipOp.Difference): everything but the label's gap (its full height,
                // so no hairline of the border is left).
                const float margin = label.height + size.height;
                gfx::Path clip;
                clip.fillType = gfx::FillType::EvenOdd;
                clip.AddRect({ -margin, -margin, size.width + margin, size.height + margin });
                clip.AddRect({ left, -label.height / 2, right, label.height / 2 });
                s.Save();
                s.BeginLayer({ -margin, -margin, size.width + margin, size.height + margin }, 1, &clip);
                s.DrawContent();
                s.EndLayer();
                s.Restore();
            }
        };
    }

    ui::Modifier internal::TextFieldBackground(ui::Modifier m, std::function<Color()> color, ShapePtr shape) {
        return m.With<BackgroundNode>(BackgroundParams{ std::move(color), std::move(shape) });
    }
    ui::Modifier internal::OutlineCutout(ui::Modifier m, std::function<ui::Size()> labelSize, ui::AlignmentH alignment, PaddingValues padding) {
        return m.With<CutoutNode>(CutoutParams{ std::move(labelSize), alignment, padding });
    }
    PaddingValues internal::SupportingTextPadding() { return { Dp(16), Dp(4), Dp(16), Dp(0) }; }
    PaddingValues internal::DefaultContentPadding(bool hasLabel, const TextFieldLabelPosition& position) {
        return hasLabel && position.kind == TextFieldLabelPosition::Kind::Inside ? TextFieldDefaults::contentPaddingWithLabel()
                                                                                 : TextFieldDefaults::contentPaddingWithoutLabel();
    }

}
