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
