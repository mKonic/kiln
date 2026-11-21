// Paging separators (paging-common SeparatorsTest, HeaderFooterTest) and flatMap
// (PageEventTest's flatMap cases): separator pages, their offsets and hint indices, terminal
// separators deferred over empty pages, drops, static lists.
#include "Check.h"

#include "kiln/paging/Separators.h"

using namespace kiln;
using namespace kiln::paging;
using namespace kiln::paging::detail;

namespace {
    using Page = TransformablePage<std::string>;
    using Event = PageEvent<std::string>;
    using Pages = std::vector<std::vector<std::string>>;

    // LETTER_SEPARATOR_GENERATOR: an upper-case letter where the first letter changes, END at the end.
    std::optional<std::string> Letters(const std::string* before, const std::string* after) {
        if (!after) return std::string("END");
        if (!before || before->front() != after->front()) return std::string(1, char(std::toupper(static_cast<unsigned char>(after->front()))));
        return std::nullopt;
    }

    std::vector<Page> ToPages(const Pages& pages, int indexOfInitialPage = 0) {
        std::vector<Page> out;
        for (std::size_t i = 0; i < pages.size(); ++i) out.push_back(Page::Of(int(i) - indexOfInitialPage, pages[i]));
        return out;
    }
    Page MakePage(std::vector<int> offsets, std::vector<std::string> data, int hintOffset, std::optional<std::vector<int>> hintIndices) {
        return { std::move(offsets), std::move(data), hintOffset, std::move(hintIndices) };
    }
    bool SamePage(const Page& a, const Page& b) {
        // A page without hint indices equals one with the identity indices (as upstream's equals).
        auto indices = [](const Page& p) {
            if (p.hintOriginalIndices) return *p.hintOriginalIndices;
            std::vector<int> v(p.data.size());
            for (std::size_t i = 0; i < v.size(); ++i) v[i] = int(i);
            return v;
        };
        return a.originalPageOffsets == b.originalPageOffsets && a.data == b.data && a.hintOriginalPageOffset == b.hintOriginalPageOffset && indices(a) == indices(b);
    }
    bool SamePages(const std::vector<Page>& a, const std::vector<Page>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!SamePage(a[i], b[i])) return false;
        return true;
    }

    LoadStates States(bool prependDone = false, bool appendDone = false) {
        return { LoadState::NotLoading(false), LoadState::NotLoading(prependDone), LoadState::NotLoading(appendDone) };
    }
    // refresh / prepend / append(pages = items): one page per item.
    std::vector<Page> OnePerItem(const std::vector<std::string>& items) {
        Pages pages;
        for (const auto& i : items) pages.push_back({ i });
        return ToPages(pages);
    }
    Event Refresh(const std::vector<std::string>& items, bool prependDone = false, bool appendDone = false) {
        auto pages = OnePerItem(items);
        if (pages.empty()) pages.push_back(Page::Of(0, {}));
        return Event::Refresh(std::move(pages), 0, 0, States(prependDone, appendDone));
    }
    Event Append(const std::vector<std::string>& items, bool appendDone = false) { return Event::Append(OnePerItem(items), 0, States(false, appendDone)); }
    Event Prepend(const std::vector<std::string>& items, bool prependDone = false) { return Event::Prepend(OnePerItem(items), 0, States(prependDone, false)); }

    // The data of each page of each insert (assertInsertData).
    std::vector<std::vector<std::string>> Items(const std::vector<Event>& events) {
        std::vector<std::vector<std::string>> out;
        for (const auto& e : events) {
            if (e.kind != Event::Kind::Insert) continue;
            std::vector<std::string> flat;
            for (const auto& p : e.pages) flat.insert(flat.end(), p.data.begin(), p.data.end());
            out.push_back(flat);
        }
        return out;
    }
    std::vector<Event> Run(const std::vector<Event>& in, TerminalSeparatorType type = TerminalSeparatorType::FullyComplete) {
        SeparatorState<std::string, std::string> state(type, Letters);
        std::vector<Event> out;
        for (const auto& e : in) out.push_back(state.OnEvent(e));
        return out;
    }
    using Items_ = std::vector<std::vector<std::string>>;
}

TEST(Separators_refreshFull) {
    auto out = Run({ Event::Refresh(ToPages({ { "a2", "b1" }, { "c1", "c2" } }), 0, 1, States()) });
    CHECK(SamePages(out[0].pages, { MakePage({ 0 }, { "a2", "B", "b1" }, 0, std::vector<int>{ 0, 1, 1 }),
                                    MakePage({ 0, 1 }, { "C" }, 1, std::vector<int>{ 0 }),
                                    Page::Of(1, { "c1", "c2" }) }));
    CHECK(out[0].placeholdersAfter == 1);
}

TEST(Separators_refreshStartFull) {
    const Event refresh = Event::Refresh(ToPages({ { "c1" } }), 0, 1, States());
    auto out = Run({ refresh, Event::Prepend(ToPages({ { "a1", "b1" }, { "b2", "b3" } }, 2), 1, States()) });
    CHECK(SamePages(out[0].pages, refresh.pages));
    CHECK(SamePages(out[1].pages, { MakePage({ -2 }, { "a1", "B", "b1" }, -2, std::vector<int>{ 0, 1, 1 }),
                                    Page::Of(-1, { "b2", "b3" }),
                                    MakePage({ -1, 0 }, { "C" }, -1, std::vector<int>{ 1 }) }));
}

TEST(Separators_refreshEndFull) {
    const Event refresh = Event::Refresh(ToPages({ { "a1", "a2" } }), 0, 1, States());
    auto out = Run({ refresh, Event::Append(ToPages({ { "c1", "d1" }, { "d2", "d3" } }, -1), 1, States()) });
    CHECK(SamePages(out[1].pages, { MakePage({ 0, 1 }, { "C" }, 1, std::vector<int>{ 0 }),
                                    MakePage({ 1 }, { "c1", "D", "d1" }, 1, std::vector<int>{ 0, 1, 1 }),
                                    Page::Of(2, { "d2", "d3" }) }));
}

TEST(Separators_refreshTerminalStates) {
    CHECK(Items(Run({ Refresh({ "a1" }) })) == Items_({ { "a1" } }));
    CHECK(Items(Run({ Refresh({ "a1" }, true) })) == Items_({ { "A", "a1" } }));
    CHECK(Items(Run({ Refresh({ "a1" }, false, true) })) == Items_({ { "a1", "END" } }));
    CHECK(Items(Run({ Refresh({ "a1" }, true, true) })) == Items_({ { "A", "a1", "END" } }));
    CHECK(Items(Run({ Refresh({}) })) == Items_({ {} }));
    // Empty and complete at both ends: the generator's one separator for nothing either side.
    CHECK(Items(Run({ Refresh({}, true, true) })) == Items_({ { "END" } }));
}

TEST(Separators_refreshEmptyStartDone) {
    auto out = Run({ Refresh({}, true), Append({ "a1" }), Append({}), Append({}, true) });
    CHECK(Items(out) == Items_({ {}, { "A", "a1" }, {}, { "END" } }));
}

TEST(Separators_refreshEmptyEndDone) {
    auto out = Run({ Refresh({}, false, true), Prepend({ "a1" }), Prepend({}), Prepend({}, true) });
    CHECK(Items(out) == Items_({ {}, { "a1", "END" }, {}, { "A" } }));
}

// emptyDropResetsDeferredFrontSeparator: a drop forgets the deferred header.
TEST(Separators_dropResetsDeferredSeparators) {
    auto out = Run({ Refresh({}, true), Event::Drop(LoadType::Prepend, -1, -1, 0), Append({ "a1" }) });
    CHECK(Items(out) == Items_({ {}, { "a1" } }));
    out = Run({ Refresh({}, false, true), Event::Drop(LoadType::Append, 1, 1, 0), Prepend({ "a1" }) });
    CHECK(Items(out) == Items_({ {}, { "a1" } }));
}

// refreshDropFull: a dropped page's stash goes, and the header can come again.
TEST(Separators_dropThenPrependAddsHeaderAgain) {
    auto out = Run({ Refresh({ "a1" }, true), Event::Drop(LoadType::Prepend, 0, 0, 0), Event::Prepend(ToPages({ { "b1" } }, 1), 0, States(true)) });
    CHECK(Items(out) == Items_({ { "A", "a1" }, { "B", "b1" } }));
}

// refreshSparsePages: empty pages between are kept, separators join the non-empty ones.
TEST(Separators_sparsePages) {
    auto out = Run({ Event::Refresh(ToPages({ {}, { "a2", "b1" }, {}, { "c1", "c2" }, {} }, 0), 0, 0, States()) });
    CHECK(SamePages(out[0].pages, { Page::Of(0, {}),
                                    MakePage({ 1 }, { "a2", "B", "b1" }, 1, std::vector<int>{ 0, 1, 1 }),
                                    Page::Of(2, {}),
                                    MakePage({ 1, 3 }, { "C" }, 3, std::vector<int>{ 0 }),
                                    Page::Of(3, { "c1", "c2" }),
                                    Page::Of(4, {}) }));
}

// The mediator's end of pagination arrives as a load state update: it becomes an empty insert
// carrying the footer (remoteAppendEndOfPaginationReached_fullyComplete).
TEST(Separators_mediatorEndAddsFooter) {
    Event refresh = Event::Refresh(ToPages({ { "a1" } }), 0, 0, States(true, true), States());
    // FullyComplete: no header while the mediator can still prepend.
    auto out = Run({ refresh, Event::LoadStateUpdate(States(true, true), States(false, true)) });
    CHECK(Items(out) == Items_({ { "a1" }, { "END" } }));
    CHECK(out[1].kind == Event::Kind::Insert && out[1].loadType == LoadType::Append);
    // SourceComplete: the footer comes with the source's end.
    out = Run({ refresh }, TerminalSeparatorType::SourceComplete);
    CHECK(Items(out) == Items_({ { "A", "a1", "END" } }));
}

// types: separators of another type than the items.
TEST(Separators_types) {
    struct Item {
        std::string s;
        bool separator = false;
        Item(std::string v, bool sep = false) : s(std::move(v)), separator(sep) {}
    };
    SeparatorState<std::string, Item> state(TerminalSeparatorType::FullyComplete, [](const std::string* a, const std::string* b) -> std::optional<Item> {
        if (a && b) return Item(*a + "|" + *b, true);
        return std::nullopt;
    });
    auto out = state.OnEvent(Event::Refresh(ToPages({ { "a", "b" } }), 0, 0, States()));
    CHECK(out.pages.size() == 1 && out.pages[0].data.size() == 3 && out.pages[0].data[1].separator && out.pages[0].data[1].s == "a|b" && !out.pages[0].data[2].separator);
}

// HeaderFooterTest: static lists get both, paged data each once its end is reached.
TEST(Separators_headerFooter) {
    auto header = [](const std::string* before, const std::string*) { return before ? std::nullopt : std::optional<std::string>("HEADER"); };
    auto footer = [](const std::string*, const std::string* after) { return after ? std::nullopt : std::optional<std::string>("FOOTER"); };
    SeparatorState<std::string, std::string> h(TerminalSeparatorType::FullyComplete, header), f(TerminalSeparatorType::FullyComplete, footer);
    CHECK(h.OnEvent(Event::StaticList({ "a", "b" })).data == std::vector<std::string>({ "HEADER", "a", "b" }));
    CHECK(f.OnEvent(Event::StaticList({ "a", "b" })).data == std::vector<std::string>({ "a", "b", "FOOTER" }));
    SeparatorState<std::string, std::string> h2(TerminalSeparatorType::FullyComplete, header);
    CHECK(Items({ h2.OnEvent(Refresh({ "a" })), h2.OnEvent(Prepend({ "b" }, true)) }) == Items_({ { "a" }, { "HEADER", "b" } }));
    // On PagingData: the functions.
    auto data = InsertFooterItem(InsertHeaderItem(PagingData<std::string>::From({ "x" }), std::string("H")), std::string("F"));
    CHECK(!data.CachedEvent());   // as upstream's transform: separators need the events before
}

// flatMap: each item to several, the hint index of each the source item's.
TEST(Paging_flatMap) {
    Event e = Event::Refresh(ToPages({ { "a", "b" } }), 0, 0, States());
    auto out = e.FlatMap([](const std::string& s) { return std::vector<std::string>{ s + "1", s + "2" }; });
    CHECK(out.pages[0].data == std::vector<std::string>({ "a1", "a2", "b1", "b2" }));
    CHECK(out.pages[0].hintOriginalIndices == std::vector<int>({ 0, 0, 1, 1 }));
    auto none = e.FlatMap([](const std::string& s) { return s == "a" ? std::vector<std::string>{} : std::vector<std::string>{ s }; });
    CHECK(none.pages[0].data == std::vector<std::string>({ "b" }) && none.pages[0].hintOriginalIndices == std::vector<int>({ 1 }));
    auto data = PagingData<std::string>::From({ "p", "q" }).FlatMap([](const std::string& s) { return std::vector<int>{ int(s[0]), 0 }; });
    auto cached = data.CachedEvent();
    CHECK(cached && cached->pages[0].data == std::vector<int>({ 'p', 0, 'q', 0 }));
}
