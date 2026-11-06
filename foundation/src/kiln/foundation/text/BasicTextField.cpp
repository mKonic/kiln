#include "kiln/foundation/text/BasicTextField.h"

#include "kiln/TextBackend.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"

#include <cmath>

namespace kiln::foundation {

    namespace {

        struct FieldCore;

        // The IME's handle on the field.
        class Session final : public ui::TextInputSession {
        public:
            explicit Session(FieldCore& c) : core(c) {}
            FieldCore& core;
            ui::ImeOptions Options() const override;
            ui::TextInputSnapshot Snapshot() const override;
            void CommitText(std::u16string_view t, int c) override;
            void SetComposingText(std::u16string_view t, int c) override;
            void SetComposingRegion(int s, int e) override;
            void FinishComposingText() override;
            void DeleteSurroundingText(int b, int a) override;
            void DeleteSurroundingTextInCodePoints(int b, int a) override;
            void SetSelection(int s, int e) override;
            void PerformImeAction(ui::ImeAction a) override;
            Caret CaretRect() const override;
        };

        // Shared between the field's modifiers (remembered per call site).
        struct FieldCore {
            TextFieldState* state = nullptr;
            TextFieldOptions options;
            ui::Owner* owner = nullptr;
            bool focused = false;
            Session session{ *this };
            rt::State<std::int64_t> blinkEpoch{ 0 };   // bumped on edits: caret shows solid
            TextBackend* text = nullptr;
            std::int64_t handle = 0;                   // current paragraph (for offsets)
            ui::FocusRequester requester;
            ui::LayoutNode* node = nullptr;   // the field's layout node (for its window position)

            void AfterImeEdit() { if (owner) owner->NotifyTextInputChanged(session, false); blinkEpoch.Set(blinkEpoch.Peek() + 1); }
            void AfterLocalEdit(bool content) { if (owner) owner->NotifyTextInputChanged(session, content); blinkEpoch.Set(blinkEpoch.Peek() + 1); }
        };

        ui::ImeOptions Session::Options() const { return core.options.ime; }
        ui::TextInputSnapshot Session::Snapshot() const {
            const TextFieldValue& v = core.state->Peek();
            ui::TextInputSnapshot s{ v.text, v.selection.start, v.selection.end, -1, -1 };
            if (v.composition) { s.compStart = v.composition->Min(); s.compEnd = v.composition->Max(); }
            return s;
        }
        void Session::CommitText(std::u16string_view t, int c) { core.state->CommitText(t, c); core.AfterImeEdit(); }
        void Session::SetComposingText(std::u16string_view t, int c) { core.state->SetComposingText(t, c); core.AfterImeEdit(); }
        void Session::SetComposingRegion(int s, int e) { core.state->SetComposingRegion(s, e); core.AfterImeEdit(); }
        void Session::FinishComposingText() { core.state->FinishComposingText(); core.AfterImeEdit(); }
        void Session::DeleteSurroundingText(int b, int a) { core.state->DeleteSurroundingText(b, a); core.AfterImeEdit(); }
        void Session::DeleteSurroundingTextInCodePoints(int b, int a) { core.state->DeleteSurroundingTextInCodePoints(b, a); core.AfterImeEdit(); }
        void Session::SetSelection(int s, int e) { core.state->SetSelection(s, e); core.AfterImeEdit(); }
        Session::Caret Session::CaretRect() const {
            Caret c;
            if (!core.text || !core.handle || !core.node) return c;
            const auto r = core.text->GetCursorRect(core.handle, core.state->Peek().selection.end);
            const Offset o = core.node->PositionInRoot();
            c.x = o.x + r.x; c.top = o.y + r.top; c.bottom = o.y + r.bottom;
            c.baseline = c.bottom - (r.bottom - r.top) * 0.2f;   // approximate until line metrics are exposed
            c.valid = true;
            return c;
        }

        void Session::PerformImeAction(ui::ImeAction a) {
            if (core.options.onImeAction) core.options.onImeAction(a);
        }

        // Layout + draw of the field's text, selection, composition underline and caret.
        struct FieldParams { std::shared_ptr<FieldCore> core; bool operator==(const FieldParams& o) const { return core == o.core; } };
        struct FieldNode final : ModifierNode, LayoutModifier, DrawModifier {
            FieldParams params;
            ParagraphData paragraph;
            std::u16string laidOut;
            int laidOutWidth = -1;
            bool laidOutEmpty = false;
            float lineHeight = 0;

            explicit FieldNode(FieldParams p) : params(std::move(p)) {}
            ~FieldNode() override { Release(); }
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            DrawModifier* AsDraw() override { return this; }

            void Release() {
                if (paragraph.handle && params.core->text) params.core->text->Release(paragraph.handle);
                paragraph = {};
            }
            void Relayout(const std::u16string& text, int width) {
                FieldCore& c = *params.core;
                if (!c.text) return;
                if (text == laidOut && width == laidOutWidth && paragraph.handle) return;
                Release();
                laidOut = text;
                laidOutWidth = width;
                laidOutEmpty = text.empty();
                // An empty field still needs a line's height for the caret.
                const std::string utf8 = text.empty() ? std::string(" ") : ToUtf8(text);
                const float w = c.options.ime.singleLine || width == Constraints::Infinity ? 0.0f : float(width);
                paragraph = c.text->Layout(utf8, c.options.style.fontSizeSp, w);
                if (laidOutEmpty) paragraph.glyphs.clear();
                c.handle = paragraph.handle;
            }

            MeasureResult Measure(MeasureScope& s, Measurable&, const Constraints& con) override {
                FieldCore& c = *params.core;
                if (Owner() && Owner()->GetOwner()) { c.owner = Owner()->GetOwner(); c.text = c.owner->Text(); c.node = Owner(); }
                const TextFieldValue& v = c.state->Value();   // subscribes measure to edits
                Relayout(v.text, con.maxWidth);
                // Sizing (spec 04 req. 14) is simplified: width = text (min 1 caret), height = text.
                const int caret = s.RoundToPx(Dp(2));
                const int w = con.ConstrainWidth(laidOutEmpty ? caret : int(std::ceil(paragraph.width)) + caret);
                const int h = con.ConstrainHeight(int(std::ceil(paragraph.height)));
                return s.Layout(w, h);
            }

            void Draw(ContentDrawScope& s) override {
                FieldCore& c = *params.core;
                const TextFieldValue& v = c.state->Value();
                if (!c.text || !paragraph.handle) { s.DrawContent(); return; }
                // Selection highlight (single-line ranges for now).
                if (c.focused && !v.selection.Collapsed()) {
                    const auto a = c.text->GetCursorRect(paragraph.handle, v.selection.Min());
                    const auto b = c.text->GetCursorRect(paragraph.handle, v.selection.Max());
                    if (a.top == b.top) s.DrawRect(c.options.selectionColor, { a.x, a.top }, { b.x - a.x, a.bottom - a.top });
                }
                s.DrawParagraph(paragraph, {}, c.options.style.color);
                // Composition underline.
                if (v.composition) {
                    const auto a = c.text->GetCursorRect(paragraph.handle, v.composition->Min());
                    const auto b = c.text->GetCursorRect(paragraph.handle, v.composition->Max());
                    if (a.top == b.top) s.DrawRect(c.options.style.color, { a.x, a.bottom - s.ToPx(Dp(1.5f)) }, { b.x - a.x, s.ToPx(Dp(1)) });
                }
                // Caret: solid for 500 ms after an edit or focus, then blinking 500/500 (TextFieldCursor).
                if (!c.focused) m_Blink.Cancel();
                if (c.focused && v.selection.Collapsed()) {
                    const bool visible = BlinkVisible();
                    if (visible) {
                        const auto r = c.text->GetCursorRect(paragraph.handle, v.selection.end);
                        const float width = std::floor(s.ToPx(Dp(2)));
                        const float x = std::clamp(r.x - width / 2, 0.0f, std::max(0.0f, s.GetSize().width - width));
                        s.DrawRect(c.options.cursorColor, { x, r.top }, { width, r.bottom - r.top });
                    }
                }
                s.DrawContent();
            }

            bool BlinkVisible();
            void OnDetach() override { m_Blink.Cancel(); }
            rt::State<bool> m_BlinkOn{ true };
            rt::Job m_Blink;
            std::int64_t m_BlinkEpoch = -1;
        };

        rt::Task<> Blink(FieldNode* node) {
            for (;;) {
                node->m_BlinkOn.Set(true);
                co_await rt::Delay(500);
                node->m_BlinkOn.Set(false);
                co_await rt::Delay(500);
            }
        }

        // The caret restarts solid on focus and on every edit (cursorAlpha animation restart).
        bool FieldNode::BlinkVisible() {
            FieldCore& c = *params.core;
            const std::int64_t epoch = c.blinkEpoch.Get();
            if (epoch != m_BlinkEpoch || !m_Blink.Active()) {
                m_BlinkEpoch = epoch;
                m_Blink = rt::Launch(rt::Dispatcher::Main(), Blink(this));
            }
            return m_BlinkOn.Get();
        }
    }

    void BasicTextField(TextFieldState& state, const Modifier& modifier, TextFieldOptions options, rt::Key key) {
        auto& comp = *rt::Composition::Current();
        auto& core = comp.Remember<std::shared_ptr<FieldCore>>([] { return std::make_shared<FieldCore>(); }, {}, key);
        core->state = &state;
        core->options = options;
        // Restart the caret blink whenever the field is focused or edited.
        auto c = core;

        Modifier m = FocusRequesterModifier(modifier, core->requester);
        m = Focusable(std::move(m), options.enabled ? ui::Focusability::Always : ui::Focusability::Never, [c](ui::FocusState s) {
            const bool now = ui::IsFocused(s);
            if (now == c->focused) return;
            c->focused = now;
            if (!c->owner) return;
            if (now) c->owner->StartTextInput(c->session);
            else {
                c->state->FinishComposingText();
                c->owner->StopTextInput(c->session);
            }
            c->blinkEpoch.Set(c->blinkEpoch.Peek() + 1);
        });
        m = OnKeyEvent(std::move(m), [c](const ui::KeyEvent& e) {
            if (e.type != ui::KeyEventType::KeyDown || !c->focused) return false;
            TextFieldState& st = *c->state;
            switch (e.keyCode) {
                case ui::keys::Del: st.Backspace(); c->AfterLocalEdit(true); return true;
                case 112: st.DeleteForward(); c->AfterLocalEdit(true); return true;   // KEYCODE_FORWARD_DEL
                case ui::keys::DpadLeft: st.MoveCursor(-1, e.Shift()); c->AfterLocalEdit(false); return true;
                case ui::keys::DpadRight: st.MoveCursor(1, e.Shift()); c->AfterLocalEdit(false); return true;
                case ui::keys::Enter: case ui::keys::NumpadEnter:
                    if (c->options.ime.singleLine) { if (c->options.onImeAction) c->options.onImeAction(c->options.ime.imeAction); return true; }
                    st.InsertFromKeyboard(u"\n"); c->AfterLocalEdit(true); return true;
                default: break;
            }
            if (e.unicode >= 0x20 && !e.Ctrl()) {
                std::u16string t;
                char32_t cp = e.unicode;
                if (cp >= 0x10000) { cp -= 0x10000; t.push_back(char16_t(0xD800 + (cp >> 10))); t.push_back(char16_t(0xDC00 + (cp & 0x3FF))); }
                else t.push_back(char16_t(cp));
                st.InsertFromKeyboard(t);
                c->AfterLocalEdit(true);
                return true;
            }
            return false;
        });
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(c.get())), [c](PointerInputScope& s) {
            TapCallbacks cb;
            cb.onTap = [c](Offset p) {
                if (!c->text || !c->handle) return;
                // Focus (the focus callback starts input and shows the keyboard), or re-show the
                // keyboard if already focused.
                if (!c->focused) c->requester.RequestFocus();
                const int offset = c->text->GetOffsetForPosition(c->handle, p.x, p.y);
                c->state->SetSelection(offset, offset);
                c->AfterLocalEdit(false);
                if (c->focused && c->owner) c->owner->StartTextInput(c->session);
            };
            return DetectTapGestures(s, cb);
        });
        // Text field semantics (spec 04 req. 7): editable text, focus, set text, selection.
        const TextFieldValue& val = state.Value();
        const bool password = options.ime.keyboardType == ui::KeyboardType::Password || options.ime.keyboardType == ui::KeyboardType::NumberPassword;
        m = Semantics(std::move(m), [c, text = ToUtf8(val.text), sel = val.selection, password](SemanticsConfiguration& cfg) {
            cfg.editableText = text;
            cfg.password = password;
            cfg.focused = c->focused;
            cfg.textSelection = std::make_pair(sel.start, sel.end);
            cfg.setText = [c](const std::string& t) { c->state->SetText(t); c->AfterLocalEdit(true); return true; };
            cfg.requestFocus = [c] { return c->requester.RequestFocus(); };
            cfg.onClick = [c] { if (!c->focused) c->requester.RequestFocus(); else if (c->owner) c->owner->StartTextInput(c->session); return true; };
        }, true);
        m = m.With<FieldNode>(FieldParams{ c });
        Layout(m, std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const>, const Constraints& con) {
            return s.Layout(con.minWidth, con.minHeight);
        }), {}, key);
    }

}
