#include "kiln/material3/TextField.h"
#include <kiln/Function.h>

#include "kiln/foundation/text/input/TextFieldBuffer.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/TextFieldDecoration.h"

namespace kiln::material3 {
    using namespace foundation;

    namespace {
        // The text as shown: through the output transformation.
        std::string VisualText(TextFieldState& state, const OutputTransformationPtr& output) {
            TextFieldBuffer buffer(state.Value());
            if (output) output->TransformOutput(buffer);
            return buffer.ToString();
        }

        void MaterialTextField(bool outlined, TextFieldState& state, const ui::Modifier& modifier, const TextFieldOptions& o) {
            auto& c = *rt::Composition::Current();
            auto source = o.interactionSource ? o.interactionSource
                                              : c.Remember<std::shared_ptr<MutableInteractionSource>>([] { return std::make_shared<MutableInteractionSource>(); });
            const TextFieldColors colors = o.colors ? *o.colors : outlined ? OutlinedTextFieldDefaults::colors() : TextFieldDefaults::colors();
            const TextFieldLabelPosition labelPosition =
                o.labelPosition.value_or(outlined ? TextFieldLabelPosition::Cutout() : TextFieldLabelPosition::Inside());
            const PaddingValues contentPadding = o.contentPadding.value_or(internal::DefaultContentPadding(bool(o.label), labelPosition));
            const ShapePtr shape = o.shape ? o.shape : outlined ? OutlinedTextFieldDefaults::shape() : TextFieldDefaults::shape();
            // The style's colour, else the state's.
            const text::TextStyle style = o.textStyle.value_or(c.Read(LocalTextStyle()));
            const ui::Color textColor = style.span.color.Specified() ? style.span.color : colors.TextColor(o.enabled, o.isError, source->IsFocused());
            text::TextStyle merged = style;
            merged.span.color = textColor;
            ScrollState* scroll = o.scrollState ? o.scrollState : &RememberScrollState();

            c.Provide(LocalTextSelectionColors, colors.textSelectionColors, [=, &state] {
                BasicTextFieldOptions b;
                b.enabled = o.enabled;
                b.readOnly = o.readOnly;
                b.textStyle = merged;
                b.cursorBrush = gfx::Brush::SolidColor(colors.CursorColor(o.isError));
                b.keyboardOptions = o.keyboardOptions;
                b.onKeyboardAction = o.onKeyboardAction;
                b.lineLimits = o.lineLimits;
                b.onTextLayout = o.onTextLayout;
                b.interactionSource = source;
                b.inputTransformation = o.inputTransformation;
                b.outputTransformation = o.outputTransformation;
                b.scrollState = scroll;
                b.spellCheck = o.spellCheck;
                const bool enabled = o.enabled, isError = o.isError, singleLine = o.lineLimits == TextFieldLineLimits::SingleLine();
                internal::DecorationBoxSlots slots{ o.label, o.placeholder, o.leadingIcon, o.trailingIcon, o.prefix, o.suffix, o.supportingText };
                TextFieldState* st = &state;
                const OutputTransformationPtr output = o.outputTransformation;
                b.decorator = [=](const kiln::Function<void()>& inner) {
                    internal::CommonDecorationBox(VisualText(*st, output), inner, labelPosition, slots, singleLine, enabled, isError, source, contentPadding,
                                                  colors, [=] {
                                                      if (outlined) OutlinedTextFieldDefaults::Container(enabled, isError, source, {}, colors, shape);
                                                      else TextFieldDefaults::Container(enabled, isError, source, {}, colors, shape);
                                                  });
                };
                ui::Modifier m = internal::TopPaddingForLabelCutout(modifier, bool(o.label), labelPosition);
                m = internal::DefaultErrorSemantics(std::move(m), o.isError, GetString(Strings::DefaultErrorMessage));
                m = DefaultMinSize(std::move(m), outlined ? OutlinedTextFieldDefaults::MinWidth : TextFieldDefaults::MinWidth,
                                   outlined ? OutlinedTextFieldDefaults::MinHeight : TextFieldDefaults::MinHeight);
                BasicTextField(state, m, std::move(b));
            });
        }
    }

    void TextField(TextFieldState& state, const ui::Modifier& modifier, const TextFieldOptions& options) { MaterialTextField(false, state, modifier, options); }
    void OutlinedTextField(TextFieldState& state, const ui::Modifier& modifier, const TextFieldOptions& options) {
        MaterialTextField(true, state, modifier, options);
    }

    namespace {
        void MaterialSecureTextField(bool outlined, TextFieldState& state, const ui::Modifier& modifier, const SecureTextFieldOptions& o) {
            auto& c = *rt::Composition::Current();
            auto source = o.interactionSource ? o.interactionSource
                                              : c.Remember<std::shared_ptr<MutableInteractionSource>>([] { return std::make_shared<MutableInteractionSource>(); });
            const TextFieldColors colors = o.colors ? *o.colors : outlined ? OutlinedTextFieldDefaults::colors() : TextFieldDefaults::colors();
            const TextFieldLabelPosition labelPosition =
                o.labelPosition.value_or(outlined ? TextFieldLabelPosition::Cutout() : TextFieldLabelPosition::Inside());
            const PaddingValues contentPadding = o.contentPadding.value_or(
                outlined ? internal::DefaultContentPadding(bool(o.label), labelPosition)
                         : (!o.label || labelPosition.kind == TextFieldLabelPosition::Kind::Above ? TextFieldDefaults::contentPaddingWithoutLabel()
                                                                                                  : TextFieldDefaults::contentPaddingWithLabel()));
            const ShapePtr shape = o.shape ? o.shape : outlined ? OutlinedTextFieldDefaults::shape() : TextFieldDefaults::shape();
            const text::TextStyle style = o.textStyle.value_or(c.Read(LocalTextStyle()));
            text::TextStyle merged = style;
            merged.span.color = style.span.color.Specified() ? style.span.color : colors.TextColor(o.enabled, o.isError, source->IsFocused());

            c.Provide(LocalTextSelectionColors, colors.textSelectionColors, [=, &state] {
                BasicSecureTextFieldOptions b;
                b.enabled = o.enabled;
                b.textStyle = merged;
                b.cursorBrush = gfx::Brush::SolidColor(colors.CursorColor(o.isError));
                if (o.keyboardOptions) b.keyboardOptions = *o.keyboardOptions;
                b.onKeyboardAction = o.onKeyboardAction;
                b.onTextLayout = o.onTextLayout;
                b.interactionSource = source;
                b.inputTransformation = o.inputTransformation;
                b.textObfuscationMode = o.textObfuscationMode;
                b.textObfuscationCharacter = o.textObfuscationCharacter;
                const bool enabled = o.enabled, isError = o.isError;
                internal::DecorationBoxSlots slots{ o.label, o.placeholder, o.leadingIcon, o.trailingIcon, o.prefix, o.suffix, o.supportingText };
                TextFieldState* st = &state;
                // The decorator sees the state's text (no output transformation), single line.
                b.decorator = [=](const kiln::Function<void()>& inner) {
                    internal::CommonDecorationBox(VisualText(*st, nullptr), inner, labelPosition, slots, true, enabled, isError, source, contentPadding, colors,
                                                  [=] {
                                                      if (outlined) OutlinedTextFieldDefaults::Container(enabled, isError, source, {}, colors, shape);
                                                      else TextFieldDefaults::Container(enabled, isError, source, {}, colors, shape);
                                                  });
                };
                ui::Modifier m = internal::TopPaddingForLabelCutout(modifier, bool(o.label), labelPosition);
                m = internal::DefaultErrorSemantics(std::move(m), o.isError, GetString(Strings::DefaultErrorMessage));
                m = DefaultMinSize(std::move(m), outlined ? OutlinedTextFieldDefaults::MinWidth : TextFieldDefaults::MinWidth,
                                   outlined ? OutlinedTextFieldDefaults::MinHeight : TextFieldDefaults::MinHeight);
                BasicSecureTextField(state, m, std::move(b));
            });
        }
    }

    void SecureTextField(TextFieldState& state, const ui::Modifier& modifier, const SecureTextFieldOptions& options) {
        MaterialSecureTextField(false, state, modifier, options);
    }
    void OutlinedSecureTextField(TextFieldState& state, const ui::Modifier& modifier, const SecureTextFieldOptions& options) {
        MaterialSecureTextField(true, state, modifier, options);
    }

}
