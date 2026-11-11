// TextFieldSamples.kt
#include "TextFieldSamples.h"
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/Semantics.h>
#include <kiln/foundation/text/input/TextFieldBuffer.h>
#include <kiln/material3/Checkbox.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/TextField.h>
#include <kiln/material3/TextFieldDefaults.h>
#include <kiln/runtime/SnapshotFlow.h>
#include <kiln/ui/Locals.h>

namespace catalog::samples {
    namespace TFD = m3::TextFieldDefaults;
    namespace OTFD = m3::OutlinedTextFieldDefaults;

    namespace {
        constexpr int charLimit = 10;   // the error samples' limit

        using FieldFn = void (*)(TextFieldState&, const Modifier&, const m3::TextFieldOptions&);
        using SecureFieldFn = void (*)(TextFieldState&, const Modifier&, const m3::SecureTextFieldOptions&);

        std::function<void(const m3::TextFieldLabelScope&)> Label(std::string text) {
            return [text](const m3::TextFieldLabelScope&) { m3::Text(text); };
        }
        std::function<void()> TextOf(std::string text) {
            return [text] { m3::Text(text); };
        }

        // The single line field with a label the samples start from; the expressive ones add the
        // rounded shape and tonal colours (and an Inside label when outlined).
        m3::TextFieldOptions Field(const std::string& label, bool expressive, bool outlined) {
            m3::TextFieldOptions o;
            o.lineLimits = TextFieldLineLimits::SingleLine();
            o.label = Label(label);
            if (expressive) {
                o.shape = outlined ? OTFD::roundedShape() : TFD::roundedShape();
                o.colors = outlined ? OTFD::tonalColors() : TFD::tonalColors();
                if (outlined) o.labelPosition = m3::TextFieldLabelPosition::Inside();
            }
            return o;
        }

        void Simple(FieldFn field, bool expressive, bool outlined) {
            field(RememberTextFieldState(), {}, Field("Label", expressive, outlined));
        }

        void WithIcons(FieldFn field, bool expressive, bool outlined) {
            auto& state = RememberTextFieldState();
            m3::TextFieldOptions o = Field("Label", expressive, outlined);
            o.leadingIcon = [expressive] { m3::Icon(Icons::Filled::Favorite(), expressive ? std::optional<std::string>("Favorite") : std::nullopt); };
            o.trailingIcon = [&state, expressive] {
                const auto button = [&state] {
                    m3::IconButton([&state] { state.ClearText(); }, {}, {}, [] { m3::Icon(Icons::Filled::Clear(), std::string("Clear text")); });
                };
                if (expressive) button();
                else PlainTooltipAbove("Clear text", button);
            };
            field(state, {}, o);
        }

        // A "Show placeholder even when unfocused" checkbox over the field: checked, the label
        // stays minimized so the placeholder shows.
        void WithMinimizeToggle(FieldFn field, bool expressive, bool outlined, const char* label, std::function<void(m3::TextFieldOptions&)> extra) {
            auto& alwaysMinimizeLabel = RememberState(false);
            Column({}, [&alwaysMinimizeLabel, field, expressive, outlined, label, extra] {
                Row({}, [&alwaysMinimizeLabel] {
                    m3::Checkbox(alwaysMinimizeLabel.Get(), [&alwaysMinimizeLabel](bool c) { alwaysMinimizeLabel.Set(c); });
                    m3::Text("Show placeholder even when unfocused");
                });
                Spacer(Height({}, 16_dp));
                m3::TextFieldOptions o = Field(label, expressive, outlined);
                o.labelPosition = m3::TextFieldLabelPosition::Inside(alwaysMinimizeLabel.Get());
                extra(o);
                field(RememberTextFieldState(), {}, o);
            });
        }
        void WithPlaceholder(FieldFn field, bool expressive, bool outlined) {
            WithMinimizeToggle(field, expressive, outlined, "Email", [](m3::TextFieldOptions& o) { o.placeholder = TextOf("example@gmail.com"); });
        }
        void WithPrefixAndSuffix(FieldFn field, bool expressive, bool outlined) {
            WithMinimizeToggle(field, expressive, outlined, "Label", [](m3::TextFieldOptions& o) {
                o.prefix = TextOf("www.");
                o.suffix = TextOf(".com");
                o.placeholder = TextOf("google");
            });
        }

        void WithSupportingText(FieldFn field, bool expressive, bool outlined) {
            m3::TextFieldOptions o = Field("Label", expressive, outlined);
            o.supportingText = TextOf("Supporting text that is long and perhaps goes onto another line.");
            field(RememberTextFieldState(), {}, o);
        }

        // Over ten characters is an error, checked as the text changes.
        void WithErrorState(FieldFn field, bool expressive, bool outlined) {
            static const std::string errorMessage = "Text input too long";
            auto& state = RememberTextFieldState();
            auto& isError = RememberSaveableState(false);
            const auto validate = [&isError](const std::u16string& text) { isError.Set(int(text.size()) > charLimit); };
            // Run validation whenever text value changes
            Here().LaunchedEffect(kiln::rt::Keys(), [&state, validate] {
                return kiln::rt::SnapshotFlow<std::u16string>([&state] { return state.Text(); }).Collect(validate);
            });
            const bool error = isError.Get();
            m3::TextFieldOptions o = Field(error ? "Username*" : "Username", expressive, outlined);
            o.supportingText = [&state, error] {
                Row({}, [&state, error] {
                    m3::Text(error ? errorMessage : "", ClearAndSetSemantics(Weight({}, 1), [](SemanticsConfiguration&) {}));
                    m3::Text("Limit: " + std::to_string(state.Text().size()) + "/" + std::to_string(charLimit));
                });
            };
            o.isError = error;
            o.onKeyboardAction = [&state, validate](std::function<void()>) { validate(state.Text()); };
            field(state, Semantics({}, [error](SemanticsConfiguration& c) {
                      c.maxTextLength = charLimit;
                      // Provide localized description of the error
                      if (error) c.error = errorMessage;
                  }),
                  o);
        }

        void Password(SecureFieldFn field, bool expressive, bool outlined) {
            auto& passwordHidden = RememberSaveableState(true);
            const bool hidden = passwordHidden.Get();
            m3::SecureTextFieldOptions o;
            o.label = Label("Enter password");
            if (!expressive) {
                KeyboardOptions k;
                k.autoCorrectEnabled = false;
                k.keyboardType = hidden ? KeyboardType::Password : KeyboardType::PasswordVisible;
                o.keyboardOptions = k;
            }
            o.textObfuscationMode = hidden ? TextObfuscationMode::System : TextObfuscationMode::Visible;
            o.trailingIcon = [&passwordHidden, hidden, expressive] {
                // Provide localized description for accessibility services
                const std::string description = hidden ? "Show password" : "Hide password";
                const auto button = [&passwordHidden, hidden, description] {
                    m3::IconButton([&passwordHidden, hidden] { passwordHidden.Set(!hidden); }, {}, {}, [hidden, description] {
                        m3::Icon(hidden ? Icons::Filled::Visibility() : Icons::Filled::VisibilityOff(), description);
                    });
                };
                if (expressive) button();
                else PlainTooltipAbove(description, button);
            };
            if (expressive) {
                o.shape = outlined ? OTFD::roundedShape() : TFD::roundedShape();
                o.colors = outlined ? OTFD::tonalColors() : TFD::tonalColors();
                if (outlined) o.labelPosition = m3::TextFieldLabelPosition::Inside();
            }
            field(RememberTextFieldState(), {}, o);
        }

        void InitialValueAndSelection(FieldFn field) {
            auto& state = RememberTextFieldState("Initial text", TextRange{ 0, 12 });
            field(state, {}, Field("Label", false, false));
        }
    }

    void SimpleTextFieldSample() { Simple(m3::TextField, false, false); }
    void SimpleOutlinedTextFieldSample() { Simple(m3::OutlinedTextField, false, true); }

    void TextFieldWithTransformations() {
        m3::TextFieldOptions o = Field("Phone number", false, false);
        o.keyboardOptions.keyboardType = KeyboardType::Number;
        // Input transformation to limit user input to 10 digits
        o.inputTransformation = Then(MaxLengthTrim(10), InputTransformationOf([](TextFieldBuffer& b) {
                                         for (char16_t c : b.AsCharSequence())
                                             if (c < u'0' || c > u'9') {
                                                 b.RevertAllChanges();
                                                 return;
                                             }
                                     }));
        // Output transformation to format as a phone number: (XXX) XXX-XXXX
        o.outputTransformation = OutputTransformationOf([](TextFieldBuffer& b) {
            if (b.Length() > 0) b.Insert(0, u"(");
            if (b.Length() > 4) b.Insert(4, u") ");
            if (b.Length() > 9) b.Insert(9, u"-");
        });
        m3::TextField(RememberTextFieldState(), {}, o);
    }

    void TextFieldWithIcons() { WithIcons(m3::TextField, false, false); }
    void TextFieldWithPlaceholder() { WithPlaceholder(m3::TextField, false, false); }
    void TextFieldWithPrefixAndSuffix() { WithPrefixAndSuffix(m3::TextField, false, false); }
    void TextFieldWithErrorState() { WithErrorState(m3::TextField, false, false); }
    void TextFieldWithSupportingText() { WithSupportingText(m3::TextField, false, false); }
    void PasswordTextField() { Password(m3::SecureTextField, false, false); }
    void TextFieldWithInitialValueAndSelection() { InitialValueAndSelection(m3::TextField); }
    void OutlinedTextFieldWithInitialValueAndSelection() { InitialValueAndSelection(m3::OutlinedTextField); }

    void DenseTextFieldContentPadding() {
        m3::TextFieldOptions o = Field("Label", false, false);
        o.contentPadding = PaddingValues{ 12_dp, 4_dp, 12_dp, 4_dp };
        // Need to set a min height using `heightIn` to override the default
        m3::TextField(RememberTextFieldState(), HeightIn({}, 48_dp), o);
    }

    void TextFieldWithHideKeyboardOnImeAction() {
        const auto keyboardController = kiln::ui::CurrentSoftwareKeyboardController();
        m3::TextFieldOptions o;
        o.label = Label("Label");
        o.keyboardOptions.imeAction = ImeAction::Done;
        o.onKeyboardAction = [keyboardController](std::function<void()>) { if (keyboardController) keyboardController->Hide(); };
        m3::TextField(RememberTextFieldState(), {}, o);
    }

    void TextArea() {
        auto& state = RememberTextFieldState(
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
            "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quisque "
            "nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. "
            "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu "
            "fugiat nulla pariatur. Excepteur sint occaecat cupidatat non  proident, sunt in "
            "culpa qui officia deserunt mollit anim id est laborum.");
        m3::TextFieldOptions o;
        o.label = Label("Label");
        m3::TextField(state, Height({}, 120_dp), o);
    }

    void ExpressiveTextFieldSample() { Simple(m3::TextField, true, false); }
    void ExpressiveOutlinedTextFieldSample() { Simple(m3::OutlinedTextField, true, true); }
    void ExpressiveTextFieldWithIcons() { WithIcons(m3::TextField, true, false); }
    void ExpressiveOutlinedTextFieldWithIcons() { WithIcons(m3::OutlinedTextField, true, true); }
    void ExpressiveTextFieldWithPlaceholder() { WithPlaceholder(m3::TextField, true, false); }
    void ExpressiveOutlinedTextFieldWithPlaceholder() { WithPlaceholder(m3::OutlinedTextField, true, true); }
    void ExpressiveTextFieldWithPrefixAndSuffix() { WithPrefixAndSuffix(m3::TextField, true, false); }
    void ExpressiveOutlinedTextFieldWithPrefixAndSuffix() { WithPrefixAndSuffix(m3::OutlinedTextField, true, true); }
    void ExpressiveTextFieldWithSupportingText() { WithSupportingText(m3::TextField, true, false); }
    void ExpressiveOutlinedTextFieldWithSupportingText() { WithSupportingText(m3::OutlinedTextField, true, true); }
    void ExpressiveTextFieldWithErrorState() { WithErrorState(m3::TextField, true, false); }
    void ExpressiveOutlinedTextFieldWithErrorState() { WithErrorState(m3::OutlinedTextField, true, true); }
    void ExpressivePasswordTextField() { Password(m3::SecureTextField, true, false); }
    void ExpressiveOutlinedPasswordTextField() { Password(m3::OutlinedSecureTextField, true, true); }

    void ExpressiveConnectedTextFieldsSample() {
        static const std::string errorMessage = "Text is invalid";
        auto& firstNameState = RememberTextFieldState();
        auto& isFirstNameError = RememberSaveableState(false);
        auto& lastNameState = RememberTextFieldState();
        auto& isLastNameError = RememberSaveableState(false);
        Column({}, [&] {
            const bool firstError = isFirstNameError.Get(), lastError = isLastNameError.Get();
            const auto field = [](TextFieldState& state, const char* name, bool error, ShapePtr shape) {
                m3::TextFieldOptions o;
                o.lineLimits = TextFieldLineLimits::SingleLine();
                o.label = Label(error ? std::string(name) + "*" : std::string(name));
                if (error) o.supportingText = [] { m3::Text(errorMessage, ClearAndSetSemantics({}, [](SemanticsConfiguration&) {})); };
                o.isError = error;
                o.shape = std::move(shape);
                o.colors = TFD::tonalColors();
                m3::TextField(state, Semantics({}, [error](SemanticsConfiguration& c) { if (error) c.error = errorMessage; }), o);
            };
            field(firstNameState, "First name", firstError,
                  firstError ? RoundedCorner(12_dp) : RoundedCorner(CornerSize::Of(12_dp), CornerSize::Of(12_dp), CornerSize::Of(4_dp), CornerSize::Of(4_dp)));
            Spacer(Height({}, 2_dp));
            // As upstream: the last name's shape follows the first name's error.
            field(lastNameState, "Last name", lastError,
                  firstError ? RoundedCorner(12_dp) : RoundedCorner(CornerSize::Of(4_dp), CornerSize::Of(4_dp), CornerSize::Of(12_dp), CornerSize::Of(12_dp)));
            Spacer(Height({}, 16_dp));
            for (auto* entry : { &isFirstNameError, &isLastNameError }) {
                Row({}, Arrangement::Start(), AlignmentV::CenterVertically(), [entry, &isFirstNameError] {
                    m3::Checkbox(entry->Get(), [entry](bool c) { entry->Set(c); });
                    m3::Text(entry == &isFirstNameError ? "Show first name error" : "Show last name error");
                });
            }
        });
    }
}
