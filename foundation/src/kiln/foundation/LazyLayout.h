#pragma once

// The public lazy layout building blocks (foundation lazy/layout): LazyLayout composes items on
// demand while it measures (an item provider says what item i is and its key), keeps items it
// was asked to precompose (LazyLayoutPrefetchState) and items content pinned
// (LazyLayoutPinnableItem), and drops the rest. IntervalList and LazyLayoutIntervalContent are
// how DSLs such as LazyColumn's describe their items. kiln's own lazy lists use an internal core;
// these are for custom lazy layouts.
//
// Items keep their state by key; composed items are not reused across content types (kiln's
// subcomposition has no slot reuse).

#include "kiln/foundation/Subcompose.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace kiln::foundation {

    // getDefaultLazyLayoutKey: the key of an item that has none (never equal to a user key of
    // the same index).
    rt::Key DefaultLazyLayoutKey(int index);

    // IntervalList / MutableIntervalList: consecutive runs of items, each with a value.
    template<typename T>
    class MutableIntervalList {
    public:
        struct Interval {
            int startIndex = 0, size = 0;
            T value;
        };
        int Size() const { return m_Size; }
        void AddInterval(int size, T value) {
            if (size < 0) throw std::invalid_argument("size should be >=0");
            if (size == 0) return;
            m_Intervals.push_back({ m_Size, size, std::move(value) });
            m_Size += size;
        }
        const Interval& Get(int index) const {
            CheckIndex(index);
            if (m_Last && index >= m_Last->startIndex && index < m_Last->startIndex + m_Last->size) return *m_Last;
            m_Last = &m_Intervals[std::size_t(Search(index))];
            return *m_Last;
        }
        // forEach(fromIndex, toIndex): the intervals covering [from, to], in order.
        template<typename F> void ForEach(int fromIndex, int toIndex, F&& block) const {
            CheckIndex(fromIndex);
            CheckIndex(toIndex);
            if (toIndex < fromIndex) throw std::invalid_argument("toIndex should be not smaller than fromIndex");
            int interval = Search(fromIndex);
            int item = m_Intervals[std::size_t(interval)].startIndex;
            while (item <= toIndex) {
                const Interval& i = m_Intervals[std::size_t(interval++)];
                block(i);
                item += i.size;
            }
        }
        template<typename F> void ForEach(F&& block) const { if (m_Size > 0) ForEach(0, m_Size - 1, std::forward<F>(block)); }

    private:
        void CheckIndex(int index) const {
            if (index < 0 || index >= m_Size) throw std::out_of_range("Index " + std::to_string(index) + ", size " + std::to_string(m_Size));
        }
        int Search(int index) const {
            auto it = std::upper_bound(m_Intervals.begin(), m_Intervals.end(), index, [](int i, const Interval& v) { return i < v.startIndex; });
            return int(it - m_Intervals.begin()) - 1;
        }
        std::vector<Interval> m_Intervals;
        int m_Size = 0;
        mutable const Interval* m_Last = nullptr;
    };
    template<typename T> using IntervalList = MutableIntervalList<T>;

    // LazyLayoutIntervalContent.Interval: an interval's keys and content types by local index
    // (content type 0: none).
    struct LazyLayoutInterval {
        std::function<rt::Key(int)> key;
        std::function<std::size_t(int)> type;
    };

    // LazyLayoutIntervalContent: items described by intervals.
    template<typename Interval = LazyLayoutInterval>
    class LazyLayoutIntervalContent {
    public:
        virtual ~LazyLayoutIntervalContent() = default;
        virtual const IntervalList<Interval>& Intervals() const = 0;
        int ItemCount() const { return Intervals().Size(); }
        rt::Key GetKey(int index) const {
            return WithInterval(index, [index](int local, const Interval& c) { return c.key ? c.key(local) : DefaultLazyLayoutKey(index); });
        }
        std::size_t GetContentType(int index) const {
            return WithInterval(index, [](int local, const Interval& c) { return c.type ? c.type(local) : std::size_t(0); });
        }
        template<typename F> auto WithInterval(int globalIndex, F&& block) const {
            const auto& interval = Intervals().Get(globalIndex);
            return block(globalIndex - interval.startIndex, interval.value);
        }
    };

    // LazyLayoutKeyIndexMap: key <-> index for a range of items (the ones near the viewport).
    class LazyLayoutKeyIndexMap {
    public:
        virtual ~LazyLayoutKeyIndexMap() = default;
        virtual int GetIndex(const rt::Key& key) const = 0;   // -1: not in the range
        virtual std::optional<rt::Key> GetKey(int index) const = 0;
    };
    // LazyLayoutKeyIndexMap(itemIndexRange, intervalContent): [first, last] of `content`.
    template<typename Interval>
    std::shared_ptr<LazyLayoutKeyIndexMap> MakeLazyLayoutKeyIndexMap(int first, int last, const LazyLayoutIntervalContent<Interval>& content) {
        struct Map final : LazyLayoutKeyIndexMap {
            std::map<std::uint64_t, int> indices;
            std::vector<rt::Key> keys;
            int start = 0;
            int GetIndex(const rt::Key& key) const override { auto it = indices.find(key.value); return it == indices.end() ? -1 : it->second; }
            std::optional<rt::Key> GetKey(int index) const override {
                const int i = index - start;
                if (i < 0 || i >= int(keys.size())) return std::nullopt;
                return keys[std::size_t(i)];
            }
        };
        auto m = std::make_shared<Map>();
        if (first < 0) throw std::invalid_argument("negative nearestRange.first");
        last = std::min(last, content.ItemCount() - 1);
        m->start = first;
        for (int i = first; i <= last; ++i) {
            const rt::Key k = content.GetKey(i);
            m->indices[k.value] = i;
            m->keys.push_back(k);
        }
        return m;
    }

    // LazyLayoutItemProvider: the items a LazyLayout shows.
    class LazyLayoutItemProvider {
    public:
        virtual ~LazyLayoutItemProvider() = default;
        virtual int ItemCount() const = 0;
        virtual void Item(int index, const rt::Key& key) = 0;   // composes item `index`
        virtual std::size_t GetContentType(int) const { return 0; }
        virtual rt::Key GetKey(int index) const { return DefaultLazyLayoutKey(index); }
        virtual int GetIndex(const rt::Key&) const { return -1; }
    };
    using LazyLayoutItemProviderPtr = std::shared_ptr<LazyLayoutItemProvider>;

    // LazyLayoutMeasureScope: the measure scope of a LazyLayout's policy. compose(index) composes
    // item `index` (once per measure) and returns its root layouts.
    class LazyLayoutMeasureScope : public MeasureScope {
    public:
        explicit LazyLayoutMeasureScope(const MeasureScope& s) : MeasureScope(s) {}
        virtual const std::vector<Measurable*>& Compose(int index) = 0;
        // measure(index, constraints): compose(index), each measured (cached per measure).
        virtual const std::vector<ui::Placeable*>& Measure(int index, const Constraints& constraints) = 0;
    };
    using LazyLayoutMeasurePolicy = std::function<MeasureResult(LazyLayoutMeasureScope&, const Constraints&)>;

    // LazyLayoutPrefetchState: items composed (and measured) ahead of being needed. A handle
    // cancels its request; precomposed items are kept until a measure composes them or the
    // request is cancelled.
    class LazyLayoutPrefetchState {
    public:
        struct PrefetchResultScope {
            int index = 0;
            std::vector<ui::IntSize> sizes;
            int PlaceablesCount() const { return int(sizes.size()); }
            ui::IntSize GetSize(int placeableIndex) const { return sizes.at(std::size_t(placeableIndex)); }
        };
        class PrefetchHandle {
        public:
            PrefetchHandle() = default;
            void Cancel() { if (m_Handle) m_Handle->Dispose(); m_Handle.reset(); }
            void MarkAsUrgent() {}   // prefetching is immediate here
        private:
            friend class LazyLayoutPrefetchState;
            std::shared_ptr<SubcomposeLayoutState::PrecomposedSlotHandle> m_Handle;
        };
        // NestedPrefetchScope: what a nested lazy layout asks to have prefetched with its parent's
        // item.
        struct NestedPrefetchScope {
            int nestedPrefetchItemCount = -1;
            std::vector<std::pair<int, std::optional<Constraints>>> requests;
            void SchedulePrecomposition(int index) { requests.push_back({ index, std::nullopt }); }
            void SchedulePrecompositionAndPremeasure(int index, Constraints c) { requests.push_back({ index, c }); }
        };

        LazyLayoutPrefetchState() = default;
        explicit LazyLayoutPrefetchState(std::function<void(NestedPrefetchScope&)> onNestedPrefetch) : m_OnNestedPrefetch(std::move(onNestedPrefetch)) {}

        PrefetchHandle SchedulePrecomposition(int index);
        PrefetchHandle SchedulePrecompositionAndPremeasure(int index, Constraints constraints,
                                                           std::function<void(const PrefetchResultScope&)> onItemPremeasured = {});
        // collectNestedPrefetchRequests: what onNestedPrefetch asks for.
        std::vector<std::pair<int, std::optional<Constraints>>> CollectNestedPrefetchRequests() const;

        // internal: the LazyLayout it serves.
        struct Target {
            SubcomposeLayoutState* subcompose = nullptr;
            std::function<LazyLayoutItemProviderPtr()> provider;
            std::function<std::shared_ptr<const std::function<void()>>(int index, const rt::Key& key)> content;
        };
        void SetTarget(std::optional<Target> t) { m_Target = std::move(t); }

    private:
        std::optional<Target> m_Target;
        std::function<void(NestedPrefetchScope&)> m_OnNestedPrefetch;
    };

    // LazyLayout(itemProvider, modifier, prefetchState, measurePolicy).
    void LazyLayout(std::function<LazyLayoutItemProviderPtr()> itemProvider, const Modifier& modifier, LazyLayoutPrefetchState* prefetchState,
                    LazyLayoutMeasurePolicy measurePolicy, rt::Key key = {});

    // LazyLayoutPinnedItemList: the items of a lazy layout that content pinned (a focused field
    // in a scrolled-away item); the layout keeps them composed and laid out.
    class LazyLayoutPinnedItemList {
    public:
        struct PinnedItem {
            rt::Key key = rt::Key::Positional();
            int index = -1;
        };
        // Observable: a measure reading it remeasures when items are pinned or released.
        const std::vector<std::shared_ptr<PinnedItem>>& Items() const { return m_Items.Get(); }
        int Size() const { return int(Items().size()); }
        // internal
        void Pin(std::shared_ptr<PinnedItem> item);
        void Release(const std::shared_ptr<PinnedItem>& item);

    private:
        rt::State<std::vector<std::shared_ptr<PinnedItem>>, rt::NeverEqual> m_Items{ std::vector<std::shared_ptr<PinnedItem>>{} };
    };

    // LazyLayoutPinnableItem: the pinnable container around an item's content; pinning it puts
    // the item in `pinnedItemList` (and pins the container around the layout).
    void LazyLayoutPinnableItem(const rt::Key& key, int index, LazyLayoutPinnedItemList& pinnedItemList, std::function<void()> content);

    // LazyLayoutCacheWindow: how far beyond the viewport (ahead of and behind the scroll) a lazy
    // layout keeps items composed.
    class LazyLayoutCacheWindow {
    public:
        virtual ~LazyLayoutCacheWindow() = default;
        virtual bool IsNonScrollCachingEnabled() const { return true; }
        virtual int CalculateAheadWindow(const ui::Density&, int /*viewport*/) const { return 0; }
        virtual int CalculateBehindWindow(const ui::Density&, int /*viewport*/) const { return 0; }
    };
    using LazyLayoutCacheWindowPtr = std::shared_ptr<const LazyLayoutCacheWindow>;
    // LazyLayoutCacheWindow(ahead, behind) in dp, or as fractions of the viewport.
    LazyLayoutCacheWindowPtr MakeLazyLayoutCacheWindow(ui::Dp ahead, ui::Dp behind = ui::Dp(0), bool isNonScrollCachingEnabled = true);
    LazyLayoutCacheWindowPtr MakeLazyLayoutCacheWindowFraction(float aheadFraction, float behindFraction = 0, bool isNonScrollCachingEnabled = true);
    // LazyListDefaults / LazyGridDefaults / LazyStaggeredGridDefaults.cacheWindow: a tenth of
    // the viewport ahead.
    struct LazyListDefaults { static LazyLayoutCacheWindowPtr CacheWindow(); };
    struct LazyGridDefaults { static LazyLayoutCacheWindowPtr CacheWindow() { return LazyListDefaults::CacheWindow(); } };
    struct LazyStaggeredGridDefaults { static LazyLayoutCacheWindowPtr CacheWindow() { return LazyListDefaults::CacheWindow(); } };

}
