#include "Check.h"

#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/foundation/Focus.h"

using namespace kiln;
using namespace kiln::foundation;

namespace { std::u16string U(const char* s) { return ToUtf16(s); } }

TEST(ImeCommandsFollowInputConnectionSemantics) {
    TextFieldState st("hello");
    st.SetSelection(5, 5);
    st.SetComposingText(U("wor"), 1);
    CHECK(st.Peek().text == U("hellowor"));
    CHECK(st.Peek().composition == (TextRange{ 5, 8 }));
    CHECK(st.Peek().selection == (TextRange{ 8, 8 }));
    st.SetComposingText(U("world"), 1);               // replaces the composition
    CHECK(st.Peek().text == U("helloworld"));
    st.CommitText(U(" world"), 1);                    // commit replaces the composition too
    CHECK(st.Peek().text == U("hello world"));
    CHECK(!st.Peek().composition);
    CHECK_EQ(st.Peek().selection.start, 11);
    st.CommitText(U("!"), 0);                         // <= 0: relative to the start of the insert
    CHECK(st.Peek().text == U("hello world!"));
    CHECK_EQ(st.Peek().selection.start, 11);
    st.SetSelection(5, 5);
    st.DeleteSurroundingText(5, 1);                   // " " after, "hello" before
    CHECK(st.Peek().text == U("world!"));
    CHECK_EQ(st.Peek().selection.start, 0);
    st.SetComposingRegion(0, 5);
    CHECK(st.Peek().composition == (TextRange{ 0, 5 }));
    st.FinishComposingText();
    CHECK(!st.Peek().composition);
}

TEST(SurrogatePairsStayWhole) {
    TextFieldState st("a\xF0\x9F\x94\xA5" "b");          // a, fire emoji (2 UTF-16 units), b
    CHECK_EQ(int(st.Peek().text.size()), 4);
    st.SetSelection(3, 3);
    st.Backspace();
    CHECK(st.Peek().text == U("ab"));
    st.SetText("a\xF0\x9F\x94\xA5" "b");
    st.SetSelection(1, 1);
    st.DeleteSurroundingText(0, 1);                   // would split the pair: deletes both units
    CHECK(st.Peek().text == U("ab"));
    st.SetText("x\xF0\x9F\x94\xA5");
    st.SetSelection(3, 3);
    st.DeleteSurroundingTextInCodePoints(1, 0);
    CHECK(st.Peek().text == U("x"));
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
    TextFieldOptions opt;
    opt.ime.keyboardType = ui::KeyboardType::Email;
    opt.ime.singleLine = true;
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
