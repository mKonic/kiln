#include "kiln/material3/internal/TextFieldDecoration.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/TextFieldDefaults.h"
#include "kiln/material3/internal/TextFieldLayout.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/TypeScaleTokens.h"

#include <algorithm>

namespace kiln::material3::internal {
    using namespace foundation;
    using ui::Color;
    using ui::Dp;

    namespace {
        enum class InputPhase : std::uint8_t { Focused, UnfocusedEmpty, UnfocusedNotEmpty };

        constexpr Dp AboveLabelHorizontalPadding{ 4 }, AboveLabelBottomPadding{ 4 }, PrefixSuffixTextPadding{ 2 }, MinTextLineHeight{ 24 },
            MinFocusedLabelLineHeight{ 16 }, MinSupportingTextLineHeight{ 16 };

        Dp LerpDp(Dp a, Dp b, float t) { return Dp(a.value + (b.value - a.value) * t); }
        Modifier HeightIn(Modifier m, Dp min) { return SizeIn(std::move(m), Dp::Unspecified(), min, Dp::Unspecified(), Dp::Unspecified()); }
        Modifier WrapContentHeight(Modifier m) { return WrapContent(std::move(m), Axis::Vertical, Alignment::CenterStart()); }

        // Decoration: the content colour and the text style merged over the current one.
        void Decoration(Color contentColor, const text::TextStyle* style, const std::function<void()>& content) {
            auto& c = *rt::Composition::Current();
            c.Provide(LocalContentColor, contentColor, [=] {
                if (style) ProvideTextStyle(*style, content);
                else content();
            });
        }

        // textFieldLabelMinHeight: at least this tall, while the constraints allow it.
        Modifier LabelMinHeight(Modifier m, std::function<Dp()> minHeight) {
            return LayoutMod(std::move(m), [minHeight](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
                const Dp min = minHeight();
                ui::Constraints child = c;
                child.minHeight = c.ConstrainHeight(min.Specified() ? s.RoundToPx(min) : 0);
                ui::Placeable& p = measurable.Measure(child);
                return s.Layout(p.Width(), p.Height(), [&p](ui::PlacementScope& place) { place.Place(p, 0, 0); });
            });
        }

        // visualTextSemantics: the visible text for accessibility when a prefix or suffix decorates it.
        Modifier VisualTextSemantics(Modifier m, const std::string& visualText, bool hasAffix) {
            if (!hasAffix || visualText.empty()) return m;
            return Semantics(std::move(m), [visualText](ui::SemanticsConfiguration& c) { c.text = { visualText }; });
        }

        Modifier FadingLayer(Modifier m, std::function<float()> alpha) {
            return GraphicsLayer(std::move(m), [alpha](GraphicsLayerParams& p) { p.alpha = alpha(); });
        }
    }

    Dp TextFieldHorizontalIconPadding() {
        const Dp interactive = rt::Composition::Current()->Read(LocalMinimumInteractiveComponentSize);
        const float size = interactive.Specified() ? interactive.value : 0.0f;
        return Dp(std::max((size - 24.0f) / 2, 0.0f));   // SmallIconButtonTokens.IconSize
    }

    Dp MinimizedLabelHalfHeight() {
        const text::TextUnit lineHeight = MaterialTheme::typography().bodySmall.paragraph.lineHeight;
        const float sp = lineHeight.IsSp() ? lineHeight.value : tokens::TypeScaleTokens::BodySmallLineHeight.value;
        const ui::Density d = rt::Composition::Current()->Read(ui::LocalDensity);
        return Dp(ui::FontScaleSpToDp(d.fontScale, sp) / 2);
    }

    Modifier TopPaddingForLabelCutout(Modifier m, bool hasLabel, const TextFieldLabelPosition& position) {
        if (!hasLabel || position.kind != TextFieldLabelPosition::Kind::Cutout) return m;
        // Merged first, so the padding is part of the text field.
        m = Semantics(std::move(m), [](ui::SemanticsConfiguration&) {}, true);
        return Padding(std::move(m), PaddingValues{ Dp(0), MinimizedLabelHalfHeight(), Dp(0), Dp(0) });
    }

    Modifier DefaultErrorSemantics(Modifier m, bool isError, const std::string& message) {
        if (!isError) return m;
        return Semantics(std::move(m), [message](ui::SemanticsConfiguration& c) { c.error = message; });
    }

    void CommonDecorationBox(const std::string& visualText, const std::function<void()>& innerTextField, const TextFieldLabelPosition& labelPosition,
                             const DecorationBoxSlots& slots, bool singleLine, bool enabled, bool isError,
                             std::shared_ptr<MutableInteractionSource> source, const PaddingValues& contentPadding, const TextFieldColors& colors,
                             const std::function<void()>& container) {
        auto& comp = *rt::Composition::Current();
        const bool isFocused = source && source->IsFocused();
        const InputPhase inputState = isFocused ? InputPhase::Focused : visualText.empty() ? InputPhase::UnfocusedEmpty : InputPhase::UnfocusedNotEmpty;

        const auto& typography = MaterialTheme::typography();
        const text::TextStyle bodyLarge = typography.bodyLarge, bodySmall = typography.bodySmall;
        const bool overrideLabelTextStyleColor = (!bodyLarge.span.color.Specified() && bodySmall.span.color.Specified()) ||
                                                 (bodyLarge.span.color.Specified() && !bodySmall.span.color.Specified());

        auto& transition = UpdateTransition(inputState, rt::Key("TextFieldInputState"));
        const bool showExpandedLabel = slots.label && labelPosition.ShowsExpanded();
        const anim::SpecPtr fastSpatial = Value(tokens::MotionSchemeKeyTokens::FastSpatial);
        const anim::SpecPtr fastEffects = Value(tokens::MotionSchemeKeyTokens::FastEffects);
        const anim::SpecPtr slowEffects = Value(tokens::MotionSchemeKeyTokens::SlowEffects);
        using Segment = Transition<InputPhase>::Segment;

        // The animated values as State: read by the layout, the fading layers and the label's
        // own scope, so this box does not recompose on each frame of the focus animation.
        std::optional<AnimatedFloat> labelProgressState, placeholderAlphaState, affixAlphaState;
        if (slots.label)
            labelProgressState = AnimateFloatState<InputPhase>(transition, [showExpandedLabel](const InputPhase& p) {
                return p == InputPhase::UnfocusedEmpty ? (showExpandedLabel ? 0.0f : 1.0f) : 1.0f;
            }, [fastSpatial](const Segment&) { return fastSpatial; }, rt::Key("LabelProgress"));
        if (slots.placeholder)
            placeholderAlphaState = AnimateFloatState<InputPhase>(transition, [showExpandedLabel](const InputPhase& p) {
                return p == InputPhase::Focused ? 1.0f : p == InputPhase::UnfocusedEmpty ? (showExpandedLabel ? 0.0f : 1.0f) : 0.0f;
            }, [fastEffects, slowEffects](const Segment& s) {
                if (s.IsTransitioningTo(InputPhase::Focused, InputPhase::UnfocusedEmpty)) return fastEffects;
                if (s.IsTransitioningTo(InputPhase::UnfocusedEmpty, InputPhase::Focused) ||
                    s.IsTransitioningTo(InputPhase::UnfocusedNotEmpty, InputPhase::UnfocusedEmpty)) return slowEffects;
                return fastEffects;
            }, rt::Key("PlaceholderOpacity"));
        if (slots.prefix || slots.suffix)
            affixAlphaState = AnimateFloatState<InputPhase>(transition, [showExpandedLabel](const InputPhase& p) {
                return p == InputPhase::UnfocusedEmpty ? (showExpandedLabel ? 0.0f : 1.0f) : 1.0f;
            }, [fastEffects](const Segment&) { return fastEffects; }, rt::Key("PrefixSuffixOpacity"));
        const std::function<float()> labelProgress = [labelProgressState] { return labelProgressState ? labelProgressState->Value() : 1.0f; };
        const std::function<float()> placeholderAlpha = [placeholderAlphaState] { return placeholderAlphaState ? placeholderAlphaState->Value() : 0.0f; };
        const std::function<float()> affixAlpha = [affixAlphaState] { return affixAlphaState ? affixAlphaState->Value() : 0.0f; };

        // DecoratedLabel: its own scope reads the progress (the style lerp) and the colours.
        std::function<void()> decoratedLabel;
        if (slots.label) {
            const Color labelColor = colors.LabelColor(enabled, isError, isFocused);
            const auto label = slots.label;
            Transition<InputPhase>* t = &transition;
            decoratedLabel = [t, label, labelColor, overrideLabelTextStyleColor, bodyLarge, bodySmall, fastEffects, labelProgress] {
                rt::Composition::Current()->Child([t, label, labelColor, overrideLabelTextStyleColor, bodyLarge, bodySmall, fastEffects, labelProgress] {
                    std::optional<Color> labelTextStyleColor;
                    if (overrideLabelTextStyleColor) {
                        const Color focusedColor = bodySmall.span.color.Specified() ? bodySmall.span.color : labelColor;
                        const Color unfocusedColor = bodyLarge.span.color.Specified() ? bodyLarge.span.color : labelColor;
                        labelTextStyleColor = AnimateColor<InputPhase>(*t, [focusedColor, unfocusedColor](const InputPhase& p) {
                            return p == InputPhase::Focused ? focusedColor : unfocusedColor;
                        }, [fastEffects](const Segment&) { return fastEffects; }, rt::Key("LabelTextStyleColor"));
                    }
                    const Color labelContentColor = AnimateColor<InputPhase>(*t, [labelColor](const InputPhase&) { return labelColor; },
                                                                             [fastEffects](const Segment&) { return fastEffects; }, rt::Key("LabelContentColor"));
                    text::TextStyle style = text::Lerp(bodyLarge, bodySmall, labelProgress());
                    if (labelTextStyleColor) style.span.color = *labelTextStyleColor;
                    TextFieldLabelScope scope{ labelProgress };
                    Decoration(labelContentColor, &style, [&] { label(scope); });
                });
            };
        }

        // Transparent slots trouble TalkBack (b/261061240): leave them out at zero alpha. Whether
        // they show is derived state: this box recomposes only when it flips.
        auto& showPlaceholder = comp.Remember<std::shared_ptr<rt::Derived<bool>>>(
            [&] { return std::make_shared<rt::Derived<bool>>([placeholderAlpha] { return placeholderAlpha() > 0; }); },
            rt::Keys(static_cast<const void*>(placeholderAlphaState ? placeholderAlphaState->child : nullptr)));
        auto& showAffixState = comp.Remember<std::shared_ptr<rt::Derived<bool>>>(
            [&] { return std::make_shared<rt::Derived<bool>>([affixAlpha] { return affixAlpha() > 0; }); },
            rt::Keys(static_cast<const void*>(affixAlphaState ? affixAlphaState->child : nullptr)));
        std::function<void(Modifier)> decoratedPlaceholder;
        if (slots.placeholder && visualText.empty() && showPlaceholder->Get()) {
            const Color color = colors.PlaceholderColor(enabled, isError, isFocused);
            const auto placeholder = slots.placeholder;
            decoratedPlaceholder = [placeholder, color, bodyLarge](Modifier m) {
                Box(m, Alignment::TopStart(), false, [&] { Decoration(color, &bodyLarge, placeholder); });
            };
        }
        const bool showAffix = (slots.prefix || slots.suffix) && showAffixState->Get();
        std::function<void()> decoratedPrefix, decoratedSuffix;
        if (slots.prefix && showAffix) {
            const Color color = colors.PrefixColor(enabled, isError, isFocused);
            decoratedPrefix = [p = slots.prefix, color, bodyLarge] { Decoration(color, &bodyLarge, p); };
        }
        if (slots.suffix && showAffix) {
            const Color color = colors.SuffixColor(enabled, isError, isFocused);
            decoratedSuffix = [p = slots.suffix, color, bodyLarge] { Decoration(color, &bodyLarge, p); };
        }
        std::function<void()> decoratedLeading, decoratedTrailing, decoratedSupporting;
        if (slots.leadingIcon) decoratedLeading = [p = slots.leadingIcon, color = colors.LeadingIconColor(enabled, isError, isFocused)] { Decoration(color, nullptr, p); };
        if (slots.trailingIcon) decoratedTrailing = [p = slots.trailingIcon, color = colors.TrailingIconColor(enabled, isError, isFocused)] { Decoration(color, nullptr, p); };
        if (slots.supportingText) {
            decoratedSupporting = [p = slots.supportingText, color = colors.SupportingTextColor(enabled, isError, isFocused), bodySmall] {
                Decoration(color, &bodySmall, p);
            };
        }

        const bool cutout = labelPosition.kind == TextFieldLabelPosition::Kind::Cutout;
        const bool above = labelPosition.kind == TextFieldLabelPosition::Kind::Above;
        const Dp iconPadding = TextFieldHorizontalIconPadding();
        const ui::LayoutDirection dir = comp.Read(ui::LocalLayoutDirection);
        const bool ltr = dir == ui::LayoutDirection::Ltr;
        const Dp startTextFieldPadding = contentPadding.rtlAware || ltr ? contentPadding.start : contentPadding.end;
        const Dp endTextFieldPadding = contentPadding.rtlAware || ltr ? contentPadding.end : contentPadding.start;
        const Dp startPadding = decoratedLeading ? Dp(std::max(startTextFieldPadding.value - iconPadding.value, 0.0f)) : startTextFieldPadding;
        const Dp endPadding = decoratedTrailing ? Dp(std::max(endTextFieldPadding.value - iconPadding.value, 0.0f)) : endTextFieldPadding;

        // The cutout's size follows the label as it is measured (cutoutSize).
        auto& cutoutSize = comp.RememberState(ui::Size{});
        TextFieldLayoutParams params;
        params.singleLine = singleLine;
        params.placement = cutout ? LabelPlacement::Cutout : above ? LabelPlacement::Above : LabelPlacement::Inside;
        params.minimizedAlignment = labelPosition.minimizedAlignment;
        params.expandedAlignment = labelPosition.expandedAlignment;
        params.labelProgress = labelProgress;
        params.padding = contentPadding;
        params.minimizedLabelHalfHeight = MinimizedLabelHalfHeight();
        params.horizontalIconPadding = iconPadding;
        if (cutout) {
            rt::State<ui::Size>* size = &cutoutSize;
            params.onLabelMeasured = [size, labelProgress](ui::Size label) {
                const float progress = labelProgress();
                const ui::Size s{ label.width * progress, label.height * progress };
                if (!(size->Peek() == s)) size->Set(s);
            };
        }
        auto policy = cutout ? OutlinedTextFieldMeasurePolicy(params) : TextFieldMeasurePolicy(params);

        Layout({}, policy, [=, &cutoutSize] {
            // The container is a child, so the supporting text can sit outside it while still
            // sizing the field.
            Modifier containerModifier = LayoutId({}, ContainerId);
            if (cutout) {
                rt::State<ui::Size>* size = &cutoutSize;
                containerModifier = OutlineCutout(std::move(containerModifier), [size] { return size->Get(); }, labelPosition.minimizedAlignment, contentPadding);
            }
            Box(containerModifier, Alignment::TopStart(), true, container);

            if (decoratedLeading) Box(MinimumInteractiveComponentSize(LayoutId({}, LeadingId)), Alignment::Center(), false, decoratedLeading);
            if (decoratedTrailing) Box(MinimumInteractiveComponentSize(LayoutId({}, TrailingId)), Alignment::Center(), false, decoratedTrailing);
            const auto affixAlphaFn = affixAlpha;
            if (decoratedPrefix) {
                Box(FadingLayer(Padding(WrapContentHeight(HeightIn(LayoutId({}, PrefixId), MinTextLineHeight)),
                                        PaddingValues{ startPadding, Dp(0), PrefixSuffixTextPadding, Dp(0) }), affixAlphaFn),
                    Alignment::TopStart(), false, decoratedPrefix);
            }
            if (decoratedSuffix) {
                Box(FadingLayer(Padding(WrapContentHeight(HeightIn(LayoutId({}, SuffixId), MinTextLineHeight)),
                                        PaddingValues{ PrefixSuffixTextPadding, Dp(0), endPadding, Dp(0) }), affixAlphaFn),
                    Alignment::TopStart(), false, decoratedSuffix);
            }

            const PaddingValues labelPadding = above ? PaddingValues{ AboveLabelHorizontalPadding, Dp(0), AboveLabelHorizontalPadding, AboveLabelBottomPadding }
                                             : cutout ? PaddingValues{}
                                                      : PaddingValues{ startPadding, Dp(0), endPadding, Dp(0) };
            if (decoratedLabel) {
                const auto minHeight = [labelProgress] { return LerpDp(MinTextLineHeight, MinFocusedLabelLineHeight, labelProgress()); };
                const Modifier m = cutout ? Padding(LayoutId(WrapContentHeight(LabelMinHeight({}, minHeight)), LabelId), labelPadding)
                                          : Padding(WrapContentHeight(LabelMinHeight(LayoutId({}, LabelId), minHeight)), labelPadding);
                Box(m, Alignment::TopStart(), false, decoratedLabel);
            }

            const PaddingValues textPadding{ decoratedPrefix ? Dp(0) : startPadding, Dp(0), decoratedSuffix ? Dp(0) : endPadding, Dp(0) };
            const auto padText = [&](Modifier m) { return Padding(WrapContentHeight(HeightIn(std::move(m), MinTextLineHeight)), textPadding); };
            if (decoratedPlaceholder) decoratedPlaceholder(FadingLayer(padText(LayoutId({}, PlaceholderId)), placeholderAlpha));
            Box(padText(VisualTextSemantics(LayoutId({}, TextFieldId), visualText, bool(decoratedPrefix) || bool(decoratedSuffix))), Alignment::TopStart(),
                true, innerTextField);

            if (decoratedSupporting) {
                Box(Padding(WrapContentHeight(HeightIn(LayoutId({}, SupportingId), MinSupportingTextLineHeight)), SupportingTextPadding()), Alignment::TopStart(),
                    false, decoratedSupporting);
            }
        });
    }

}
