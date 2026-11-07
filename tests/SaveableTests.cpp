#include "Check.h"

#include "kiln/runtime/Saveable.h"

using namespace kiln;
using namespace kiln::rt;

namespace {
    struct Node final : TreeNode {
        void SetChildren(std::vector<TreeNode*>) override {}
    };
    // One "activity": a composition over a registry restored from `saved`.
    struct Root {
        Node root;
        SaveableStateRegistry registry;
        Composition composition{ root };
        explicit Root(const std::vector<std::uint8_t>& saved)
            : registry(SaveableStateRegistry::Deserialize(saved)) {}
        void Run(std::function<void()> content) {
            composition.SetContent([this, content] { Composition::Current()->Provide(LocalSaveableStateRegistry, &registry, content); });
            composition.Recompose();
        }
        std::vector<std::uint8_t> Save() const { return SaveableStateRegistry::Serialize(registry.PerformSave()); }
    };
}

TEST(SavedValuesRoundTrip) {
    SaveableStateRegistry::Values v;
    v["a"] = { SavedValue(true), SavedValue(), SavedValue(-5), SavedValue(2.5), SavedValue("héllo"),
               SavedValue(SavedValue::List{ SavedValue(1), SavedValue("x") }) };
    const auto back = SaveableStateRegistry::Deserialize(SaveableStateRegistry::Serialize(v));
    CHECK(back == v);
    auto bytes = SaveableStateRegistry::Serialize(v);
    bytes.resize(bytes.size() - 3);   // truncated data restores nothing rather than garbage
    CHECK(SaveableStateRegistry::Deserialize(bytes).empty());
}

TEST(RememberSaveableSurvivesRecreation) {
    std::vector<std::uint8_t> saved;
    auto content = [](State<int>*& counter, std::string*& name) {
        counter = &RememberSaveableState(0);
        name = &RememberSaveable<std::string>([] { return std::string("init"); });
    };
    {
        Root a(saved);
        State<int>* counter = nullptr; std::string* name = nullptr;
        a.Run([&] { content(counter, name); });
        counter->Set(7);
        *name = "changed";
        saved = a.Save();
    }
    Root b(saved);
    State<int>* counter = nullptr; std::string* name = nullptr;
    b.Run([&] { content(counter, name); });
    CHECK_EQ(counter->Peek(), 7);
    CHECK_EQ(*name, std::string("changed"));
}

TEST(SaveableInALoopRestoresInOrder) {
    std::vector<std::uint8_t> saved;
    std::vector<State<int>*> states;
    auto content = [&] {
        states.clear();
        for (int i = 0; i < 3; ++i) states.push_back(&RememberSaveableState(0));
    };
    {
        Root a(saved);
        a.Run(content);
        for (int i = 0; i < 3; ++i) states[i]->Set(10 + i);
        saved = a.Save();
    }
    Root b(saved);
    b.Run(content);
    CHECK_EQ(states[0]->Peek(), 10);
    CHECK_EQ(states[2]->Peek(), 12);
}

TEST(UncomposedSavedValuesAreKept) {
    std::vector<std::uint8_t> saved;
    State<int>* state = nullptr;
    rt::State<bool> show(true);
    auto content = [&] { if (show.Peek()) state = &RememberSaveableState(5); };
    {
        Root a(saved);
        a.Run(content);
        state->Set(6);
        saved = a.Save();
    }
    show.Set(false);
    {
        Root b(saved);
        b.Run(content);                     // the saveable is not composed this time
        saved = b.Save();
    }
    show.Set(true);
    Root c(saved);
    c.Run(content);
    CHECK_EQ(state->Peek(), 6);
}

TEST(SaveablesOutlivingTheirRegistryDoNotUnregister) {
    // A lazy item's registry can go before the item's composition: the composition's
    // disposers must not reach into it.
    Node root;
    auto registry = std::make_unique<SaveableStateRegistry>();
    auto composition = std::make_unique<Composition>(root);
    SaveableStateRegistry* r = registry.get();
    composition->SetContent([r] {
        Composition::Current()->Provide(LocalSaveableStateRegistry, r, [] {
            RememberSaveable<int>([] { return 1; });
            RememberSaveableState(2);
        });
    });
    composition->Recompose();
    CHECK(r->PerformSave().size() == 2);
    registry.reset();
    composition.reset();
    CHECK(true);
}

#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Pager.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/text/Savers.h"

TEST(AnnotatedStringSaverKeepsStylesAndAnnotations) {
    using namespace kiln::text;
    AnnotatedStringBuilder b;
    SpanStyle bold;
    bold.fontWeight = FontWeight::Bold();
    bold.fontSize = Sp(20);
    bold.color = ui::Color(0xFF112233u);
    bold.shadow = Shadow{ ui::Color(0x80000000u), { 2, 3 }, 4 };
    bold.textDecoration = TextDecoration::Underline;
    b.WithStyle(bold, [&] { b.Append("Bold"); });
    ParagraphStyle para;
    para.textAlign = TextAlign::Center;
    para.lineHeight = Em(1.5f);
    para.textIndent = TextIndent{ Sp(8), Sp(0) };
    b.WithStyle(para, [&] { b.Append(" centred"); });
    TextLinkStyles ls;
    SpanStyle u;
    u.textDecoration = TextDecoration::Underline;
    ls.style = u;
    b.WithLink(LinkAnnotation::Url("https://example.com", ls), [&] { b.Append(" link"); });
    b.WithAnnotation("tag", "value", [&] { b.Append(" tagged"); });
    const AnnotatedString original = b.ToAnnotatedString();
    const auto saver = AnnotatedStringSaver();
    // Through the byte form the host bundle stores.
    SaveableStateRegistry::Values v;
    v["k"] = { saver.save(original) };
    const auto back = SaveableStateRegistry::Deserialize(SaveableStateRegistry::Serialize(v));
    const auto restored = saver.restore(back.at("k")[0]);
    CHECK(restored.has_value());
    if (restored) CHECK(*restored == original);
}

TEST(RememberedStatesSurviveRecreation) {
    using namespace kiln::foundation;
    std::vector<std::uint8_t> saved;
    {
        Root a(saved);
        ScrollState* scroll = nullptr;
        LazyListState* list = nullptr;
        PagerState* pager = nullptr;
        TextFieldState* field = nullptr;
        a.Run([&] {
            scroll = &RememberScrollState();
            list = &RememberLazyListState();
            pager = &RememberPagerState(2, 0.25f, [] { return 10; });
            field = &RememberTextFieldState("hello");
        });
        scroll->SetBounds(500, 100);
        scroll->ScrollTo(120);
        list->ConsumeScrollDelta(0);   // nothing laid out: the position below is what is saved
        field->SetTextAndPlaceCursorAtEnd("hello world");
        field->Edit([](TextFieldBuffer& b) { b.SetSelection({ 0, 5 }); });
        saved = a.Save();
    }
    Root b(saved);
    ScrollState* scroll = nullptr;
    PagerState* pager = nullptr;
    TextFieldState* field = nullptr;
    b.Run([&] {
        scroll = &RememberScrollState();
        RememberLazyListState();
        pager = &RememberPagerState(0, 0, [] { return 10; });
        field = &RememberTextFieldState();
    });
    CHECK_EQ(scroll->Value(), 120);
    CHECK_EQ(pager->CurrentPage(), 2);
    CHECK(std::abs(pager->CurrentPageOffsetFraction() - 0.25f) < 1e-6f);
    CHECK_EQ(pager->PageCount(), 10);
    CHECK_EQ(field->TextUtf8(), std::string("hello world"));
    CHECK(field->Peek().selection.start == 0 && field->Peek().selection.end == 5);
    CHECK(field->CanUndo());   // the history survives too
    field->Undo();
    CHECK_EQ(field->TextUtf8(), std::string("hello"));
}
