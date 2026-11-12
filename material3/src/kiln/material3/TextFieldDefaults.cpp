#include "kiln/material3/TextFieldDefaults.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Compose.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/ColorSchemeKeyTokens.h"
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

    // tonalTextFieldColors (no tokens yet: b/448727879).
    TextFieldColors TextFieldDefaults::tonalColors() {
        namespace ColorSchemeKeyTokens = tokens::ColorSchemeKeyTokens;
        namespace T = tokens::FilledTextFieldTokens;
        const Color onSurface = Value(ColorSchemeKeyTokens::OnSurface), onVariant = Value(ColorSchemeKeyTokens::OnSurfaceVariant),
                    onBackground = Value(ColorSchemeKeyTokens::OnBackground), container = Value(ColorSchemeKeyTokens::SurfaceContainer),
                    error = Value(ColorSchemeKeyTokens::Error);
        TextFieldColors c{};
        c.unfocusedContainerColor = container;
        c.unfocusedIndicatorColor = Color::Transparent();
        c.unfocusedTextColor = onSurface;
        c.unfocusedLabelColor = c.unfocusedLeadingIconColor = c.unfocusedTrailingIconColor = onVariant;
        c.unfocusedSupportingTextColor = onBackground;
        c.unfocusedPlaceholderColor = c.unfocusedPrefixColor = c.unfocusedSuffixColor = onVariant;
        c.focusedContainerColor = container;
        c.focusedIndicatorColor = Color::Transparent();
        c.focusedTextColor = onBackground;
        c.focusedLabelColor = c.focusedLeadingIconColor = c.focusedTrailingIconColor = onVariant;
        c.focusedSupportingTextColor = onBackground;
        c.focusedPlaceholderColor = onVariant;
        c.focusedPrefixColor = c.focusedSuffixColor = onBackground;
        c.disabledContainerColor = container.WithAlpha(T::DisabledInputOpacity);
        c.disabledIndicatorColor = Color::Transparent();
        c.disabledTextColor = onSurface.WithAlpha(T::DisabledInputOpacity);
        c.disabledLabelColor = onVariant.WithAlpha(T::DisabledLabelOpacity);
        c.disabledLeadingIconColor = onVariant.WithAlpha(T::DisabledLeadingIconOpacity);
        c.disabledTrailingIconColor = onVariant.WithAlpha(T::DisabledTrailingIconOpacity);
        c.disabledSupportingTextColor = onBackground.WithAlpha(T::DisabledSupportingOpacity);
        c.disabledPlaceholderColor = c.disabledPrefixColor = c.disabledSuffixColor = onVariant.WithAlpha(T::DisabledInputOpacity);
        c.errorContainerColor = Value(ColorSchemeKeyTokens::ErrorContainer);
        c.errorIndicatorColor = Color::Transparent();
        c.errorTextColor = onBackground;
        c.errorLabelColor = error;
        c.errorLeadingIconColor = onVariant;
        c.errorTrailingIconColor = c.errorCursorColor = c.errorSupportingTextColor = error;
        c.errorPlaceholderColor = c.errorPrefixColor = c.errorSuffixColor = onVariant;
        c.cursorColor = Value(ColorSchemeKeyTokens::Primary);
        c.textSelectionColors = CurrentSelectionColors();
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

    namespace {
        // TextFieldContainerNode (textFieldContainer, NodeTransition): the container colour
        // (FastEffects), border colour (FastEffects) and border width (FastSpatial) animate in the
        // node, so a focus change redraws without recomposing. The border snaps while disabled.
        struct ContainerParams {
            bool enabled, isError;
            std::shared_ptr<MutableInteractionSource> source;
            TextFieldColors colors;
            ShapePtr shape;
            std::optional<Dp> focusedBorder, unfocusedBorder;   // both unset: no border
            anim::SpecPtr effectsSpec, spatialSpec;
            bool operator==(const ContainerParams& o) const {
                return enabled == o.enabled && isError == o.isError && source == o.source && colors == o.colors && shape == o.shape &&
                       focusedBorder == o.focusedBorder && unfocusedBorder == o.unfocusedBorder;
            }
        };

        // A colour animating as a lerp from where it was to its target (Animatable<Color>).
        struct AnimatedColor {
            Color from, to;
            anim::Animatable progress{ 1.0f };
            rt::Job job;
            Color Value() const { return ui::Lerp(from, to, progress.Float()); }
            void Retarget(Color target, const anim::SpecPtr& spec) {
                if (target == to) return;
                from = Value();
                to = target;
                if (!spec) { job.Cancel(); progress.SnapTo(1.0f); from = to; return; }
                progress.SnapTo(0.0f);
                job = rt::Launch(rt::Dispatcher::Main(), AnimateIndicator(&progress, { 1, 0, 0, 0 }, spec));
            }
        };

        struct TextFieldContainerNode final : ui::ModifierNode, ui::DrawModifier {
            ContainerParams params;
            bool focused = false;
            AnimatedColor container, border;
            anim::Animatable borderWidth{ 0.0f };
            float widthTarget = 0;
            rt::Job widthJob;
            BorderRenderer renderer;
            explicit TextFieldContainerNode(ContainerParams p) : params(std::move(p)) {
                rt::Observation::Untracked([&] { focused = params.source && params.source->IsFocused(); });
                container.from = container.to = ContainerTarget();
                border.from = border.to = BorderTarget();
                widthTarget = WidthTarget();
                borderWidth.SnapTo(widthTarget);
            }
            ui::DrawModifier* AsDraw() override { return this; }
            void OnDetach() override { container.job.Cancel(); border.job.Cancel(); widthJob.Cancel(); }
            void OnParamsChanged() { Update(); InvalidateDraw(); }
            bool HasBorder() const { return params.focusedBorder || params.unfocusedBorder; }
            Color ContainerTarget() const { return params.colors.ContainerColor(params.enabled, params.isError, focused); }
            Color BorderTarget() const { return params.colors.IndicatorColor(params.enabled, params.isError, focused); }
            float WidthTarget() const {
                const std::optional<Dp>& w = focused && params.enabled ? params.focusedBorder : params.unfocusedBorder;
                return w ? w->value : 0.0f;
            }
            void Update() {
                rt::Observation::Untracked([&] {
                    container.Retarget(ContainerTarget(), params.effectsSpec);
                    border.Retarget(BorderTarget(), params.enabled ? params.effectsSpec : nullptr);
                    const float w = WidthTarget();
                    if (w == widthTarget) return;
                    widthTarget = w;
                    if (params.enabled) widthJob = rt::Launch(rt::Dispatcher::Main(), AnimateIndicator(&borderWidth, { w, 0, 0, 0 }, params.spatialSpec));
                    else { widthJob.Cancel(); borderWidth.SnapTo(w); }
                });
            }
            void Draw(ui::ContentDrawScope& s) override {
                // The source's focus, read while drawing (InteractionTracker).
                const bool nowFocused = params.source && params.source->IsFocused();
                if (nowFocused != focused) {
                    focused = nowFocused;
                    Update();
                }
                const gfx::Outline outline = params.shape->CreateOutline(s.GetSize(), s.Direction(), s);
                s.DrawPath(outline.ToPath(), container.Value());
                s.DrawContent();
                if (HasBorder()) renderer.DrawBorder(s, Dp(borderWidth.Float()), gfx::Brush::SolidColor(border.Value()), outline);
            }
        };

        ui::Modifier TextFieldContainer(ui::Modifier m, bool enabled, bool isError, std::shared_ptr<MutableInteractionSource> source, const TextFieldColors& colors,
                                        ShapePtr shape, std::optional<Dp> focusedBorder = std::nullopt, std::optional<Dp> unfocusedBorder = std::nullopt) {
            return m.With<TextFieldContainerNode>(ContainerParams{ enabled, isError, std::move(source), colors, std::move(shape), focusedBorder, unfocusedBorder,
                                                                   Value(tokens::MotionSchemeKeyTokens::FastEffects),
                                                                   Value(tokens::MotionSchemeKeyTokens::FastSpatial) });
        }
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
        Box(TextFieldDefaults::IndicatorLine(TextFieldContainer(modifier, enabled, isError, source, c, shape), enabled, isError, source, c, shape,
                                             focusedThickness, unfocusedThickness));
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

    // tonalOutlinedTextFieldColors (no tokens yet: b/448727879).
    TextFieldColors OutlinedTextFieldDefaults::tonalColors() {
        namespace ColorSchemeKeyTokens = tokens::ColorSchemeKeyTokens;
        namespace T = tokens::OutlinedTextFieldTokens;
        const Color onSurface = Value(ColorSchemeKeyTokens::OnSurface), onVariant = Value(ColorSchemeKeyTokens::OnSurfaceVariant),
                    onBackground = Value(ColorSchemeKeyTokens::OnBackground), outline = Value(ColorSchemeKeyTokens::OutlineVariant),
                    error = Value(ColorSchemeKeyTokens::Error);
        TextFieldColors c{};
        c.focusedContainerColor = c.unfocusedContainerColor = c.disabledContainerColor = c.errorContainerColor = Color::Transparent();
        c.unfocusedIndicatorColor = c.focusedIndicatorColor = c.disabledIndicatorColor = outline;
        c.unfocusedTextColor = onSurface;
        c.unfocusedLabelColor = c.unfocusedLeadingIconColor = c.unfocusedTrailingIconColor = onVariant;
        c.unfocusedSupportingTextColor = onBackground;
        c.unfocusedPlaceholderColor = c.unfocusedPrefixColor = c.unfocusedSuffixColor = onVariant;
        c.focusedTextColor = onBackground;
        c.focusedLabelColor = c.focusedLeadingIconColor = c.focusedTrailingIconColor = onVariant;
        c.focusedSupportingTextColor = onBackground;
        c.focusedPlaceholderColor = onVariant;
        c.focusedPrefixColor = c.focusedSuffixColor = onBackground;
        c.disabledTextColor = onSurface.WithAlpha(T::DisabledInputOpacity);
        c.disabledLabelColor = onVariant.WithAlpha(T::DisabledLabelOpacity);
        c.disabledLeadingIconColor = onVariant.WithAlpha(T::DisabledLeadingIconOpacity);
        c.disabledTrailingIconColor = onVariant.WithAlpha(T::DisabledTrailingIconOpacity);
        c.disabledSupportingTextColor = onBackground.WithAlpha(T::DisabledSupportingOpacity);
        c.disabledPlaceholderColor = c.disabledPrefixColor = c.disabledSuffixColor = onVariant.WithAlpha(T::DisabledInputOpacity);
        c.errorIndicatorColor = error;
        c.errorTextColor = onBackground;
        c.errorLabelColor = error;
        c.errorLeadingIconColor = onVariant;
        c.errorTrailingIconColor = c.errorCursorColor = c.errorSupportingTextColor = error;
        c.errorPlaceholderColor = c.errorPrefixColor = c.errorSuffixColor = onVariant;
        c.cursorColor = Value(ColorSchemeKeyTokens::Primary);
        c.textSelectionColors = CurrentSelectionColors();
        return c;
    }

    void OutlinedTextFieldDefaults::Container(bool enabled, bool isError, std::shared_ptr<MutableInteractionSource> source, const ui::Modifier& modifier,
                                              std::optional<TextFieldColors> colors, ShapePtr shape, Dp focusedThickness, Dp unfocusedThickness) {
        const TextFieldColors c = colors ? *colors : OutlinedTextFieldDefaults::colors();
        if (!shape) shape = OutlinedTextFieldDefaults::shape();
        Box(TextFieldContainer(modifier, enabled, isError, source, c, shape, focusedThickness, unfocusedThickness));
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
