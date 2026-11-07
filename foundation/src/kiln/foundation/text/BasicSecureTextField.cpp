#include "kiln/foundation/text/BasicSecureTextField.h"

#include "kiln/TextBackend.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/text/input/TextFieldKeys.h"
#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    namespace {
        constexpr std::int64_t kLastTypedCharacterRevealDurationMillis = 1500;

        // SecureTextFieldController with its PasswordInputTransformation.
        struct SecureController {
            ui::Owner* owner = nullptr;
            rt::State<char32_t> obfuscationMask{ U'•' };
            TextObfuscationMode mode = TextObfuscationMode::System;
            rt::State<int> revealCodepointIndex{ -1 };
            rt::Job hideJob;

            void Hide() { if (revealCodepointIndex.Peek() != -1) revealCodepointIndex.Set(-1); }
            // observeHideEvents (collectLatest): each signal restarts the 1.5 s timer.
            void ScheduleHide() {
                hideJob = rt::Launch(rt::Dispatcher::Main(), [](SecureController* c) -> rt::Task<> {
                    co_await rt::Delay(kLastTypedCharacterRevealDurationMillis);
                    c->Hide();
                }(this));
            }
            int Break(std::u16string_view text, int offset, bool following) const {
                TextBackend* b = owner ? owner->Text() : nullptr;
                return b ? b->CharacterBreak(text, offset, following) : TextBackend::SurrogateCharacterBreak(text, offset, following);
            }
            std::u16string MaskString() const {
                const char32_t c = obfuscationMask.Get();
                if (c < 0x10000) return std::u16string(1, char16_t(c));
                const char32_t v = c - 0x10000;
                return { char16_t(0xD800 + (v >> 10)), char16_t(0xDC00 + (v & 0x3FF)) };
            }
        };

        class PasswordInputTransformation final : public InputTransformation {
        public:
            explicit PasswordInputTransformation(std::shared_ptr<SecureController> c) : m_C(std::move(c)) {}
            void TransformInput(TextFieldBuffer& buffer) const override {
                SecureController& c = *m_C;
                // isBasicTextFieldOutputTransformationOnlyEnabled: one inserted glyph.
                bool singleGlyph = false;
                const ChangeList& changes = buffer.Changes();
                if (changes.ChangeCount() == 1) {
                    const TextRange inserted = changes.Range(0);
                    const int length = inserted.Length();
                    if (length == 1) {
                        singleGlyph = true;
                    } else if (length > 1) {
                        const std::u16string_view text(buffer.AsCharSequence());
                        if (c.Break(text.substr(std::size_t(inserted.Min()), std::size_t(length)), 0, true) == length) singleGlyph = true;
                    }
                }
                if (!singleGlyph || buffer.HasSelection()) {
                    c.revealCodepointIndex.Set(-1);
                    return;
                }
                bool shouldReveal = false;
                if (c.mode == TextObfuscationMode::RevealLastTyped) {
                    shouldReveal = true;
                } else if (c.mode == TextObfuscationMode::System) {
                    const int settings = c.owner ? c.owner->PasswordVisibility() : 3;
                    const auto* tracker = dynamic_cast<const ChangeTracker*>(&changes);
                    const bool physical = tracker && tracker->IsFromHardwareSource(0);
                    shouldReveal = physical ? (settings & 2) != 0 : (settings & 1) != 0;
                }
                if (!shouldReveal) {
                    c.revealCodepointIndex.Set(-1);
                    return;
                }
                const int insertionPoint = changes.Range(0).Min();
                if (c.revealCodepointIndex.Peek() != insertionPoint) {
                    c.ScheduleHide();
                    c.revealCodepointIndex.Set(insertionPoint);
                }
            }
        private:
            std::shared_ptr<SecureController> m_C;
        };

        // OutputTransformationPerGlyph: each glyph, from the end, replaced by what `transform`
        // gives for its range (nothing: kept).
        class PerGlyphOutput final : public OutputTransformation {
        public:
            PerGlyphOutput(std::shared_ptr<SecureController> c, bool revealLast) : m_C(std::move(c)), m_RevealLast(revealLast) {}
            void TransformOutput(TextFieldBuffer& buffer) const override {
                int readOffset = buffer.Length();
                while (readOffset > 0) {
                    const int previousBreak = m_C->Break(buffer.AsCharSequence(), readOffset, false);
                    if (!m_RevealLast || previousBreak != m_C->revealCodepointIndex.Get()) buffer.Replace(previousBreak, readOffset, m_C->MaskString());
                    readOffset = previousBreak;
                }
            }
        private:
            std::shared_ptr<SecureController> m_C;
            bool m_RevealLast;
        };

        // Where the controller learns its owner (the text backend, the show-passwords setting).
        struct OwnerParams {
            std::shared_ptr<SecureController> c;
            bool operator==(const OwnerParams&) const = default;
        };
        struct OwnerNode final : ModifierNode {
            OwnerParams params;
            explicit OwnerNode(OwnerParams p) : params(std::move(p)) {}
            void OnParamsChanged() { OnAttach(); }
            void OnAttach() override { params.c->owner = Owner() ? Owner()->GetOwner() : nullptr; }
            void OnDetach() override { params.c->hideJob.Cancel(); params.c->owner = nullptr; }
        };

        struct Transformations {
            InputTransformationPtr password;
            OutputTransformationPtr revealLast, hidden;
        };
    }

    void BasicSecureTextField(TextFieldState& state, const Modifier& modifier, BasicSecureTextFieldOptions options, rt::Key key) {
        auto& comp = *rt::Composition::Current();
        auto controller = comp.Remember<std::shared_ptr<SecureController>>([] { return std::make_shared<SecureController>(); }, {}, key);
        auto& tx = comp.Remember<std::shared_ptr<Transformations>>([controller] {
            auto t = std::make_shared<Transformations>();
            t->password = std::make_shared<PasswordInputTransformation>(controller);
            t->revealLast = std::make_shared<PerGlyphOutput>(controller, true);
            t->hidden = std::make_shared<PerGlyphOutput>(controller, false);
            return t;
        }, {}, key);
        // rememberUpdatedState
        if (controller->obfuscationMask.Peek() != options.textObfuscationCharacter) controller->obfuscationMask.Set(options.textObfuscationCharacter);
        controller->mode = options.textObfuscationMode;
        const bool revealLastTypedEnabled =
            options.textObfuscationMode == TextObfuscationMode::RevealLastTyped || options.textObfuscationMode == TextObfuscationMode::System;
        if (!revealLastTypedEnabled) controller->Hide();

        OutputTransformationPtr output;
        switch (options.textObfuscationMode) {
            case TextObfuscationMode::RevealLastTyped:
            case TextObfuscationMode::System: output = tx->revealLast; break;
            case TextObfuscationMode::Hidden: output = tx->hidden; break;
            case TextObfuscationMode::Visible: break;
        }

        Modifier m = modifier.With<OwnerNode>(OwnerParams{ controller });
        // Copy and cut keys do nothing.
        m = OnKeyEvent(std::move(m), {}, [](const ui::KeyEvent& e) {
            const auto command = MapKey(e);
            return command == KeyCommand::Copy || command == KeyCommand::Cut;
        });
        m = OnFocusChanged(std::move(m), [controller](ui::FocusState s) { if (!ui::IsFocused(s)) controller->Hide(); });

        BasicTextFieldOptions o;
        o.enabled = options.enabled;
        o.readOnly = options.readOnly;
        o.inputTransformation = options.inputTransformation ? Then(options.inputTransformation, tx->password) : tx->password;
        o.textStyle = options.textStyle;
        o.keyboardOptions = options.keyboardOptions;
        o.onKeyboardAction = std::move(options.onKeyboardAction);
        o.lineLimits = TextFieldLineLimits::SingleLine();
        o.onTextLayout = std::move(options.onTextLayout);
        o.interactionSource = std::move(options.interactionSource);
        o.cursorBrush = std::move(options.cursorBrush);
        o.outputTransformation = output;
        o.decorator = std::move(options.decorator);
        o.scrollState = options.scrollState;
        o.isPassword = true;
        BasicTextField(state, m, std::move(o), key);
    }

}
