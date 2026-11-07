#include "Check.h"
#include "ToolbarHelpers.h"
#include "FakeText.h"

#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/content/ReceiveContent.h"
#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/Pointer.h"

#include <chrono>
#include <thread>

using namespace kiln;
using namespace kiln::foundation;

namespace { std::u16string U(const char* s) { return ToUtf16(s); } }

namespace {
    void Select(TextFieldState& st, int s, int e) { st.Edit([&](TextFieldBuffer& b) { b.SetSelection({ s, e }); }); }
    void SetText(TextFieldState& st, const char* t) { st.SetTextAndPlaceCursorAtEnd(t); }
}

TEST(ImeCommandsFollowInputConnectionSemantics) {
    TextFieldState st("hello");
    TransformedTextFieldState tf(st);
    ImeEditCommandScope ime(tf);
    Select(st, 5, 5);
    ime.SetComposingText(U("wor"), 1);
    CHECK(st.Peek().text == U("hellowor"));
    CHECK(st.Peek().composition == (TextRange{ 5, 8 }));
    CHECK(st.Peek().selection == (TextRange{ 8, 8 }));
    // A composition is underlined unless the IME styles it.
    CHECK(st.Peek().composingAnnotations.size() == 1 && st.Peek().composingAnnotations[0].start == 5);
    ime.SetComposingText(U("world"), 1);               // replaces the composition
    CHECK(st.Peek().text == U("helloworld"));
    ime.CommitText(U(" world"), 1);                    // commit replaces the composition too
    CHECK(st.Peek().text == U("hello world"));
    CHECK(!st.Peek().composition);
    CHECK_EQ(st.Peek().selection.start, 11);
    ime.CommitText(U("!"), 0);                         // <= 0: relative to the start of the insert
    CHECK(st.Peek().text == U("hello world!"));
    CHECK_EQ(st.Peek().selection.start, 11);
    Select(st, 5, 5);
    ime.DeleteSurroundingText(5, 1);                   // " " after, "hello" before
    CHECK(st.Peek().text == U("world!"));
    CHECK_EQ(st.Peek().selection.start, 0);
    ime.SetComposingRegion(0, 5);
    CHECK(st.Peek().composition == (TextRange{ 0, 5 }));
    ime.FinishComposingText();
    CHECK(!st.Peek().composition);
    // A batch is one edit.
    ime.BeginBatchEdit();
    ime.CommitText(U("a"), 1);
    ime.CommitText(U("b"), 1);
    CHECK(st.Peek().text == U("world!"));
    ime.EndBatchEdit();
    CHECK(st.Peek().text == U("abworld!"));
}

TEST(SurrogatePairsStayWhole) {
    TextFieldState st("a\xF0\x9F\x94\xA5" "b");          // a, fire emoji (2 UTF-16 units), b
    TransformedTextFieldState tf(st);
    ImeEditCommandScope ime(tf);
    CHECK_EQ(int(st.Peek().text.size()), 4);
    Select(st, 3, 3);
    ime.DeleteSurroundingTextInCodePoints(1, 0);
    CHECK(st.Peek().text == U("ab"));
    SetText(st, "x\xF0\x9F\x94\xA5");
    Select(st, 3, 3);
    ime.DeleteSurroundingTextInCodePoints(1, 0);
    CHECK(st.Peek().text == U("x"));
}

TEST(ChangeTrackerMergesTouchingChanges) {
    ChangeTracker t;
    t.TrackChange(2, 2, 3);       // insert 3 at 2
    t.TrackChange(5, 5, 1);       // touching: one change
    CHECK_EQ(t.ChangeCount(), 1);
    CHECK(t.Range(0) == (TextRange{ 2, 6 }) && t.OriginalRange(0) == (TextRange{ 2, 2 }));
    t.TrackChange(0, 1, 0);       // delete before it: a second change, the first shifts
    CHECK_EQ(t.ChangeCount(), 2);
    CHECK(t.Range(0) == (TextRange{ 0, 0 }) && t.OriginalRange(0) == (TextRange{ 0, 1 }));
    CHECK(t.Range(1) == (TextRange{ 1, 5 }) && t.OriginalRange(1) == (TextRange{ 2, 2 }));
}

TEST(UndoMergesTypingAndKeepsNewLinesApart) {
    std::int64_t now = 1000;
    SetUndoClock([&] { return now; });
    TextFieldState st("");
    TransformedTextFieldState tf(st);
    for (const char* c : { "h", "e", "y" }) tf.ReplaceSelectedText(ToUtf16(c));
    CHECK(st.Peek().text == U("hey"));
    tf.ReplaceSelectedText(u"\n");                    // a new line never merges
    tf.ReplaceSelectedText(u"x");
    now += 6000;                                       // 5 s later: a new step
    tf.ReplaceSelectedText(u"z");
    CHECK(st.CanUndo() && !st.CanRedo());
    st.Undo();
    CHECK(st.Peek().text == U("hey\nx"));
    st.Undo();
    CHECK(st.Peek().text == U("hey\n"));
    st.Undo();
    CHECK(st.Peek().text == U("hey"));
    st.Undo();
    CHECK(st.Peek().text == U(""));
    CHECK(!st.CanUndo() && st.CanRedo());
    st.Redo();
    CHECK(st.Peek().text == U("hey") && st.Peek().selection == TextRange(3));
    // Backspaces merge into one deletion; a programmatic edit is its own step.
    Select(st, 3, 3);
    for (int i = 0; i < 2; ++i)
        tf.EditUntransformedTextAsUser([](TextFieldBuffer& b) { const int at = b.Selection().start; b.Delete(at - 1, at); });
    CHECK(st.Peek().text == U("h"));
    st.Undo();
    CHECK(st.Peek().text == U("hey"));
    st.Edit([](TextFieldBuffer& b) { b.Append(u"!"); });
    st.Undo();
    CHECK(st.Peek().text == U("hey"));
    SetUndoClock(nullptr);
}

TEST(InputTransformationsFilterUserEdits) {
    TextFieldState st("ab");
    TransformedTextFieldState trim(st, MaxLengthTrim(4));
    trim.ReplaceSelectedText(u"cdef");
    CHECK(st.Peek().text == U("abcd"));
    TransformedTextFieldState reject(st, MaxLengthReject(4));
    reject.ReplaceSelectedText(u"z");
    CHECK(st.Peek().text == U("abcd"));
    TransformedTextFieldState caps(st, Then(AllCaps(), ByValue([](std::u16string_view, std::u16string_view proposed) {
        std::u16string out(proposed);
        std::erase(out, u'1');   // digits 1 are dropped
        return out;
    })));
    caps.ReplaceAll(u"x1y");
    CHECK(st.Peek().text == U("XY"));
    // Programmatic edits skip the transformation.
    st.SetTextAndPlaceCursorAtEnd("lower1");
    CHECK(st.Peek().text == U("lower1"));
    // The semantics of a length limit.
    ui::SemanticsConfiguration c;
    MaxLengthTrim(7)->ApplySemantics(c);
    CHECK(c.maxTextLength == std::optional(7));
}

TEST(OutputAndCodepointTransformationsMapOffsets) {
    TextFieldState st("123456");
    // A phone-like format: "123-456", a dash inserted after the third digit.
    auto dash = OutputTransformationOf([](TextFieldBuffer& b) { if (b.Length() > 3) b.Insert(3, u"-"); });
    TransformedTextFieldState tf(st, nullptr, nullptr, dash);
    CHECK(tf.VisualText().text == U("123-456"));
    CHECK(tf.VisualText().selection == TextRange(7));
    CHECK(tf.MapFromTransformed(5) == TextRange(4));
    CHECK(tf.MapToTransformed(2) == TextRange(2));
    CHECK(tf.MapToTransformed(3) == (TextRange{ 3, 4 }));   // the wedge: before or after the dash
    CHECK(tf.IndexTransformation(3) == IndexTransformationType::Insertion);
    tf.PlaceCursorBeforeCharAt(5);
    CHECK(st.Peek().selection == TextRange(4));
    // A password mask keeps offsets one for one (a surrogate pair is one bullet).
    TextFieldState pw("a\xF0\x9F\x94\xA5");
    TransformedTextFieldState masked(pw, nullptr, MaskCodepoints(0x2022));
    CHECK(masked.VisualText().text == U("\xE2\x80\xA2\xE2\x80\xA2"));
    CHECK(masked.MapFromTransformed(TextRange(1, 2)) == (TextRange{ 1, 3 }));
}

TEST(OnTextChangedHearsOnlyTextChanges) {
    TextFieldState st("a");
    std::vector<std::u16string> seen;
    st.SetOnTextChanged([&](const std::u16string& t) { seen.push_back(t); });
    TransformedTextFieldState tf(st);
    tf.ReplaceSelectedText(u"b");
    tf.SelectAll();                      // selection only: not told
    st.SetTextAndPlaceCursorAtEnd("ab"); // same text: not told
    CHECK(seen.size() == 1 && seen[0] == U("ab"));
    // Editing from the callback is an error.
    bool threw = false;
    st.SetOnTextChanged([&](const std::u16string&) {
        try { st.ClearText(); } catch (const std::logic_error&) { threw = true; }
    });
    tf.ReplaceSelectedText(u"c");
    CHECK(threw);
}

TEST(FocusStartsAndStopsTheInputSession) {
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    rt::Composition comp(owner.Root(), rt::Dispatcher::Main());
    int starts = 0, stops = 0;
    ui::ImeOptions seen;
    ui::TextInputHost host;
    host.start = [&](const ui::ImeOptions& o, const ui::TextInputSnapshot&) { ++starts; seen = o; };
    host.stop = [&] { ++stops; };
    owner.SetTextInputHost(host);
    TextFieldState state("hi");
    BasicTextFieldOptions opt;
    opt.keyboardOptions.keyboardType = ui::KeyboardType::Email;
    opt.lineLimits = TextFieldLineLimits::SingleLine();
    comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, opt); }); });
    comp.Recompose();
    owner.MeasureAndLayout({ 200, 100 });
    owner.MoveFocus(ui::FocusDirection::Next);
    CHECK_EQ(starts, 1);
    CHECK(seen.keyboardType == ui::KeyboardType::Email);
    CHECK(owner.ActiveTextInput() != nullptr);
    owner.ClearFocus(true);
    CHECK_EQ(stops, 1);
    CHECK(owner.ActiveTextInput() == nullptr);
}

TEST(HardwareKeysTypeAndCombineDeadKeys) {
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    owner.SetDeadCharCombiner([](int accent, int c) { return accent == 0xB4 && c == 'e' ? 0xE9 : 0; });   // ´ + e = é
    rt::Composition comp(owner.Root(), rt::Dispatcher::Main());
    TextFieldState state("");
    comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, {}); }); });
    comp.Recompose();
    owner.MeasureAndLayout({ 200, 100 });
    owner.MoveFocus(ui::FocusDirection::Next);
    auto type = [&](std::uint32_t unicode) {
        ui::KeyEvent d{ 0, ui::KeyEventType::KeyDown };
        d.unicode = unicode;
        owner.DispatchKeyEvent(d);
        owner.DispatchKeyEvent(ui::KeyEvent{ 0, ui::KeyEventType::KeyUp });
    };
    type('a');
    type(0x80000000u | 0xB4);   // dead acute accent: nothing yet
    CHECK(state.Peek().text == U("a"));
    type('e');
    CHECK(state.Peek().text == U("a\xC3\xA9"));
    type(0x80000000u | 0xB4);
    type('x');                  // no ´x: the plain character
    CHECK(state.Peek().text == U("a\xC3\xA9x"));
    type(0x1B);                 // a control character is not typed
    CHECK(state.Peek().text == U("a\xC3\xA9x"));
}

namespace {
    // A field over the monospace FakeText: 10 px per character and 25 px lines at 20 sp.
    struct FieldHarness {
        test::FakeText backend;
        ui::Owner owner;
        rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
        std::int64_t t = 1000;
        int starts = 0, stops = 0, hides = 0;
        FieldHarness() {
            owner.SetDensity({ 1, 1 });
            owner.SetText(&backend);
            ui::TextInputHost host;
            host.start = [this](const ui::ImeOptions&, const ui::TextInputSnapshot&) { ++starts; };
            host.stop = [this] { ++stops; };
            host.hideKeyboard = [this] { ++hides; };
            owner.SetTextInputHost(host);
        }
        bool draw = false;   // draw each pump (nothing is rendered: draw-driven work runs)
        void Pump(ui::IntSize size = { 120, 300 }) {
            for (int i = 0; i < 3; ++i) {
                rt::Dispatcher::Main().RunPending();
                comp.Recompose();
                owner.MeasureAndLayout(size);
                if (draw) {
                    ui::DrawTarget target;
                    target.viewportWidth = float(size.width);
                    target.viewportHeight = float(size.height);
                    owner.Draw(target);
                }
            }
        }
        void Key(int keyCode, std::uint32_t meta = 0) {
            ui::KeyEvent d{ keyCode, ui::KeyEventType::KeyDown, meta };
            d.fromHardwareSource = true;
            owner.DispatchKeyEvent(d);
            ui::KeyEvent u{ keyCode, ui::KeyEventType::KeyUp, meta };
            owner.DispatchKeyEvent(u);
            Pump();
        }
        void Tap(float x, float y) {
            for (const bool down : { true, false }) {
                ui::RawPointerEvent e;
                e.type = down ? ui::PointerEventType::Press : ui::PointerEventType::Release;
                e.uptimeMillis = t += 16;
                e.pointers.push_back({ 1, { x, y }, down, ui::PointerType::Touch, {} });
                owner.ProcessPointer(e);
                Pump();
            }
        }
    };
    BasicTextFieldOptions Style20(BasicTextFieldOptions o = {}) {
        o.textStyle = text::TextStyle(text::Sp(20));
        return o;
    }
}

TEST(TextFieldSizesByLineLimitsAndMinWidth) {
    FieldHarness h;
    TextFieldState single(""), multi("one");
    ui::IntSize singleSize, multiSize;
    BasicTextFieldOptions s = Style20();
    s.lineLimits = TextFieldLineLimits::SingleLine();
    BasicTextFieldOptions m = Style20();
    m.lineLimits = TextFieldLineLimits::MultiLine(2, 3);
    h.comp.SetContent([&] {
        Column(Modifier{}, [&] {
            BasicTextField(single, OnSizeChanged(Modifier{}, [&](ui::IntSize z) { singleSize = z; }), s);
            BasicTextField(multi, OnSizeChanged(Modifier{}, [&](ui::IntSize z) { multiSize = z; }), m);
        });
    });
    h.Pump();
    // An empty field is 10 "H" wide and one line (its first line's bottom) tall.
    CHECK_EQ(singleSize.width, 100);
    CHECK_EQ(singleSize.height, 25);
    // minLines 2: two lines tall with one line of text; maxLines 3 caps five lines.
    CHECK_EQ(multiSize.height, 50);
    multi.SetTextAndPlaceCursorAtEnd("1\n2\n3\n4\n5");
    h.Pump();
    CHECK_EQ(multiSize.height, 75);
}

TEST(SingleLineFieldScrollsToKeepTheCursorVisible) {
    FieldHarness h;
    TextFieldState state("abcdefghijklmnopqrst");   // 200 px in a 120 px column
    ScrollState scroll;
    BasicTextFieldOptions o = Style20();
    o.lineLimits = TextFieldLineLimits::SingleLine();
    o.scrollState = &scroll;
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, o); }); });
    h.Pump();
    CHECK_EQ(scroll.MaxValue(), 80);
    CHECK_EQ(scroll.Value(), 0);   // not focused: the cursor is not followed
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump();
    CHECK_EQ(h.starts, 1);
    CHECK_EQ(scroll.Value(), 80);  // the cursor at the end comes into view
    h.Key(ui::keys::MoveHome);
    CHECK(state.Selection() == TextRange(0));
    CHECK_EQ(scroll.Value(), 0);
}

TEST(TextFieldKeysMoveByWordDeleteAndUndo) {
    FieldHarness h;
    TextFieldState state("hello big world");
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); }); });
    h.Pump({ 300, 300 });
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump({ 300, 300 });
    h.Key(ui::keys::DpadLeft, ui::kMetaCtrl);
    CHECK(state.Selection() == TextRange(10));
    h.Key(ui::keys::DpadLeft, ui::kMetaCtrl | ui::kMetaShift);
    CHECK(state.Selection() == TextRange(10, 6));
    h.Key(ui::keys::Del);
    CHECK(state.Peek().text == U("hello world"));
    h.Key(54, ui::kMetaCtrl);   // Ctrl+Z
    CHECK(state.Peek().text == U("hello big world"));
    h.Key(54, ui::kMetaCtrl | ui::kMetaShift);   // Ctrl+Shift+Z
    CHECK(state.Peek().text == U("hello world"));
}

TEST(ImeActionsRunTheHandlerOrTheDefault) {
    FieldHarness h;
    TextFieldState a("a"), b("b");
    int handled = 0;
    BasicTextFieldOptions first = Style20();
    first.lineLimits = TextFieldLineLimits::SingleLine();
    first.keyboardOptions.imeAction = ui::ImeAction::Next;
    BasicTextFieldOptions second = first;
    second.keyboardOptions.imeAction = ui::ImeAction::Done;
    second.onKeyboardAction = [&](std::function<void()> performDefault) { ++handled; performDefault(); };
    h.comp.SetContent([&] {
        Column(Modifier{}, [&] {
            BasicTextField(a, {}, first);
            BasicTextField(b, {}, second);
        });
    });
    h.Pump();
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump();
    CHECK(h.owner.ActiveTextInput() != nullptr);
    h.owner.ActiveTextInput()->PerformImeAction(ui::ImeAction::Next);   // the default: focus moves on
    h.Pump();
    CHECK_EQ(h.starts, 2);
    h.Key(ui::keys::Enter);   // a single line field submits on Enter: the handler, then Done hides
    CHECK_EQ(handled, 1);
    CHECK_EQ(h.hides, 1);
    CHECK(b.Peek().text == U("b"));
}

TEST(TapFocusesAndPlacesTheCursor) {
    FieldHarness h;
    TextFieldState state("hello world");
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); }); });
    h.Pump({ 300, 300 });
    h.Tap(42, 10);   // focuses (the session starts on focus) and places the cursor at 4
    CHECK_EQ(h.starts, 1);
    CHECK(state.Selection() == TextRange(4));
    h.Tap(78, 10);
    CHECK(state.Selection() == TextRange(8));
    CHECK_EQ(h.starts, 1);
}

TEST(ImeSessionBatchesEditsAndReportsState) {
    FieldHarness h;
    std::vector<std::pair<ui::TextInputSnapshot, bool>> updates;
    std::vector<ui::CursorAnchorInfo> anchors;
    ui::TextInputHost host;
    host.start = [&](const ui::ImeOptions&, const ui::TextInputSnapshot&) { ++h.starts; };
    host.update = [&](const ui::TextInputSnapshot& s, bool changed) { updates.push_back({ s, changed }); };
    host.cursorAnchorInfo = [&](const ui::CursorAnchorInfo& c) { anchors.push_back(c); };
    h.owner.SetTextInputHost(host);
    TextFieldState state("ab");
    std::shared_ptr<const text::TextLayoutResult> laidOut;
    BasicTextFieldOptions o = Style20();
    o.onTextLayout = [&](const text::TextLayoutResult& r) { laidOut = std::make_shared<text::TextLayoutResult>(r); };
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, o); }); });
    h.Pump({ 300, 300 });
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump({ 300, 300 });
    ui::TextInputSession* s = h.owner.ActiveTextInput();
    CHECK(s != nullptr);
    // Edits inside a batch apply together when it ends.
    s->BeginBatchEdit();
    s->CommitText(U("c"), 1);
    s->SetComposingText(U("de"), 1);
    CHECK(state.Peek().text == U("ab"));
    CHECK(!s->EndBatchEdit());
    CHECK(state.Peek().text == U("abcde"));
    CHECK(!updates.empty() && updates.back().second);
    CHECK(updates.back().first.compStart == 3 && updates.back().first.compEnd == 5);
    // The composition draws underlined (DefaultComposingStyle).
    h.Pump({ 300, 300 });
    bool underlined = false;
    for (const auto& r : laidOut->LayoutInput().text.SpanStyles())
        if (r.start == 3 && r.end == 5 && r.item.textDecoration == text::TextDecoration::Underline) underlined = true;
    CHECK(underlined);
    // A selection-only change is reported as such; the IME sees min / max.
    s->SetSelection(4, 1);
    CHECK(updates.back().second && updates.back().first.selStart == 1 && updates.back().first.selEnd == 4);
    // Cursor anchor info: the insertion marker at the cursor and the composing characters.
    s->SetSelection(5, 5);
    s->RequestCursorUpdates(1 | 16 | 8);
    CHECK(!anchors.empty());
    const ui::CursorAnchorInfo& a = anchors.back();
    CHECK(a.hasInsertionMarker);
    CHECK_NEAR(a.markerX, 50, 1e-3f);
    CHECK_NEAR(a.markerBottom, 25, 1e-3f);
    CHECK(a.composingStart == 3 && a.composingText == U("de") && a.characterBounds.size() == 2);
    CHECK(!a.hasEditorBounds);
}

TEST(ContentReceiversTakeKeyboardContentAndPastes) {
    CHECK(ui::ClipMetadata::CompareMimeTypes("image/png", "image/*"));
    CHECK(ui::ClipMetadata::CompareMimeTypes("image/png", "*/*"));
    CHECK(!ui::ClipMetadata::CompareMimeTypes("image/*", "image/png"));   // wildcards on the desired side only
    FieldHarness h;
    bool sawAccepts = false;
    ui::TextInputHost host;
    host.start = [&](const ui::ImeOptions& o, const ui::TextInputSnapshot&) { ++h.starts; sawAccepts = o.acceptsContent; };
    h.owner.SetTextInputHost(host);
    std::vector<std::string> outer, inner;
    // The inner receiver takes images; the outer one sees what is left.
    auto innerListener = ReceiveContentListenerOf([&](TransferableContentPtr c) {
        inner.push_back(c->clipMetadata.label);
        return c->Consume([](const ui::ClipItem& i) { return i.uri.has_value(); }, c);
    });
    auto outerListener = ReceiveContentListenerOf([&](TransferableContentPtr c) { outer.push_back(c->clipMetadata.label); return c; });
    TextFieldState state("");
    h.comp.SetContent([&] {
        Column(ContentReceiver(Modifier{}, outerListener), [&] {
            BasicTextField(state, ContentReceiver(Modifier{}, innerListener), Style20());
        });
    });
    h.Pump({ 300, 300 });
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump({ 300, 300 });
    CHECK(sawAccepts);   // EditorInfo gets the content MIME types
    ui::TextInputSession* s = h.owner.ActiveTextInput();
    // A GIF from the keyboard: the inner receiver takes it, nothing reaches the outer one.
    CHECK(s->CommitContent(ui::ClipEntry{ { ui::ClipItem{ std::nullopt, std::nullopt, "content://gif/1" } } },
                           ui::ClipMetadata{ "gif", { "image/gif" } }, std::nullopt, 0));
    CHECK(inner.size() == 1 && outer.empty());
    // A pasted clip with an image and text: the image goes, the text is inserted.
    ui::Owner::ClipboardHost clip;
    clip.getText = [] { return std::optional<std::string>("hi"); };
    clip.getClip = [] {
        return std::optional(std::pair{ ui::ClipEntry{ { ui::ClipItem{ std::nullopt, std::nullopt, "content://img/2" }, ui::ClipItem{ "hi", std::nullopt, std::nullopt } } },
                                        ui::ClipMetadata{ "mixed", { "image/png", "text/plain" } } });
    };
    h.owner.SetClipboardHost(clip);
    h.Key(50, ui::kMetaCtrl);   // Ctrl+V
    CHECK(inner.size() == 2 && outer.size() == 1 && outer[0] == "mixed");
    CHECK(state.Peek().text == U("hi"));
}

TEST(StylusStrokeNearAFieldStartsHandwriting) {
    ui::SetStylusHandwritingSupported(true);
    FieldHarness h;
    int handwriting = 0;
    bool editorFlag = false;
    ui::TextInputHost host;
    host.start = [&](const ui::ImeOptions& o, const ui::TextInputSnapshot&) { ++h.starts; editorFlag = o.stylusHandwriting; };
    host.startStylusHandwriting = [&] { ++handwriting; };
    h.owner.SetTextInputHost(host);
    TextFieldState state("");
    h.comp.SetContent([&] {
        Column(Modifier{}, [&] {
            Box(SizeMod(Modifier{}, Dp(100), Dp(60)));
            BasicTextField(state, {}, Style20());   // y 60..85
        });
    });
    h.Pump({ 300, 300 });
    auto send = [&](ui::PointerType type, ui::PointerEventType kind, float x, float y, bool down) {
        ui::RawPointerEvent e;
        e.type = kind;
        e.uptimeMillis = h.t += 16;
        e.pointers.push_back({ 1, { x, y }, down, type, {} });
        h.owner.ProcessPointer(e);
        h.Pump({ 300, 300 });
    };
    // A finger in the expansion above the field is not near enough (no direct hit).
    send(ui::PointerType::Touch, ui::PointerEventType::Press, 20, 30, true);
    send(ui::PointerType::Touch, ui::PointerEventType::Move, 40, 30, true);
    send(ui::PointerType::Touch, ui::PointerEventType::Release, 40, 30, false);
    CHECK_EQ(h.starts, 0);
    // A stylus 30 px above the field (inside its 40 dp expansion) moving past the slop.
    send(ui::PointerType::Stylus, ui::PointerEventType::Press, 20, 30, true);
    send(ui::PointerType::Stylus, ui::PointerEventType::Move, 30, 30, true);
    CHECK_EQ(h.starts, 1);   // focused: the session started
    CHECK(editorFlag);
    CHECK_EQ(handwriting, 0);   // after the session's first frame
    rt::Dispatcher::Main().RunFrame(1'000'000);
    h.Pump({ 300, 300 });
    CHECK_EQ(handwriting, 1);
    send(ui::PointerType::Stylus, ui::PointerEventType::Release, 60, 30, false);
    CHECK(state.Peek().text.empty());
    ui::SetStylusHandwritingSupported(false);
}

TEST(RangeForRectFindsWordsAndCharacters) {
    test::FakeText backend;
    const ui::Density d{ 1, 1 };
    text::TextLayoutInput in;
    in.text = text::AnnotatedString("hello big world\nsecond line");
    in.style = text::TextStyle(text::Sp(20));
    in.density = d;
    const text::TextLayoutResult l = text::LayoutText(&backend, in);
    using text::TextGranularity;
    // "big" is 60..90 px on the first line (10 px characters, 25 px lines).
    CHECK(l.RangeForRect({ 58, 5, 95, 20 }, TextGranularity::Word, text::inclusion::ContainsCenter()) == std::pair(6, 9));
    CHECK(l.RangeForRect({ 62, 5, 78, 20 }, TextGranularity::Character, text::inclusion::ContainsCenter()) == std::pair(6, 8));
    // Two lines: "big world" on the first ("big" centred at 75), "line" (70..110) on the second.
    CHECK(l.RangeForRect({ 65, 5, 300, 45 }, TextGranularity::Word, text::inclusion::ContainsCenter()) == std::pair(6, 27));
    // Nothing on the second line has its centre in the rect: the range ends on the first.
    CHECK(l.RangeForRect({ 100, 5, 300, 45 }, TextGranularity::Word, text::inclusion::ContainsCenter()) == std::pair(10, 15));
    // Nothing under the rect.
    CHECK(l.RangeForRect({ 0, 80, 50, 90 }, TextGranularity::Word, text::inclusion::ContainsCenter()) == std::pair(0, 0));
}

TEST(HandwritingGesturesEditTheField) {
    FieldHarness h;
    TextFieldState state("hello big world");
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); }); });
    h.Pump({ 300, 300 });
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump({ 300, 300 });
    ui::TextInputSession* s = h.owner.ActiveTextInput();
    using G = ui::HandwritingGesture;
    // Preview then delete "big" by word: the space before it goes too.
    G del;
    del.type = G::Type::Delete;
    del.granularity = G::Granularity::Word;
    del.area = { 58, 2, 95, 22 };
    CHECK(s->PreviewHandwritingGesture(del));
    CHECK(state.Peek().highlight.has_value() && state.Peek().highlight->second == TextRange(6, 9));
    CHECK_EQ(s->PerformHandwritingGesture(del), G::kResultSuccess);
    CHECK(state.Peek().text == U("hello world"));
    h.Pump({ 300, 300 });
    // Select "world".
    G sel;
    sel.type = G::Type::Select;
    sel.granularity = G::Granularity::Word;
    sel.area = { 60, 2, 110, 22 };
    CHECK_EQ(s->PerformHandwritingGesture(sel), G::kResultSuccess);
    CHECK(state.Selection() == TextRange(6, 11));
    // Split "hello" after "hel", then join it back.
    G split;
    split.type = G::Type::JoinOrSplit;
    split.point = { 30, 12 };
    CHECK_EQ(s->PerformHandwritingGesture(split), G::kResultSuccess);
    CHECK(state.Peek().text == U("hel lo world"));
    h.Pump({ 300, 300 });
    CHECK_EQ(s->PerformHandwritingGesture(split), G::kResultSuccess);
    CHECK(state.Peek().text == U("hello world"));
    h.Pump({ 300, 300 });
    // Insert at a point; remove the spaces between two points.
    G ins;
    ins.type = G::Type::Insert;
    ins.point = { 50, 12 };
    ins.textToInsert = u",";
    CHECK_EQ(s->PerformHandwritingGesture(ins), G::kResultSuccess);
    CHECK(state.Peek().text == U("hello, world"));
    h.Pump({ 300, 300 });
    G rm;
    rm.type = G::Type::RemoveSpace;
    rm.point = { 55, 12 };
    rm.endPoint = { 75, 12 };
    CHECK_EQ(s->PerformHandwritingGesture(rm), G::kResultSuccess);
    CHECK(state.Peek().text == U("hello,world"));
    // A gesture over nothing falls back to its text, or fails without one.
    G miss = sel;
    miss.area = { 0, 200, 10, 210 };
    CHECK_EQ(s->PerformHandwritingGesture(miss), G::kResultFailed);
    miss.fallbackText = u"!";
    CHECK_EQ(s->PerformHandwritingGesture(miss), G::kResultFallback);
}

namespace {
    void SendPointer(FieldHarness& h, ui::PointerType type, ui::PointerEventType kind, float x, float y, bool down, std::uint32_t buttons = 0) {
        ui::RawPointerEvent e;
        e.type = kind;
        e.uptimeMillis = h.t += 16;
        e.buttons = buttons;
        e.pointers.push_back({ 1, { x, y }, down, type, {} });
        h.owner.ProcessPointer(e);
        h.Pump({ 300, 300 });
    }
    void WaitMs(FieldHarness& h, int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        h.t += ms;
        h.Pump({ 300, 300 });
    }
    // The field's handle layer: the second child of the content-with-handles layout.
    std::vector<ui::LayoutNode*> FieldHandles(FieldHarness& h) {
        std::vector<ui::LayoutNode*> out;
        std::function<void(ui::LayoutNode*)> walk = [&](ui::LayoutNode* n) {
            if (n->Children().size() == 2 && n->Children()[1]->ZIndex() == 1) {
                for (auto* c : n->Children()[1]->Children()) out.push_back(c);
                return;
            }
            for (auto* c : n->Children()) walk(c);
        };
        walk(&h.owner.Root());
        return out;
    }
}

TEST(LongPressSelectsAWordWithHandlesAndToolbar) {
    FieldHarness h;
    h.owner.Config().longPressTimeoutMillis = 40;
    TextFieldState state("hello big world");
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); }); });
    h.Pump({ 300, 300 });
    using ui::PointerEventType;
    using ui::PointerType;
    // Long press on "big": the word selected; focus (and the toolbar) wait for the release.
    SendPointer(h, PointerType::Touch, PointerEventType::Press, 75, 10, true);
    WaitMs(h, 80);
    CHECK(state.Selection() == TextRange(6, 9));
    CHECK_EQ(h.starts, 0);
    // Dragging extends by words.
    SendPointer(h, PointerType::Touch, PointerEventType::Move, 110, 10, true);
    SendPointer(h, PointerType::Touch, PointerEventType::Move, 125, 10, true);
    CHECK(state.Selection() == TextRange(6, 15));
    SendPointer(h, PointerType::Touch, PointerEventType::Release, 125, 10, false);
    CHECK_EQ(h.starts, 1);
    CHECK(h.owner.TextToolbarShown());
    CHECK((tests::ToolbarIds(h.owner) == std::vector{ ui::TextToolbarItemId::Cut, ui::TextToolbarItemId::Copy, ui::TextToolbarItemId::SelectAll }));
    // Two handles at the ends: the start handle's point at x 60 (left of it), the end at 150.
    auto handles = FieldHandles(h);
    CHECK_EQ(int(handles.size()), 2);
    if (handles.size() == 2) {
        CHECK_EQ(handles[0]->Position().x, 60 - 40);
        CHECK_EQ(handles[0]->Position().y, 25);
        CHECK_EQ(handles[1]->Position().x, 150);
    }
    // Dragging the end handle back to "big"'s end (character drag with word acceleration).
    if (handles.size() == 2) {
        const float x = 150 + 5, y = 25 + 10;
        SendPointer(h, PointerType::Touch, PointerEventType::Press, x, y, true);
        SendPointer(h, PointerType::Touch, PointerEventType::Move, x - 20, y, true);
        SendPointer(h, PointerType::Touch, PointerEventType::Move, x - 60, y, true);
        CHECK(!h.owner.TextToolbarShown());   // hidden while a handle is dragged
        SendPointer(h, PointerType::Touch, PointerEventType::Release, x - 60, y, false);
        CHECK(state.Selection().start == 6 && state.Selection().end < 15);
        CHECK(h.owner.TextToolbarShown());
    }
    // Cut from the toolbar.
    CHECK(tests::ClickToolbar(h.owner, ui::TextToolbarItemId::Cut));
    h.Pump({ 300, 300 });
    CHECK(h.owner.ClipboardText().has_value());
    CHECK(state.Peek().text.size() < 15);
    CHECK(!h.owner.TextToolbarShown());
}

TEST(TextContextMenuBuilderTidiesAndFilters) {
    TextContextMenuBuilderScope b;
    auto noop = [](TextContextMenuSession&) {};
    b.Separator();
    b.Item(TextContextMenuKeys::Cut, {}, noop);
    b.Separator();
    b.Separator();
    b.Item(TextContextMenuKeys::Copy, {}, noop);
    b.Item(TextContextMenuKeys::Paste, {}, noop);
    b.Separator();
    b.AddFilter([](const TextContextMenuComponent& c) { return KeyOf(c) != std::optional(TextContextMenuKeys::Copy); });
    const auto entries = ToolbarEntriesOf(b.Build());
    CHECK_EQ(int(entries.size()), 2);
    if (entries.size() == 2) {
        CHECK(entries[0].id == ui::TextToolbarItemId::Cut && entries[0].group == 1);
        CHECK(entries[1].id == ui::TextToolbarItemId::Paste && entries[1].group == 2);
    }
}

TEST(SmartSelectionGrowsTheWordAndLeadsTheToolbar) {
    FieldHarness h;
    h.owner.Config().longPressTimeoutMillis = 40;
    int classified = 0, released = 0, processed = -1;
    std::u16string processedText;
    ui::TextClassifierHost host;
    host.createSession = [](ui::SelectedTextType t) { return t == ui::SelectedTextType::EditableText ? 1 : 2; };
    // The classifier sees an address in the whole text, with two actions.
    host.suggestSelection = [](int, const std::u16string& text, int, int, const std::vector<std::string>&, auto done) {
        done(std::pair{ 0, int(text.size()) }, ui::TextClassificationInfo{ 7, { "Map", "Share" }, false });
    };
    host.classifyText = [&](int, const std::u16string&, int, int, const std::vector<std::string>&, auto done) {
        ++classified;
        done(ui::TextClassificationInfo{ 8, { "Other" }, false });
    };
    host.releaseClassification = [&](int) { ++released; };
    host.processTextActivities = [] { return std::vector<std::string>{ "Translate" }; };
    host.processText = [&](int index, bool, const std::u16string& t) { processed = index; processedText = t; };
    h.owner.SetTextClassifierHost(host);
    TextFieldState state("hello big world");
    // A component from further out comes after the field's own.
    h.comp.SetContent([&] {
        Column(AppendTextContextMenuComponents(Modifier{}, [](TextContextMenuBuilderScope& b) { b.Item({ &b, 1 }, "Mine", [](TextContextMenuSession&) {}); }),
               [&] { BasicTextField(state, {}, Style20()); });
    });
    h.Pump({ 300, 300 });
    using ui::PointerEventType;
    using ui::PointerType;
    SendPointer(h, PointerType::Touch, PointerEventType::Press, 75, 10, true);
    WaitMs(h, 80);
    SendPointer(h, PointerType::Touch, PointerEventType::Release, 75, 10, false);
    h.Pump({ 300, 300 });
    // The suggestion replaced "big"; its classification was kept, so showing classified nothing.
    CHECK(state.Selection() == TextRange(0, 15));
    CHECK_EQ(classified, 0);
    CHECK(h.owner.TextToolbarShown());
    const auto& e = h.owner.TextToolbarEntries();
    using Kind = ui::TextToolbarEntry::Kind;
    CHECK_EQ(int(e.size()), 6);
    if (e.size() == 6) {
        CHECK(e[0].kind == Kind::Classification && e[0].classification == 7 && e[0].index == 0);
        CHECK(e[1].id == ui::TextToolbarItemId::Cut && e[2].id == ui::TextToolbarItemId::Copy);   // all selected: no Select all
        CHECK(e[3].kind == Kind::Classification && e[3].index == 1);
        CHECK(e[4].label == "Translate" && e[4].group > e[3].group);
        CHECK(e[5].label == "Mine");
    }
    // Process text: the selected text to the activity; the toolbar closes.
    if (e.size() == 6) h.owner.OnTextToolbarItem(4);
    h.Pump({ 300, 300 });
    CHECK_EQ(processed, 0);
    CHECK(processedText == U("hello big world"));
    CHECK(!h.owner.TextToolbarShown());
}

TEST(DoubleTapAndMouseDragSelect) {
    FieldHarness h;
    TextFieldState state("hello big world");
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); }); });
    h.Pump({ 300, 300 });
    using ui::PointerEventType;
    using ui::PointerType;
    // Double tap on "world" selects it.
    SendPointer(h, PointerType::Touch, PointerEventType::Press, 125, 10, true);
    SendPointer(h, PointerType::Touch, PointerEventType::Release, 125, 10, false);
    SendPointer(h, PointerType::Touch, PointerEventType::Press, 125, 10, true);
    SendPointer(h, PointerType::Touch, PointerEventType::Release, 125, 10, false);
    CHECK(state.Selection() == TextRange(10, 15));
    // A mouse drag selects characters; no handles for a mouse.
    WaitMs(h, 400);
    SendPointer(h, PointerType::Mouse, PointerEventType::Press, 10, 10, true, 1);
    SendPointer(h, PointerType::Mouse, PointerEventType::Move, 30, 10, true, 1);
    SendPointer(h, PointerType::Mouse, PointerEventType::Move, 52, 10, true, 1);
    SendPointer(h, PointerType::Mouse, PointerEventType::Release, 52, 10, false, 0);
    CHECK(state.Selection() == TextRange(1, 5));
    CHECK(FieldHandles(h).empty());
}

TEST(MagnifierFollowsTheDraggedHandle) {
    FieldHarness h;
    h.owner.Config().longPressTimeoutMillis = 40;
    int creates = 0, dismisses = 0;
    std::vector<Offset> shown;
    ui::Owner::MagnifierHost mag;
    mag.create = [&](const ui::Owner::MagnifierConfig& c) { ++creates; CHECK(c.useTextDefault); return creates; };
    mag.show = [&](int, Offset src, Offset, float) { shown.push_back(src); };
    mag.dismiss = [&](int) { ++dismisses; };
    mag.destroy = [](int) {};
    mag.size = [](int) { return ui::IntSize{ 100, 48 }; };
    h.owner.SetMagnifierHost(mag);
    h.draw = true;
    TextFieldState state("hello big world");
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); }); });
    h.Pump({ 300, 300 });
    using ui::PointerEventType;
    using ui::PointerType;
    // A long press selects "big"; the drag shows the magnifier on the line's centre (y 12.5).
    SendPointer(h, PointerType::Touch, PointerEventType::Press, 75, 10, true);
    WaitMs(h, 80);
    SendPointer(h, PointerType::Touch, PointerEventType::Move, 110, 10, true);
    SendPointer(h, PointerType::Touch, PointerEventType::Move, 125, 10, true);
    CHECK_EQ(creates, 1);
    CHECK(!shown.empty());
    if (!shown.empty()) {
        CHECK_NEAR(shown.back().y, 12.5f, 0.01f);
        CHECK(shown.back().x > 100 && shown.back().x <= 150);
    }
    SendPointer(h, PointerType::Touch, PointerEventType::Release, 125, 10, false);
    CHECK(dismisses >= 1);
}
