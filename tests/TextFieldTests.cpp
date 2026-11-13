#include "Check.h"
#include <kiln/Function.h>
#include "kiln/foundation/text/input/TextFieldValue.h"
#include "kiln/ui/Back.h"
#include "kiln/foundation/contextmenu/ContextMenuUi.h"
#include "kiln/ui/Semantics.h"
#include "ToolbarHelpers.h"
#include "FakeText.h"

#include "kiln/foundation/text/handwriting/StylusHandwriting.h"
#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/foundation/text/contextmenu/TextContextMenuProvider.h"
#include "kiln/foundation/text/BasicSecureTextField.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/content/ReceiveContent.h"
#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include "kiln/ui/Draw.h"
#include "kiln/ui/Pointer.h"

#include <chrono>

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
                rt::Composition::RecomposeAll();
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
    second.onKeyboardAction = [&](kiln::Function<void()> performDefault) { ++handled; performDefault(); };
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
        rt::Dispatcher::Main().AdvanceTimeBy(std::chrono::milliseconds(ms));
        h.t += ms;
        h.Pump({ 300, 300 });
    }
    // The handle popups: where each sits relative to its anchor (the layout it is called in).
    struct HandleAt {
        ui::IntOffset at;
        ui::IntOffset Position() const { return at; }
    };
    std::vector<HandleAt> PopupHandles(ui::Owner& owner) {
        std::vector<HandleAt> out;
        for (ui::PopupLayer* l : owner.Popups()) {
            const auto anchor = l->anchorBounds ? l->anchorBounds() : std::nullopt;
            if (!l->bounds || !anchor) continue;
            out.push_back({ { l->bounds->left - anchor->left, l->bounds->top - anchor->top } });
        }
        return out;
    }
    std::vector<HandleAt> FieldHandles(FieldHarness& h) { return PopupHandles(h.owner); }
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
        CHECK_EQ(handles[0].Position().x, 60 - 40);
        CHECK_EQ(handles[0].Position().y, 25);
        CHECK_EQ(handles[1].Position().x, 150);
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

TEST(SecureFieldMasksAndRevealsTheLastTypedCharacter) {
    FieldHarness h;
    TextFieldState state("");
    std::string shown;
    BasicSecureTextFieldOptions o;
    o.textStyle = text::TextStyle(text::Sp(20));
    o.onTextLayout = [&](const text::TextLayoutResult& r) { shown = r.LayoutInput().text.Text(); };
    auto mode = std::make_shared<rt::State<TextObfuscationMode>>(TextObfuscationMode::System);
    h.comp.SetContent([&] {
        o.textObfuscationMode = mode->Get();
        Column(Modifier{}, [&] { BasicSecureTextField(state, {}, o); });
    });
    h.Pump({ 300, 300 });
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump({ 300, 300 });
    ui::TextInputSession* s = h.owner.ActiveTextInput();
    CHECK(s != nullptr);
    if (!s) return;
    // Typed on the screen keyboard: the last character shows, the earlier ones are masked.
    s->CommitText(U("a"), 1);
    s->CommitText(U("b"), 1);
    h.Pump({ 300, 300 });
    CHECK(shown == "\u2022b");
    CHECK(state.Peek().text == U("ab"));
    // 1.5 s later it is masked too.
    WaitMs(h, 1600);
    CHECK(shown == "\u2022\u2022");
    // The setting off for screen keyboards: nothing shows.
    h.owner.SetPasswordVisibility(2);
    s->CommitText(U("c"), 1);
    h.Pump({ 300, 300 });
    CHECK(shown == "\u2022\u2022\u2022");
    // RevealLastTyped ignores the setting; Hidden never reveals.
    mode->Set(TextObfuscationMode::RevealLastTyped);
    h.Pump({ 300, 300 });
    s = h.owner.ActiveTextInput();
    if (s) s->CommitText(U("d"), 1);
    h.Pump({ 300, 300 });
    CHECK(shown == "\u2022\u2022\u2022d");
    mode->Set(TextObfuscationMode::Hidden);
    h.Pump({ 300, 300 });
    CHECK(shown == "\u2022\u2022\u2022\u2022");
}

TEST(FieldLaysOutItsTextStyles) {
    FieldHarness h;
    TextFieldState state("hello");
    state.Edit([](TextFieldBuffer& b) {
        text::SpanStyle s;
        s.fontWeight = text::FontWeight::Bold();
        b.AddStyle(s, 1, 3);
    });
    std::vector<text::Range<text::SpanStyle>> laidOut;
    BasicTextFieldOptions o = Style20();
    o.onTextLayout = [&](const text::TextLayoutResult& r) { laidOut = r.LayoutInput().text.SpanStyles(); };
    h.comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, o); }); });
    h.Pump({ 300, 300 });
    CHECK(laidOut.size() == 1 && laidOut[0].start == 1 && laidOut[0].end == 3);
}

TEST(FieldAutofillNotifiesFillsAndDescribesItself) {
    FieldHarness h;
    h.draw = true;
    std::vector<std::string> log;
    ui::AutofillHost host;
    host.notifyViewEntered = [&](int, ui::IntRect) { log.push_back("entered"); };
    host.notifyViewExited = [&](int) { log.push_back("exited"); };
    host.notifyValueChanged = [&](int, const ui::FillableData& d) { log.push_back("value " + d.text); };
    host.notifyViewVisibilityChanged = [&](int, bool v) { log.push_back(v ? "visible" : "invisible"); };
    host.commit = [&] { log.push_back("commit"); };
    host.requestAutofill = [&](int, ui::IntRect) { log.push_back("request"); };
    h.owner.SetAutofillHost(host);
    TextFieldState state("ab");
    BasicTextFieldOptions o = Style20();
    o.keyboardOptions.keyboardType = ui::KeyboardType::Email;
    auto shown = std::make_shared<rt::State<bool>>(true);
    h.comp.SetContent([&] { Column(Modifier{}, [&] { if (shown->Get()) BasicTextField(state, {}, o); }); });
    h.Pump({ 300, 300 });
    // A field with a content type shows up for autofill.
    CHECK(std::find(log.begin(), log.end(), "visible") != log.end());
    log.clear();
    h.owner.MoveFocus(ui::FocusDirection::Next);
    h.Pump({ 300, 300 });
    CHECK(std::find(log.begin(), log.end(), "entered") != log.end());
    // Its structure: an EditText of type text, holding its text, hinted as an email address.
    const auto structure = h.owner.Autofill()->Structure();
    const ui::AutofillStructureNode* node = nullptr;
    for (const auto& n : structure) if (n.autofillType == ui::autofill::kTypeText) node = &n;
    CHECK(node != nullptr);
    if (node) {
        CHECK(node->className == std::optional<std::string>("android.widget.EditText"));
        CHECK(node->value && node->value->text == "ab");
        CHECK((node->hints == std::vector<std::string>{ "emailAddress" }));
        CHECK(node->parent == 0);
    }
    // Typing reports the value.
    log.clear();
    if (auto* s = h.owner.ActiveTextInput()) s->CommitText(U("c"), 1);
    h.Pump({ 300, 300 });
    CHECK(std::find(log.begin(), log.end(), "value abc") != log.end());
    // Autofill fills it and highlights it until the next edit.
    if (node) {
        ui::FillableData d;
        d.text = "me@example.com";
        h.owner.Autofill()->PerformAutofill(node->id, d);
    }
    h.Pump({ 300, 300 });
    CHECK(state.Peek().text == U("me@example.com"));
    // Leaving: the field goes and, with nothing with a content type left, the session commits.
    log.clear();
    shown->Set(false);
    h.Pump({ 300, 300 });
    h.Pump({ 300, 300 });
    CHECK(std::find(log.begin(), log.end(), "invisible") != log.end());
    CHECK(std::find(log.begin(), log.end(), "commit") != log.end());
}

TEST(CredentialRequestsReachTheStructureAndTheirCallback) {
    FieldHarness h;
    ui::AutofillHost host;
    host.credentialRequestsSupported = true;
    h.owner.SetAutofillHost(host);
    std::string received;
    auto request = std::make_shared<ui::CredentialRequestData>();
    request->options = { ui::GetPasswordOption(), ui::GetPublicKeyCredentialOption("{\"challenge\":\"x\"}") };
    request->onResult = [&](const ui::Credential& c) {
        const std::string* id = c.data.GetString(ui::credentials::kBundleKeyId);
        received = c.type + " " + (id ? *id : "");
    };
    h.comp.SetContent([&] {
        Column(Modifier{}, [&] {
            Box(SizeMod(Semantics(Modifier{}, [request](ui::SemanticsConfiguration& c) { c.credentialRequest = request; }), Dp(50), Dp(20)));
        });
    });
    h.Pump({ 300, 300 });
    const auto structure = h.owner.Autofill()->Structure();
    const ui::AutofillStructureNode* node = nullptr;
    for (const auto& n : structure) if (n.credentialRequest) node = &n;
    CHECK(node != nullptr);
    if (node) {
        // androidx.credentials' bundles: the password option's allowed ids and priority.
        const auto& password = node->credentialRequest->options[0];
        CHECK(password.type == "android.credentials.TYPE_PASSWORD_CREDENTIAL");
        const auto* priority = password.requestData.Get(ui::credentials::kBundleKeyTypePriorityValue);
        CHECK(priority && std::get<std::int32_t>(*priority) == 1000);
        ui::Credential c;
        c.type = ui::credentials::kTypePasswordCredential;
        c.data.Put(ui::credentials::kBundleKeyId, std::string("me"));
        h.owner.Autofill()->OnCredentialResult(node->id, c);
    }
    CHECK(received == "android.credentials.TYPE_PASSWORD_CREDENTIAL me");
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
    int contentUpdates = 0;
    mag.updateContent = [&](int) { ++contentUpdates; };
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
    // The magnifier copies the content again once the frame drawing the new selection is out.
    CHECK_EQ(contentUpdates, 0);
    for (std::int64_t f = 1; f <= 3; ++f) {
        rt::Dispatcher::Main().RunFrame(f * 16'000'000);
        h.Pump({ 300, 300 });
    }
    CHECK(contentUpdates >= 1);
    SendPointer(h, PointerType::Touch, PointerEventType::Release, 125, 10, false);
    CHECK(dismisses >= 1);
}

TEST(ContextMenuPopupFlipsToStayInTheWindow) {
    // Fits after the point: starts there; else ends there; else against the far edge.
    CHECK_EQ(AlignPopupAxis(10, 50, 100), 10);
    CHECK_EQ(AlignPopupAxis(80, 50, 100), 30);
    CHECK_EQ(AlignPopupAxis(40, 70, 100), 30);
    CHECK_EQ(AlignPopupAxis(10, 120, 100), 0);
    CHECK_EQ(AlignPopupAxis(80, 50, 100, false), 30);   // RTL: ends at the point when it fits before
}

TEST(RightClickOpensTheDropdownTextContextMenu) {
    FieldHarness h;
    ui::ContextMenuHost host;
    host.standardLabel = [](ui::TextToolbarItemId id) {
        switch (id) {
            case ui::TextToolbarItemId::Cut: return std::string("Cut");
            case ui::TextToolbarItemId::Copy: return std::string("Copy");
            case ui::TextToolbarItemId::Paste: return std::string("Paste");
            case ui::TextToolbarItemId::SelectAll: return std::string("Select all");
            default: return std::string();
        }
    };
    h.owner.SetContextMenuHost(host);
    TextFieldState state("hello big world");
    h.comp.SetContent([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] { Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); }); });
    });
    h.Pump({ 300, 300 });
    using ui::PointerEventType;
    using ui::PointerType;
    // A right click: a focusable popup at the click with the field's items.
    SendPointer(h, PointerType::Mouse, PointerEventType::Press, 75, 10, true, ui::PointerButtons::Secondary);
    SendPointer(h, PointerType::Mouse, PointerEventType::Release, 75, 10, false, 0);
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    if (h.owner.Popups().size() != 1) return;
    CHECK(h.owner.Popups()[0]->properties.focusable);
    const auto b = *h.owner.Popups()[0]->bounds;
    CHECK_EQ(b.left, 75);
    CHECK_EQ(b.top, 10);
    CHECK_EQ(b.right - b.left, 112);   // ContainerWidthMin
    // The items: Select all is there (the field has text); clicking it selects and closes.
    std::vector<std::string> labels;
    kiln::Function<void(ui::LayoutNode*)> walk = [&](ui::LayoutNode* n) {
        ui::SemanticsConfiguration cfg;
        if (ui::CalculateSemanticsConfiguration(*n, cfg) && cfg.onClickLabel) labels.push_back(*cfg.onClickLabel);
        for (auto* c : n->Children()) walk(c);
    };
    walk(h.owner.Popups()[0]->node);
    CHECK(std::find(labels.begin(), labels.end(), "Select all") != labels.end());
    CHECK(std::find(labels.begin(), labels.end(), "Paste") == labels.end());   // empty clipboard
    // The last item (Select all) is the bottom 48 dp row above the 8 dp padding.
    const float x = float(b.left) + 30, y = float(b.bottom) - 8 - 24;
    SendPointer(h, PointerType::Mouse, PointerEventType::Press, x, y, true, ui::PointerButtons::Primary);
    SendPointer(h, PointerType::Mouse, PointerEventType::Release, x, y, false, 0);
    CHECK(state.Selection() == TextRange(0, 15));
    CHECK(h.owner.Popups().empty());
}

TEST(RightClickOpensTheMenuOfAPaddedSingleLineField) {
    FieldHarness h;
    ui::ContextMenuHost host;
    host.standardLabel = [](ui::TextToolbarItemId id) { return id == ui::TextToolbarItemId::SelectAll ? std::string("Select all") : std::string("X"); };
    h.owner.SetContextMenuHost(host);
    TextFieldState state("Edit me");
    h.comp.SetContent([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] {
            Column(Modifier{}, [&] {
                Box(Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 0_dp), Alignment::CenterStart(), false, [&] {
                    BasicTextFieldOptions opt = Style20();
                    opt.lineLimits = TextFieldLineLimits::SingleLine();
                    BasicTextField(state, Padding(FillMax(Modifier{}, Axis::Horizontal), 16_dp, 16_dp), opt);
                });
            });
        });
    });
    h.Pump({ 300, 300 });
    SendPointer(h, ui::PointerType::Mouse, ui::PointerEventType::Press, 100, 30, true, ui::PointerButtons::Secondary);
    SendPointer(h, ui::PointerType::Mouse, ui::PointerEventType::Release, 100, 30, false, 0);
    CHECK_EQ(int(h.owner.Popups().size()), 1);
    // Back (the window's back callback, not a key) dismisses the menu.
    CHECK(ui::BackDispatcher::Main().Completed());
    h.Pump({ 300, 300 });
    CHECK(h.owner.Popups().empty());
}

TEST(SoftwareKeyboardControllerDefaultsToTheOwnersAndCanBeProvided) {
    FieldHarness h;
    struct Counting final : ui::SoftwareKeyboardController {
        int shows = 0, hides = 0;
        void Show() override { ++shows; }
        void Hide() override { ++hides; }
    };
    auto custom = std::make_shared<Counting>();
    std::shared_ptr<ui::SoftwareKeyboardController> fromOwner, provided;
    h.comp.SetContent([&] {
        rt::Composition::Current()->Provide(ui::LocalOwner, &h.owner, [&] {
            fromOwner = ui::CurrentSoftwareKeyboardController();
            rt::Composition::Current()->Provide(ui::LocalSoftwareKeyboardController, std::shared_ptr<ui::SoftwareKeyboardController>(custom),
                                                [&] { provided = ui::CurrentSoftwareKeyboardController(); });
        });
    });
    h.Pump();
    CHECK(fromOwner != nullptr);
    if (!fromOwner) return;
    fromOwner->Hide();
    CHECK_EQ(h.hides, 1);
    CHECK(provided == custom);
    if (!provided) return;
    provided->Hide();
    CHECK_EQ(custom->hides, 1);
    CHECK_EQ(h.hides, 1);
}

// An idle field asks for no frames: drawing it without a magnifier writes no state.
TEST(IdleTextFieldDrawsWithoutAskingForFrames) {
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    auto& d = rt::Dispatcher::Main();
    rt::Composition comp(owner.Root(), d);
    TextFieldState state("hi");
    comp.SetContent([&] { Column(Modifier{}, [&] { BasicTextField(state, {}, {}); }); });
    comp.Recompose();
    owner.MeasureAndLayout({ 200, 100 });
    owner.Draw({});
    rt::Observation::ApplyChanges();
    // A host asks for a frame on the first state write after a frame (the change listener).
    int writes = 0;
    rt::Observation::SetChangeListener([&] { ++writes; });
    for (int i = 0; i < 3; ++i) {
        owner.Draw({});
        rt::Observation::ApplyChanges();
    }
    rt::Observation::SetChangeListener(nullptr);
    CHECK_EQ(writes, 0);
}

// populateViewStructure_hintText: a node with only HintText is in the structure, with its hint.
TEST(AutofillStructureCarriesHintText) {
    FieldHarness h;
    h.owner.SetAutofillHost(ui::AutofillHost{});
    h.comp.SetContent([&] {
        Column(Modifier{}, [&] {
            Box(SizeMod(Semantics(Modifier{}, [](ui::SemanticsConfiguration& c) { c.hintText = "Hint"; }), Dp(50), Dp(20)));
        });
    });
    h.Pump({ 300, 300 });
    const auto structure = h.owner.Autofill()->Structure();
    bool found = false;
    for (const auto& n : structure) if (n.hint == std::optional<std::string>("Hint")) found = true;
    CHECK(found);
}

TEST(ToolbarProviderReplacesThePlatformToolbar) {
    FieldHarness h;
    h.owner.Config().longPressTimeoutMillis = 40;
    TextFieldState state("hello big world");
    struct Provider final : TextContextMenuProvider {
        int shown = 0;
        std::size_t items = 0;
        rt::Task<> ShowTextContextMenu(std::shared_ptr<TextContextMenuDataProvider> data) override {
            ++shown;
            items = data->Data().components.size();
            co_await rt::NextFrame();   // stays up until cancelled (or a frame passes)
        }
    } provider;
    h.comp.SetContent([&] {
        rt::Composition::Current()->Provide(LocalTextContextMenuToolbarProvider, static_cast<TextContextMenuProvider*>(&provider), [&] {
            Column(Modifier{}, [&] { BasicTextField(state, {}, Style20()); });
        });
    });
    h.Pump({ 300, 300 });
    using ui::PointerEventType;
    using ui::PointerType;
    SendPointer(h, PointerType::Touch, PointerEventType::Press, 75, 10, true);
    WaitMs(h, 80);
    SendPointer(h, PointerType::Touch, PointerEventType::Release, 75, 10, false);
    CHECK_EQ(provider.shown, 1);
    CHECK(provider.items >= 3);   // cut, copy, select all
    CHECK(!h.owner.TextToolbarShown());
}

TEST(EditProcessorAppliesImeCommandsLikeCompose) {
    using namespace kiln::foundation;
    EditProcessor p;
    p.Reset(TextFieldValue("hello", TextRange(5)));
    auto v = p.Apply({ std::make_shared<SetComposingTextCommand>("wor", 1) });
    CHECK(v.Text() == "hellowor" && v.Composition() == std::optional<TextRange>(TextRange(5, 8)) && v.Selection() == TextRange(8));
    v = p.Apply({ std::make_shared<CommitTextCommand>("world", 1) });
    CHECK(v.Text() == "helloworld" && !v.Composition() && v.Selection() == TextRange(10));
    v = p.Apply({ std::make_shared<BackspaceCommand>(), std::make_shared<MoveCursorCommand>(-4) });
    CHECK(v.Text() == "helloworl" && v.Selection() == TextRange(5));
    v = p.Apply({ std::make_shared<DeleteSurroundingTextCommand>(2, 1) });
    CHECK(v.Text() == "helorl" && v.Selection() == TextRange(3));
    // A surrogate pair counts as one code point.
    p.Reset(TextFieldValue("a\xF0\x9F\x98\x80" "b", TextRange(3)));
    v = p.Apply({ std::make_shared<DeleteSurroundingTextInCodePointsCommand>(1, 0) });
    CHECK(v.Text() == "ab" && v.Selection() == TextRange(1));
    v = p.Apply({ std::make_shared<SetSelectionCommand>(2, 0), std::make_shared<DeleteAllCommand>() });
    CHECK(v.Text().empty());
    const TextFieldValue tf("hello world", TextRange(6, 11));
    CHECK(GetSelectedText(tf).Text() == "world" && GetTextBeforeSelection(tf, 3).Text() == "lo " && GetTextAfterSelection(tf, 3).Text().empty());
}

TEST(KeyboardOptionsCarryPlatformImeOptions) {
    // fillUnspecifiedValuesWith keeps this side's platformImeOptions; ToImeOptions passes it on.
    foundation::KeyboardOptions a, b;
    b.platformImeOptions = ui::PlatformImeOptions{ "nm" };
    CHECK(a.FillUnspecifiedValuesWith(b).platformImeOptions == b.platformImeOptions);
    a.platformImeOptions = ui::PlatformImeOptions{};
    CHECK(!a.FillUnspecifiedValuesWith(b).platformImeOptions->privateImeOptions);
    const ui::ImeOptions o = b.ToImeOptions(true);
    CHECK(o.platformImeOptions && o.platformImeOptions->privateImeOptions == "nm" && o.singleLine);
}

TEST(HandwritingDetectorDelegatesToTheHandlerThatTakesFocus) {
    ui::SetStylusHandwritingSupported(true);
    FieldHarness h;
    int prepared = 0, accepted = 0, detected = 0;
    ui::TextInputHost host;
    host.prepareStylusHandwritingDelegation = [&] { ++prepared; };
    host.acceptStylusHandwritingDelegation = [&] { ++accepted; };
    h.owner.SetTextInputHost(host);
    rt::State<bool> showEditor{ false };
    FocusRequester editorFocus;
    bool delegatorFocused = false;
    h.comp.SetContent([&] {
        Column(Modifier{}, [&] {
            // A delegator: a focusable box that, on a stroke, opens the real editor.
            Box(Focusable(OnFocusChanged(SizeMod(HandwritingDetector(Modifier{}, [&] { ++detected; showEditor.Set(true); }), Dp(100), Dp(40)),
                                         [&](ui::FocusState s) { delegatorFocused = ui::IsFocused(s); })));
            if (showEditor.Get())
                Box(Focusable(FocusRequesterModifier(HandwritingHandler(SizeMod(Modifier{}, Dp(100), Dp(40))), editorFocus)));
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
    // A finger does nothing; a stylus past the slop calls back, focuses the content below and
    // prepares the delegation.
    send(ui::PointerType::Touch, ui::PointerEventType::Press, 20, 20, true);
    send(ui::PointerType::Touch, ui::PointerEventType::Move, 60, 20, true);
    send(ui::PointerType::Touch, ui::PointerEventType::Release, 60, 20, false);
    CHECK(detected == 0 && prepared == 0);
    send(ui::PointerType::Stylus, ui::PointerEventType::Press, 20, 20, true);
    send(ui::PointerType::Stylus, ui::PointerEventType::Move, 40, 20, true);
    CHECK(detected == 1 && prepared == 1 && delegatorFocused);
    send(ui::PointerType::Stylus, ui::PointerEventType::Release, 40, 20, false);
    // The editor takes focus: it accepts the delegation once the focus change has run.
    editorFocus.RequestFocus();
    CHECK_EQ(accepted, 0);
    rt::Dispatcher::Main().RunPending();
    CHECK_EQ(accepted, 1);
    // Without support both modifiers are nothing.
    ui::SetStylusHandwritingSupported(false);
    CHECK(HandwritingHandler(Modifier{}).Empty() && HandwritingDetector(Modifier{}, [] {}).Empty());
}
