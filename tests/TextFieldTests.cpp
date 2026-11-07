#include "Check.h"
#include "FakeText.h"

#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/Pointer.h"

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
        void Pump(ui::IntSize size = { 120, 300 }) {
            for (int i = 0; i < 3; ++i) {
                rt::Dispatcher::Main().RunPending();
                comp.Recompose();
                owner.MeasureAndLayout(size);
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
