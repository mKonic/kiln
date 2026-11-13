// The public lazy layout API: a custom lazy column built on LazyLayout composes only what it
// measures; prefetching and pinning keep more composed; interval content and key maps.
#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/LazyLayout.h"
#include "kiln/foundation/Layouts.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 400, 200 });
            }
        }
    };

    // Items 50 px tall; `composed` counts the items composed now.
    struct Provider final : LazyLayoutItemProvider {
        int count = 100;
        std::set<int>* composed = nullptr;
        int ItemCount() const override { return count; }
        rt::Key GetKey(int index) const override { return rt::Key(index); }
        int GetIndex(const rt::Key& key) const override {
            for (int i = 0; i < count; ++i) if (rt::Key(i) == key) return i;
            return -1;
        }
        void Item(int index, const rt::Key&) override {
            auto* c = composed;
            rt::Composition::Current()->DisposableEffect(rt::Keys(index), [c, index] {
                c->insert(index);
                return kiln::Function<void()>([c, index] { c->erase(index); });
            });
            Box(Height(FillMax(Modifier{}, Axis::Horizontal, 1), Dp(50)), Alignment::TopStart(), false, [] {});
        }
    };

    // A lazy column from `first`: composes and places what fits in the height.
    LazyLayoutMeasurePolicy Column(int first, LazyLayoutPinnedItemList* pinned = nullptr) {
        return [first, pinned](LazyLayoutMeasureScope& s, const Constraints& c) {
            std::vector<std::pair<Placeable*, int>> placed;
            int y = 0;
            for (int i = first; i < 100 && y < c.maxHeight; ++i) {
                for (Placeable* p : s.Measure(i, Constraints{ 0, c.maxWidth, 0, Constraints::Infinity })) placed.push_back({ p, y });
                y += 50;
            }
            // Pinned items stay composed (and laid out) wherever they are.
            if (pinned) for (const auto& item : pinned->Items()) s.Measure(item->index, Constraints{ 0, c.maxWidth, 0, Constraints::Infinity });
            return s.Layout(c.maxWidth, c.maxHeight, [placed](PlacementScope& place) { for (auto [p, at] : placed) place.Place(*p, 0, at); });
        };
    }
}

TEST(LazyLayoutComposesWhatItMeasures) {
    std::set<int> composed;   // before the harness: its teardown disposes items into it
    Harness h;
    auto provider = std::make_shared<Provider>();
    provider->composed = &composed;
    rt::State<int> first(0);
    h.Run([&] {
        LazyLayout([provider] { return provider; }, Modifier{}, nullptr, Column(first.Get()));
    });
    CHECK((composed == std::set<int>{ 0, 1, 2, 3 }));
    first.Set(10);
    h.Pump();
    CHECK((composed == std::set<int>{ 10, 11, 12, 13 }));
}

TEST(LazyLayoutPrefetchesAndKeepsPinnedItems) {
    std::set<int> composed;   // before the harness: its teardown disposes items into it
    Harness h;
    auto provider = std::make_shared<Provider>();
    provider->composed = &composed;
    LazyLayoutPrefetchState prefetch;
    LazyLayoutPinnedItemList pinned;
    rt::State<int> first(0);
    std::shared_ptr<PinnableContainer> container2;
    // Item 2's content grabs its pinnable container.
    struct PinProvider final : LazyLayoutItemProvider {
        std::shared_ptr<Provider> inner;
        LazyLayoutPinnedItemList* list = nullptr;
        std::shared_ptr<PinnableContainer>* container2 = nullptr;
        int ItemCount() const override { return inner->ItemCount(); }
        rt::Key GetKey(int i) const override { return inner->GetKey(i); }
        int GetIndex(const rt::Key& k) const override { return inner->GetIndex(k); }
        void Item(int index, const rt::Key& key) override {
            LazyLayoutPinnableItem(key, index, *list, [this, index, key] {
                if (index == 2) *container2 = rt::Composition::Current()->Read(LocalPinnableContainer);
                inner->Item(index, key);
            });
        }
    };
    auto pinProvider = std::make_shared<PinProvider>();
    pinProvider->inner = provider;
    pinProvider->list = &pinned;
    pinProvider->container2 = &container2;
    h.Run([&] {
        LazyLayout([pinProvider] { return pinProvider; }, Modifier{}, &prefetch, Column(first.Get(), &pinned));
    });
    CHECK((composed == std::set<int>{ 0, 1, 2, 3 }));
    // Prefetching item 4 composes it before a measure needs it.
    auto handle = prefetch.SchedulePrecomposition(4);
    h.Pump();
    CHECK(composed.contains(4));
    handle.Cancel();
    h.Pump();
    CHECK(!composed.contains(4));
    // Pinning item 2 keeps it while it scrolls away.
    CHECK(container2 != nullptr);
    container2->Pin();
    CHECK_EQ(pinned.Size(), 1);
    first.Set(20);
    h.Pump();
    CHECK(composed.contains(2));
    CHECK(composed.contains(20));
    container2->Release();
    h.Pump();
    CHECK(!composed.contains(2));
    CHECK_EQ(pinned.Size(), 0);
}

TEST(IntervalContentKeysAndMaps) {
    struct Content final : LazyLayoutIntervalContent<> {
        MutableIntervalList<LazyLayoutInterval> list;
        const IntervalList<LazyLayoutInterval>& Intervals() const override { return list; }
    } content;
    content.list.AddInterval(3, { [](int i) { return rt::Key(100 + i); }, [](int) { return std::size_t(7); } });
    content.list.AddInterval(0, {});   // empty intervals are skipped
    content.list.AddInterval(2, {});
    CHECK_EQ(content.ItemCount(), 5);
    CHECK(content.GetKey(1) == rt::Key(101));
    CHECK(content.GetKey(4) == DefaultLazyLayoutKey(4));
    CHECK(!(DefaultLazyLayoutKey(4) == rt::Key(4)));
    CHECK_EQ(content.GetContentType(0), std::size_t(7));
    CHECK_EQ(content.GetContentType(3), std::size_t(0));
    int visited = 0;
    content.list.ForEach(2, 3, [&](const auto&) { ++visited; });
    CHECK_EQ(visited, 2);
    auto map = MakeLazyLayoutKeyIndexMap(1, 10, content);
    CHECK_EQ(map->GetIndex(rt::Key(102)), 2);
    CHECK_EQ(map->GetIndex(rt::Key(100)), -1);   // outside the range
    CHECK(map->GetKey(4).has_value());
    CHECK_EQ(LazyListDefaults::CacheWindow()->CalculateAheadWindow(Density{ 1, 1 }, 1000), 100);
}
