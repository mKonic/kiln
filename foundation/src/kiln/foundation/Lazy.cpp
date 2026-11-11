#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Overscroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/StaggeredLaneInfo.h"
#include "kiln/foundation/Draw.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <deque>
#include <map>
#include <tuple>
#include <set>
#include <unordered_map>

namespace kiln::foundation {

    // --- scopes ---------------------------------------------------------------------------------

    void LazyListScope::Item(std::function<void()> content, rt::Key key) {
        auto c = std::make_shared<std::function<void()>>(std::move(content));
        intervals.push_back({ 1, [c](int) { (*c)(); }, key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr });
    }
    void LazyListScope::StickyHeader(std::function<void(int)> content, rt::Key key) {
        // The content gets the global index (filled in by the core).
        intervals.push_back({ 1, std::move(content), key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr, true });
    }
    void LazyListScope::Items(int count, std::function<void(int)> content, std::function<rt::Key(int)> key) {
        intervals.push_back({ count, std::move(content), std::move(key) });
    }

    void LazyGridScope::Item(std::function<void()> content, rt::Key key, std::function<int(const GridItemSpanScope&)> span) {
        auto c = std::make_shared<std::function<void()>>(std::move(content));
        LazyInterval iv{ 1, [c](int) { (*c)(); }, key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr };
        if (span) iv.span = [span](int, int cur, int max) { return span({ cur, max }); };
        intervals.push_back(std::move(iv));
    }
    void LazyGridScope::Items(int count, std::function<void(int)> content, std::function<rt::Key(int)> key, GridSpan span) {
        LazyInterval iv{ count, std::move(content), std::move(key) };
        if (span) iv.span = [span](int i, int cur, int max) { return span({ cur, max }, i); };
        intervals.push_back(std::move(iv));
    }
    void LazyGridScope::StickyHeader(std::function<void(int)> content, rt::Key key) {
        intervals.push_back({ 1, std::move(content), key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr, true });
    }

    void LazyStaggeredGridScope::Item(std::function<void()> content, rt::Key key, bool fullLine) {
        auto c = std::make_shared<std::function<void()>>(std::move(content));
        LazyInterval iv{ 1, [c](int) { (*c)(); }, key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr };
        if (fullLine) iv.span = [](int, int, int max) { return max; };
        intervals.push_back(std::move(iv));
    }
    void LazyStaggeredGridScope::Items(int count, std::function<void(int)> content, std::function<rt::Key(int)> key,
                                       std::function<bool(int)> fullLine) {
        LazyInterval iv{ count, std::move(content), std::move(key) };
        if (fullLine) iv.span = [fullLine](int i, int, int max) { return fullLine(i) ? max : 1; };
        intervals.push_back(std::move(iv));
    }

    // GridCells.calculateCrossAxisCellSizes (LazyGridDsl.kt).
    std::vector<int> GridCells::CellSizes(const Density& d, int available, int spacing) const {
        auto split = [&](int slots) {
            const int without = available - spacing * (slots - 1);
            const int slot = without / slots, remaining = without % slots;
            std::vector<int> out(std::size_t(slots), slot);
            for (int i = 0; i < remaining; ++i) ++out[std::size_t(i)];
            return out;
        };
        switch (kind) {
            case Kind::Fixed: return split(count);
            case Kind::Adaptive: return split(std::max((available + spacing) / (d.RoundToPx(size) + spacing), 1));
            case Kind::FixedSize: {
                const int cell = d.RoundToPx(size);
                if (cell + spacing < available + spacing) return std::vector<int>(std::size_t((available + spacing) / (cell + spacing)), cell);
                return { available };
            }
        }
        return { available };
    }

    // --- the measure-time core ------------------------------------------------------------------

    namespace {
        // Holder node of one item: lays its children out like a Box at the origin.
        struct HolderPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) override {
                std::vector<Placeable*> ps;
                int w = c.minWidth, h = c.minHeight;
                for (Measurable* m : ch) { Placeable& p = m->Measure(c); w = std::max(w, p.Width()); h = std::max(h, p.Height()); ps.push_back(&p); }
                return s.Layout(w, h, [ps](PlacementScope& place) { for (Placeable* p : ps) place.PlaceRelative(*p, 0, 0); });
            }
        };
        // LazyLayoutPinnableItem: an item's pin. Pinning it pins the item around the layout too.
        struct PinItem final : ui::PinnableContainer {
            PinItem(std::weak_ptr<LazyCore> c, rt::Key k) : core(std::move(c)), key(k) {}
            void Pin() override;
            void Release() override;
            void Dispose();
            void Unpin(bool remeasure);
            std::weak_ptr<LazyCore> core;
            rt::Key key;
            std::shared_ptr<ui::PinnableContainer> parent, parentHandle;
            int count = 0;
            bool disposed = false;
        };
        struct Slot {
            std::shared_ptr<PinItem> pin;            // its LocalPinnableContainer
            // Destroyed in reverse: the composition first (its disposers unregister from the
            // registry and it unhooks its nodes from the holder).
            std::unique_ptr<LayoutNode> holder;
            std::shared_ptr<rt::SaveableStateRegistry> registry;   // the item's rememberSaveable values
            std::unique_ptr<rt::Composition> composition;
            int index = -1;
            std::uint64_t contentVersion = 0;
        };
        constexpr int kNone = std::numeric_limits<int>::min();

        // LazyLayoutItemAnimation: one item's placement delta and fade, by key.
        struct ItemAnim {
            LazyItemAnimationSpecs specs;
            bool has = false;                      // animateItem() is on this item
            bool targetSet = false;
            IntOffset target;                      // where it was last placed (layout coordinates)
            rt::State<IntOffset> delta{ IntOffset{} };   // placementDelta: drawn offset from the target
            anim::Animatable placement{ anim::Vec{ 0, 0, 0, 0 }, 2 };
            anim::Vec placementTarget{ 0, 0, 0, 0 };
            bool placing = false;
            bool movingAway = false;   // still in the data, animating out past an edge
            rt::State<float> alpha{ 1.0f };
            anim::Animatable fade{ 1.0f };
            bool appearing = false, disappearing = false, disappeared = false;
            Constraints constraints;               // to re-place a disappearing item
            int mainSize = 0;
            rt::Job placementJob, fadeJob;

            // animatePlacementDelta: stay where it was drawn, then spring to the new target with
            // the velocity so far.
            void AnimateDelta(IntOffset d) {
                if (!specs.placement) return;
                placementTarget[0] += float(d.x);
                placementTarget[1] += float(d.y);
                const anim::Vec v = placement.Value();
                delta.Set({ int(std::lround(v[0] - placementTarget[0])), int(std::lround(v[1] - placementTarget[1])) });
                placementJob = rt::Launch(rt::Dispatcher::Main(), RunPlacement());
            }
            rt::Task<> RunPlacement() {
                placing = true;
                co_await placement.AnimateTo(placementTarget, specs.placement, std::nullopt, [this](const anim::Vec& v) {
                    delta.Set({ int(std::lround(v[0] - placementTarget[0])), int(std::lround(v[1] - placementTarget[1])) });
                });
                if (placement.Value()[0] == placementTarget[0] && placement.Value()[1] == placementTarget[1]) {
                    placing = false;
                    if (movingAway && onDisappeared) onDisappeared();   // the layout drops it now
                }
            }
            void FadeIn() {
                if (!specs.fadeIn) return;
                appearing = true;
                fade.SnapTo(0.0f);
                alpha.Set(0.0f);
                fadeJob = rt::Launch(rt::Dispatcher::Main(), RunFade(1.0f, specs.fadeIn));
            }
            void FadeOut() {
                disappearing = true;
                fadeJob = rt::Launch(rt::Dispatcher::Main(), RunFade(0.0f, specs.fadeOut));
            }
            rt::Task<> RunFade(float to, anim::SpecPtr spec) {
                co_await fade.AnimateTo(to, std::move(spec));
                alpha.Set(fade.Float());
                if (to == 0 && fade.Float() == 0) { disappeared = true; if (onDisappeared) onDisappeared(); }
                if (to == 1) appearing = false;
            }
            std::function<void()> onDisappeared;   // the layout drops the item then
            bool Busy() const { return placing || appearing || (disappearing && !disappeared); }
        };

        rt::Local<ItemAnim*> LocalItemAnim{ nullptr };
        // LazyItemScopeImpl's max size: the layout's content constraints (less content padding).
        rt::Local<const rt::State<IntSize>*> LocalItemParentSize{ nullptr };
    }

    Modifier AnimateItem(Modifier m, LazyItemAnimationSpecs specs) {
        ItemAnim* a = rt::Composition::Current() ? rt::Composition::Current()->Read(LocalItemAnim) : nullptr;
        if (!a) return m;   // not in a lazy item
        a->specs = std::move(specs);
        a->has = true;
        // The fade, read at draw time (graphicsLayer { alpha }).
        return DrawWithContent(std::move(m), [a](ContentDrawScope& d) {
            const float alpha = a->fade.Float();
            if (alpha >= 1) { d.DrawContent(); return; }
            const Size sz = d.GetSize();
            d.BeginLayer({ 0, 0, sz.width, sz.height }, alpha);
            d.DrawContent();
            d.EndLayer();
        });
    }

    struct LazyCore : std::enable_shared_from_this<LazyCore> {
        // setMaxSize: what fillParentMax* items fill (Infinity on an unbounded axis).
        rt::State<IntSize> parentMaxSize{ IntSize{ Constraints::Infinity, Constraints::Infinity } };
        // stickyHeadersEnabled: off while a scroll capture runs (the header would repeat in
        // every captured tile).
        bool stickyHeadersEnabled = true;
        // LazyLayoutPinnedItemList: keys of items pinned now (kept composed and laid out).
        std::map<std::uint64_t, rt::Key> pinned;
        std::shared_ptr<ui::PinnableContainer> parentPin;   // the item around this layout, if any
        void RequestRemeasure() { if (state && state->m_Node) state->m_Node->RequestRemeasure(); }
        void RemeasureNow() { if (state && state->m_Node) state->m_Node->ForceRemeasure(); }
        bool HasVisible() const { return state && !state->m_Lines.empty(); }
        int FirstPlaced() const { return state->m_Index.Peek(); }
        int LastPlaced() const { return state->LastVisibleIndex(); }
        int AverageLineSize() const {
            int sum = 0;
            for (const auto& l : state->m_Lines) sum += l.size;
            return state->m_Lines.empty() ? 0 : sum / int(state->m_Lines.size());
        }
        // LazyLayoutBeyondBoundsInfo: items laid out past the edge for a focus search.
        std::optional<std::pair<int, int>> beyondInterval;
        bool vertical = true;
        bool grid = false;
        LazyStateBase* state = nullptr;
        LazyListState* listState = nullptr;
        LazyGridState* gridState = nullptr;
        PaddingValues contentPadding;
        Dp mainSpacing{ 0 };
        std::optional<Arrangement> mainArrangement;   // lists: placement of short content
        bool reverse = false;
        AlignmentH horizontalAlignment = AlignmentH::Start();   // list cross alignment
        AlignmentV verticalAlignment = AlignmentV::Top();
        Arrangement crossArrangement = Arrangement::Start();    // grid slots
        GridCells cells;
        bool pager = false;
        int beyondCount = 0;   // pager: beyondViewportPageCount
        std::function<int(int, int, int, int)> pageSnap;   // pager: SnapPosition
        int stickingSize = 0;   // the header sticking to the start (stickingItemsCombinedSize)
        // Staggered grids: positions of the items measured so far (a prefix from item 0).
        bool staggered = false;
        LazyStaggeredGridState* stagState = nullptr;
        StaggeredLaneInfo stagLaneInfo;
        std::vector<int> stagIndices, stagOffsets;   // per lane: the first item and its offset
        std::vector<int> stagLaneSizes;
        std::map<int, int> stagVisibleOffsets;       // last measure: visible item -> main offset
        MeasureResult MeasureStaggered(LayoutNode& self, MeasureScope& s, const Constraints& c);
        // LazyLayoutItemAnimator, shared by every mode. AnimBegin: which items appear, leave the
        // data or move out of view; AnimPlace: placement deltas, fades and edge slides for the
        // items placed now (screen positions), plus what is fading or moving away.
        struct AnimFrame {
            bool animating = false;
            std::set<std::uint64_t> placedKeys;
            std::vector<std::pair<std::uint64_t, std::shared_ptr<ItemAnim>>> disappearing;
            struct Away { std::uint64_t key; std::shared_ptr<ItemAnim> anim; int index; };
            std::vector<Away> movingAway;
        };
        struct AnimItem { int index; IntOffset cur; int size; int lane; Constraints constraints; };
        AnimFrame AnimBegin(const std::vector<int>& placedIndices);
        void AnimPlace(LayoutNode& self, AnimFrame& f, const std::vector<AnimItem>& items, IntOffset shift, int mainMax, bool reverse,
                       int spacing, int firstIndex, std::vector<std::pair<Placeable*, std::shared_ptr<ItemAnim>>>& fading);
        std::function<int(const Density&, int, int)> pageSize;
        std::vector<LazyInterval> intervals;
        std::uint64_t version = 0;
        std::map<std::uint64_t, Slot> slots;     // by item key value
        rt::Scope* context = nullptr;            // Locals the items see
        rt::SaveableStateHolder* holder = nullptr;   // keeps item state while items are off-screen
        int count = 0;

        // Lines: arithmetic when every item spans one slot, else the start item of each line.
        int slotsPerLine = 1;
        bool regular = true;
        std::vector<int> lineStarts;
        std::uint64_t lineCacheVersion = ~0ull;
        int lineCacheSlots = -1;

        // Item animations (LazyLayoutItemAnimator), by key; only items with animateItem().
        std::map<std::uint64_t, std::shared_ptr<ItemAnim>> anims;
        std::unordered_map<std::uint64_t, int> keyIndex, prevKeyIndex;
        std::uint64_t keyIndexVersion = ~0ull;
        bool hadLayout = false;
        int prevFirstIndex = 0;
        std::shared_ptr<ItemAnim> AnimFor(std::uint64_t key) {
            auto& a = anims[key];
            if (!a) a = std::make_shared<ItemAnim>();
            return a;
        }

        static std::string SaveKey(std::uint64_t keyValue) { return std::to_string(keyValue); }
        void DisposeSlot(std::uint64_t keyValue, Slot& slot) {
            // The pin goes first: content releasing it while it is disposed changes nothing.
            if (slot.pin) slot.pin->Dispose();
            // Saves the item's state while it is still composed, then disposes it.
            if (holder && slot.registry) holder->Close(SaveKey(keyValue));
            slot.composition.reset();
            slot.registry.reset();
        }

        const LazyInterval* IntervalOf(int index, int& local) const {
            int base = 0;
            for (const auto& iv : intervals) {
                if (index < base + iv.count) { local = index - base; return &iv; }
                base += iv.count;
            }
            return nullptr;
        }
        rt::Key UserKey(int index) const {
            int local = 0;
            if (const auto* iv = IntervalOf(index, local); iv && iv->key) return iv->key(local);
            rt::Key k = rt::Key::Positional();
            k.value = std::uint64_t(index);
            return k;
        }
        rt::Key KeyOf(int index) const {
            int local = 0;
            if (const auto* iv = IntervalOf(index, local); iv && iv->key) { rt::Key k = iv->key(local); k.value = k.value * 31 + 0x51ed; return k; }
            rt::Key k = rt::Key::Positional();
            k.value = std::uint64_t(index) * 2 + 1;   // index-keyed items
            return k;
        }
        void Content(int index) const {
            int local = 0;
            if (const auto* iv = IntervalOf(index, local)) iv->content(iv->sticky ? index : local);
        }
        bool IsSticky(int index) const { int local = 0; const auto* iv = IntervalOf(index, local); return iv && iv->sticky; }
        // The last sticky header at or before `index`, or -1.
        int StickyAtOrBefore(int index) const {
            int base = 0, found = -1;
            for (const auto& iv : intervals) {
                if (base > index) break;
                if (iv.sticky) found = base;
                base += iv.count;
            }
            return found;
        }
        int IndexOfKey(rt::Key key, int hint) const {
            if (hint >= 0 && hint < count && KeyOf(hint).value == key.value) return hint;
            for (int i = 0; i < count; ++i) if (KeyOf(i).value == key.value) return i;
            return -1;
        }

        // spanOf: headers fill the line; spans are clamped to [1, slotsPerLine].
        int SpanOf(int index, int maxCurrent) const {
            if (!grid) return 1;
            int local = 0;
            const auto* iv = IntervalOf(index, local);
            if (!iv) return 1;
            if (iv->sticky) return slotsPerLine;
            if (!iv->span) return 1;
            return std::clamp(iv->span(local, maxCurrent, slotsPerLine), 1, slotsPerLine);
        }
        void UpdateLines() {
            regular = !grid || std::none_of(intervals.begin(), intervals.end(), [](const LazyInterval& iv) { return iv.span || iv.sticky; });
            if (regular) return;
            if (lineCacheVersion == version && lineCacheSlots == slotsPerLine) return;
            lineCacheVersion = version;
            lineCacheSlots = slotsPerLine;
            lineStarts.clear();
            int used = 0;
            for (int i = 0; i < count; ++i) {
                const int s = SpanOf(i, used == 0 ? slotsPerLine : slotsPerLine - used);
                if (used == 0 || used + s > slotsPerLine) { lineStarts.push_back(i); used = s; }
                else used += s;
                if (used >= slotsPerLine) used = 0;
            }
        }
        int LineCount() const { return regular ? (count + slotsPerLine - 1) / slotsPerLine : int(lineStarts.size()); }
        int LineOf(int index) const {
            if (regular) return index / slotsPerLine;
            auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), index);
            return int(it - lineStarts.begin()) - 1;
        }
        int LineFirst(int line) const { return regular ? line * slotsPerLine : lineStarts[std::size_t(line)]; }
        int LineEnd(int line) const {
            if (regular) return std::min(count, (line + 1) * slotsPerLine);
            return line + 1 < int(lineStarts.size()) ? lineStarts[std::size_t(line) + 1] : count;
        }

        Slot& Compose(int index, rt::Dispatcher& d) {
            const rt::Key key = KeyOf(index);
            Slot& slot = slots[key.value];
            if (!slot.holder) {
                slot.holder = std::make_unique<LayoutNode>();
                slot.holder->SetMeasurePolicy(std::make_shared<HolderPolicy>());
                slot.composition = std::make_unique<rt::Composition>(*slot.holder, d);
                if (holder) slot.registry = holder->Open(SaveKey(key.value));
            }
            slot.composition->SetParentContext(context);
            if (!slot.pin) slot.pin = std::make_shared<PinItem>(weak_from_this(), key);
            slot.pin->parent = parentPin;
            if (slot.index != index || slot.contentVersion != version) {
                slot.index = index;
                slot.contentVersion = version;
                rt::SaveableStateRegistry* reg = slot.registry.get();
                ItemAnim* anim = AnimFor(key.value).get();
                std::shared_ptr<ui::PinnableContainer> pin = slot.pin;
                slot.composition->SetContent([this, index, reg, anim, pin] {
                    auto* c = rt::Composition::Current();
                    c->Provide(LocalItemParentSize, static_cast<const rt::State<IntSize>*>(&parentMaxSize), [this, index, reg, c, anim, pin] {
                        c->Provide(LocalItemAnim, anim, [this, index, reg, c, pin] {
                            c->Provide(ui::LocalPinnableContainer, pin, [this, index, reg, c] {
                                if (!reg) { Content(index); return; }
                                c->Provide(rt::LocalSaveableStateRegistry, reg, [this, index] { Content(index); });
                            });
                        });
                    });
                });
            }
            slot.composition->Recompose();
            return slot;
        }

        MeasureResult Measure(LayoutNode& self, MeasureScope& s, const Constraints& c);
    };

    namespace {
        void PinItem::Pin() {
            if (disposed) return;
            if (count++ == 0) {
                if (auto c = core.lock()) c->pinned[key.value] = key;
                parentHandle = parent;
                if (parentHandle) parentHandle->Pin();
            }
        }
        void PinItem::Release() {
            if (disposed || count == 0) return;
            if (--count == 0) Unpin(true);
        }
        void PinItem::Dispose() {
            if (disposed) return;
            disposed = true;
            if (count > 0) Unpin(false);
            count = 0;
        }
        void PinItem::Unpin(bool remeasure) {
            if (auto c = core.lock()) {
                c->pinned.erase(key.value);
                // Scrolled away meanwhile: the next measure lets it go.
                if (remeasure) c->RequestRemeasure();
            }
            if (parentHandle) { parentHandle->Release(); parentHandle = nullptr; }
        }
    }

    // --- state ------------------------------------------------------------------------------------

    float LazyStateBase::ConsumeScrollDelta(float delta) {
        if (!m_Node) return 0;
        if (delta < 0 && !CanScrollBackward()) return 0;
        if (delta > 0 && !CanScrollForward()) return 0;
        // The measure consumes the carried sub-pixel remainder too; report only this delta's part.
        const float carried = m_PendingScroll;
        m_PendingScroll += delta;
        m_LastConsumed = carried;
        m_Node->ForceRemeasure();
        return m_LastConsumed - carried;
    }

    void LazyStateBase::ScrollToItem(int index, int offset) {
        StopScroll();
        m_Index.Set(index);
        m_Offset.Set(offset);
        m_HasFirstKey = false;
        m_PendingScroll = 0;
        if (m_Node) m_Node->RequestRemeasure();
    }

    void LazyStateBase::RequestScrollToItem(int index, int offset) {
        m_Index.Set(index);
        m_Offset.Set(offset);
        m_HasFirstKey = false;
        m_PendingScroll = 0;
        if (m_Node) m_Node->RequestRemeasure();
    }

    void LazyStateBase::SnapToItem(int index, int offset) {
        m_Index.Set(index);
        m_Offset.Set(offset);
        m_HasFirstKey = false;
        m_PendingScroll = 0;
        if (m_Node) m_Node->ForceRemeasure();
    }

    // calculateDistanceTo: exact for composed items, else by the average visible line size
    // (LazyListScrollScope / LazyGridScrollScope).
    int LazyStateBase::DistanceTo(int index, int targetOffset) const {
        if (m_Lines.empty()) return 0;
        int d = 0;
        if (!Visible(index)) {
            int sum = 0;
            for (const auto& l : m_Lines) sum += l.size;
            const int average = sum / int(m_Lines.size()) + m_Spacing;
            const bool before = index < m_Index.Peek();
            const int linesDiff = (index - m_Index.Peek() + (m_SlotsPerLine - 1) * (before ? -1 : 1)) / m_SlotsPerLine;
            d = average * linesDiff - m_Offset.Peek();
        } else {
            for (const auto& l : m_Lines) if (index >= l.first && index <= l.last) { d = l.offset; break; }
        }
        return d + targetOffset;
    }

    rt::Task<> LazyStateBase::AnimateScrollToItem(int index, int scrollOffset) {
        constexpr int kTeleport = 100;   // numOfItemsForTeleport
        const float targetDistance = 2500 * m_Density, boundDistance = 1500 * m_Density, minDistance = 50 * m_Density;
        anim::Animatable* running = nullptr;
        bool stopped = false;
        ScrollSession session(*this, [&] { stopped = true; if (running) running->Stop(); });
        index = std::max(0, index);
        float velocity = 0;
        bool found = Visible(index);
        const bool forward = index > m_Index.Peek();
        auto overshot = [&] {
            const int first = m_Index.Peek(), off = m_Offset.Peek();
            return forward ? (first > index || (first == index && off > scrollOffset))
                           : (first < index || (first == index && off < scrollOffset));
        };
        bool loop = !found;
        int loops = 1;
        while (loop && m_TotalItems > 0) {
            const int expected = DistanceTo(index) + scrollOffset;
            const float target = std::abs(float(expected)) < targetDistance
                ? (forward ? 1.0f : -1.0f) * std::max(std::abs(float(expected)), minDistance)
                : (forward ? targetDistance : -targetDistance);
            anim::Animatable a(0.0f);
            running = &a;
            float prev = 0;
            co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, anim::Spring(), anim::Vec{ velocity, 0, 0, 0 }, [&](const anim::Vec& vv) {
                const float value = vv[0];
                // The animation state's velocity carries into the next loop (stopping zeroes the Animatable's).
                velocity = a.Velocity()[0];
                if (stopped) return;
                if (!Visible(index)) {
                    const float coerced = target > 0 ? std::min(value, target) : std::max(value, target);
                    const float delta = coerced - prev;
                    const float consumed = ConsumeScrollDelta(delta);
                    if (!Visible(index) && !overshot()) {
                        if (std::abs(delta - consumed) > 0.5f) { loop = false; a.Stop(); return; }   // hit the end
                        prev += delta;
                        if (forward ? value > boundDistance : value < -boundDistance) a.Stop();
                        if (forward) {
                            if (loops >= 2 && index - LastVisibleIndex() > kTeleport) SnapToItem(index - kTeleport, 0);
                        } else {
                            if (loops >= 2 && m_Index.Peek() - index > kTeleport) SnapToItem(index + kTeleport, 0);
                        }
                    }
                }
                if (overshot()) {
                    SnapToItem(index, scrollOffset);
                    loop = false;
                    a.Stop();
                } else if (Visible(index)) {
                    found = true;
                    loop = false;
                    a.Stop();
                }
            });
            running = nullptr;
            if (stopped) co_return;
            ++loops;
        }
        if (!found) co_return;
        // Seek: the exact distance, continuing with the velocity so far.
        const float target = float(DistanceTo(index) + scrollOffset);
        if (target != 0) {
            anim::Animatable a(0.0f);
            running = &a;
            float prev = 0;
            co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, anim::Spring(), anim::Vec{ velocity, 0, 0, 0 }, [&](const anim::Vec& vv) {
                if (stopped) return;
                const float value = vv[0];
                const float coerced = target > 0 ? std::min(value, target) : std::max(value, target);
                const float delta = coerced - prev;
                const float consumed = ConsumeScrollDelta(delta);
                if (std::abs(delta - consumed) > 0.5f || coerced != value) a.Stop();
                prev += delta;
            });
            running = nullptr;
            if (stopped) co_return;
        }
        SnapToItem(index, scrollOffset);
    }

    // --- measure ----------------------------------------------------------------------------------

    // --- staggered grids (LazyStaggeredGridMeasure, simplified: lanes from a cached prefix) -----

    // LazyStaggeredGridMeasure: each lane keeps its first item and its offset; scrolling back
    // finds earlier items lane by lane (StaggeredLaneInfo remembers assignments), so a far
    // scrollToItem never measures the items before the target.
    MeasureResult LazyCore::MeasureStaggered(LayoutNode& self, MeasureScope& s, const Constraints& c) {
        using LI = StaggeredLaneInfo;
        LazyStateBase& st = *state;
        st.m_Node = &self;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        const int mainMax = vertical ? c.maxHeight : c.maxWidth;
        const int crossMax = vertical ? c.maxWidth : c.maxHeight;
        const int before = s.RoundToPx(vertical ? contentPadding.top : contentPadding.start);
        const int after = s.RoundToPx(vertical ? contentPadding.bottom : contentPadding.end);
        const int crossBefore = s.RoundToPx(vertical ? contentPadding.start : contentPadding.top);
        const int crossPad = crossBefore + s.RoundToPx(vertical ? contentPadding.end : contentPadding.bottom);
        const int spacing = s.RoundToPx(mainSpacing);
        const int crossAvailable = std::max(0, crossMax - crossPad);
        const int crossSpacing = s.RoundToPx(crossArrangement.Spacing());
        std::vector<int> sizes = cells.CellSizes(s, crossAvailable, crossSpacing), positions(sizes.size(), 0);
        crossArrangement.Arrange(s, crossAvailable, sizes, LayoutDirection::Ltr, positions);
        const int lanes = int(sizes.size());
        slotsPerLine = lanes;
        const int viewport = mainMax - before - after;   // mainAxisAvailableSize
        const int w = vertical ? c.ConstrainWidth(crossMax == Constraints::Infinity ? crossPad : crossMax) : c.ConstrainWidth(mainMax);
        const int h = vertical ? c.ConstrainHeight(mainMax) : c.ConstrainHeight(crossMax == Constraints::Infinity ? crossPad : crossMax);

        if (count == 0 || lanes == 0) {
            for (auto& [k, slot] : slots) DisposeSlot(k, slot);
            slots.clear();
            self.SetChildrenDuringMeasure({});
            st.m_Lines.clear();
            st.m_TotalItems = 0;
            st.m_LastConsumed = st.m_PendingScroll = 0;
            st.m_CanForward = false;
            st.OnMeasured();
            return s.Layout(c.minWidth, c.minHeight);
        }

        // --- measured items (cached by index, lane and span for this pass) ---
        struct SpanR { int start, size; bool Full() const { return size != 1; } int LaneInfo() const { return Full() ? LI::kLaneFullSpan : start; } };
        struct SItem { int index, lane, span, size, sws; Placeable* p; Constraints constraints; bool visible = true; int main = 0, cross = 0; };
        std::deque<SItem> pool;
        std::map<std::tuple<int, int, int>, SItem*> cache;
        std::vector<LayoutNode*> active;
        for (auto& [k, sl] : slots) if (sl.holder) active.push_back(sl.holder.get());   // no detaching mid-measure
        auto isFull = [&](int i) { int local = 0; const auto* iv = IntervalOf(i, local); return iv && iv->span && iv->span(local, lanes, lanes) >= lanes; };
        auto spanRange = [&](int i, int lane) { return isFull(i) ? SpanR{ 0, lanes } : SpanR{ lane, 1 }; };
        auto getAndMeasure = [&](int i, SpanR span) -> SItem& {
            const int start = std::min(span.start, lanes - 1), size = std::min(span.size, lanes - start);
            auto key = std::make_tuple(i, start, size);
            if (auto it = cache.find(key); it != cache.end()) return *it->second;
            Slot& sl = Compose(i, d);
            if (std::find(active.begin(), active.end(), sl.holder.get()) == active.end()) active.push_back(sl.holder.get());
            self.SetChildrenDuringMeasure(active);
            const int cw = size == 1 ? sizes[std::size_t(start)]
                                     : positions[std::size_t(start + size - 1)] + sizes[std::size_t(start + size - 1)] - positions[std::size_t(start)];
            const Constraints ic = vertical ? Constraints{ cw, cw, 0, Constraints::Infinity } : Constraints{ 0, Constraints::Infinity, cw, cw };
            Placeable& p = sl.holder->AsMeasurable().Measure(ic);
            const int mainSize = vertical ? p.Height() : p.Width();
            pool.push_back({ i, start, size, mainSize, mainSize + spacing, &p, ic });
            cache[key] = &pool.back();
            return pool.back();
        };
        auto maxIn = [](const std::vector<int>& v, SpanR r) { int m = std::numeric_limits<int>::min(); for (int l = r.start; l < r.start + r.size; ++l) m = std::max(m, v[std::size_t(l)]); return m; };
        auto offsetBy = [](std::vector<int>& v, int delta) { for (int& x : v) x += delta; };
        auto indexOfMin = [](const std::vector<int>& v, int minBound = std::numeric_limits<int>::min()) {
            int result = -1, mn = std::numeric_limits<int>::max();
            // Values in (minBound, min): IntArray.indexOfMinValue.
            for (int i = 0; i < int(v.size()); ++i) if (v[std::size_t(i)] > minBound && v[std::size_t(i)] < mn) { mn = v[std::size_t(i)]; result = i; }
            return result;
        };
        auto indexOfMax = [](const std::vector<int>& v) {
            int result = -1, mx = std::numeric_limits<int>::min();
            for (int i = 0; i < int(v.size()); ++i) if (mx < v[std::size_t(i)]) { mx = v[std::size_t(i)]; result = i; }
            return result;
        };

        // fillNearestIndices: the lanes around `itemIndex` (previous lanes take earlier items).
        auto fillNearest = [&](int itemIndex) {
            std::vector<int> idx(std::size_t(lanes), 0);
            if (isFull(itemIndex)) { std::fill(idx.begin(), idx.end(), itemIndex); return idx; }
            stagLaneInfo.EnsureValidIndex(itemIndex + lanes);
            const int prevLane = stagLaneInfo.GetLane(itemIndex);
            const int target = prevLane == LI::kLaneUnset || prevLane == LI::kLaneFullSpan ? 0 : std::min(prevLane, lanes);
            int current = itemIndex;
            for (int lane = target - 1; lane >= 0; --lane) {
                idx[std::size_t(lane)] = stagLaneInfo.FindPreviousItemIndex(current, lane);
                if (idx[std::size_t(lane)] == LI::kLaneUnset) { std::fill(idx.begin(), idx.begin() + lane, -1); break; }
                current = idx[std::size_t(lane)];
            }
            idx[std::size_t(std::min(target, lanes - 1))] = itemIndex;
            current = itemIndex;
            for (int lane = target + 1; lane < lanes; ++lane) {
                idx[std::size_t(lane)] = stagLaneInfo.FindNextItemIndex(current, lane);
                current = idx[std::size_t(lane)];
            }
            return idx;
        };

        // --- the scroll position: per-lane first items and offsets ---
        if (sizes != stagLaneSizes) { stagLaneSizes = sizes; }
        if (int(stagIndices.size()) != lanes) {
            // Initial state or a resized grid: lanes after the known ones start after them.
            stagLaneInfo.Reset();
            std::vector<int> idx(static_cast<std::size_t>(lanes)), off(static_cast<std::size_t>(lanes));
            for (int lane = 0; lane < lanes; ++lane) {
                idx[std::size_t(lane)] = lane < int(stagIndices.size()) && stagIndices[std::size_t(lane)] != LI::kLaneUnset ? stagIndices[std::size_t(lane)]
                                       : lane == 0 ? 0 : maxIn(idx, { 0, lane }) + 1;
                stagLaneInfo.SetLane(idx[std::size_t(lane)], lane);
                off[std::size_t(lane)] = lane < int(stagOffsets.size()) ? stagOffsets[std::size_t(lane)] : lane == 0 ? 0 : off[std::size_t(lane - 1)];
            }
            stagIndices = std::move(idx);
            stagOffsets = std::move(off);
        }
        if (!st.m_HasFirstKey) {
            // scrollToItem / requestScrollToItem (snapToItemInternal): a visible target moves the
            // offsets by its distance; otherwise the lanes are filled around it.
            const int target = std::clamp(st.m_Index.Peek(), 0, count - 1), scrollOffset = st.m_Offset.Peek();
            auto vis = stagVisibleOffsets.find(target);
            if (vis != stagVisibleOffsets.end()) {
                for (int& o : stagOffsets) o += vis->second + scrollOffset;
            } else {
                stagIndices = fillNearest(target);
                std::fill(stagOffsets.begin(), stagOffsets.end(), scrollOffset);
            }
        } else {
            // updateScrollPositionIfTheFirstItemWasMoved: keep the first item by key.
            const int found = IndexOfKey(st.m_FirstKey, stagIndices.empty() ? 0 : stagIndices[0]);
            if (found >= 0 && std::find(stagIndices.begin(), stagIndices.end(), found) == stagIndices.end()) stagIndices = fillNearest(found);
        }

        // --- measure (LazyStaggeredGridMeasureContext.measure) ---
        const float pending = st.m_PendingScroll;
        st.m_PendingScroll = 0;
        const int wholeDelta = RoundJava(pending);
        struct Out {
            bool restart = false;
            int scrollDelta = 0;
            std::vector<int> fi, fo, ci, co;
            std::vector<SItem*> visibleItems;
            std::vector<std::deque<SItem*>> measured;
            std::vector<int> scrollOffsets;
        };
        auto measure = [&](int initialScrollDelta, std::vector<int> fi, std::vector<int> fo, bool canRestart) -> Out {
            Out out;
            const int itemCount = count;
            int scrollDelta = initialScrollDelta;
            // ensureIndicesInRange (last lanes keep the last items)
            for (int i = lanes - 1; i >= 0; --i) {
                while (fi[std::size_t(i)] >= itemCount || !stagLaneInfo.AssignedToLane(fi[std::size_t(i)], i)) fi[std::size_t(i)] = stagLaneInfo.FindPreviousItemIndex(fi[std::size_t(i)], i);
                if (fi[std::size_t(i)] >= 0) {
                    const int itemIndex = fi[std::size_t(i)];
                    if (!isFull(itemIndex)) {
                        int lane = i;
                        if (stagLaneInfo.GetLane(itemIndex) == LI::kLaneFullSpan) {
                            const int targetLane = int(std::find(fi.begin(), fi.end(), itemIndex) - fi.begin());
                            for (int l = targetLane + 1; l <= i; ++l) if (fi[std::size_t(l)] == itemIndex) fi[std::size_t(l)] = stagLaneInfo.FindPreviousItemIndex(itemIndex, l);
                            lane = targetLane;
                        }
                        stagLaneInfo.SetLane(itemIndex, lane);
                    }
                }
            }
            offsetBy(fo, -scrollDelta);
            std::vector<std::deque<SItem*>> measured(static_cast<std::size_t>(lanes));
            offsetBy(fo, -before);
            auto hasSpaceBeforeFirst = [&] {
                for (int lane = 0; lane < lanes; ++lane) if (fo[std::size_t(lane)] < std::max(-spacing, 0) && fi[std::size_t(lane)] > 0) return true;
                return false;
            };
            int laneToCheckForGaps = -1;
            while (hasSpaceBeforeFirst()) {
                const int laneIndex = indexOfMax(fi);
                const int itemIndex = fi[std::size_t(laneIndex)];
                for (int i = 0; i < lanes; ++i)
                    if (fi[std::size_t(i)] != fi[std::size_t(laneIndex)] && fo[std::size_t(i)] < fo[std::size_t(laneIndex)]) fo[std::size_t(i)] = fo[std::size_t(laneIndex)];
                const int previous = stagLaneInfo.FindPreviousItemIndex(itemIndex, laneIndex);
                if (previous < 0) { laneToCheckForGaps = laneIndex; break; }
                const SpanR span = spanRange(previous, laneIndex);
                stagLaneInfo.SetLane(previous, span.LaneInfo());
                SItem& item = getAndMeasure(previous, span);
                const int offset = maxIn(fo, span);
                const std::vector<int>* gaps = span.Full() ? stagLaneInfo.GetGaps(previous) : nullptr;
                for (int lane = span.start; lane < span.start + span.size; ++lane) {
                    fi[std::size_t(lane)] = previous;
                    fo[std::size_t(lane)] = offset + item.sws + (gaps ? (*gaps)[std::size_t(lane)] : 0);
                }
            }
            auto misalignedStart = [&](int ref) {
                for (int lane = 0; lane < lanes; ++lane)
                    if (stagLaneInfo.FindPreviousItemIndex(fi[std::size_t(lane)], lane) == LI::kLaneUnset && fo[std::size_t(lane)] != fo[std::size_t(ref)]) return true;
                for (int lane = 0; lane < lanes; ++lane)
                    if (stagLaneInfo.FindPreviousItemIndex(fi[std::size_t(lane)], lane) != LI::kLaneUnset && fo[std::size_t(lane)] >= fo[std::size_t(ref)]) return true;
                const int firstLane = stagLaneInfo.GetLane(0);
                return firstLane != 0 && firstLane != LI::kLaneUnset && firstLane != LI::kLaneFullSpan;
            };
            const int minOffset = -before;
            if (fo[0] < minOffset) {
                const int notConsumed = minOffset - fo[0];
                offsetBy(fo, minOffset - fo[0]);
                scrollDelta -= notConsumed;
            }
            offsetBy(fo, before);
            if (laneToCheckForGaps == -1) {
                auto it = std::find(fi.begin(), fi.end(), 0);
                laneToCheckForGaps = it == fi.end() ? -1 : int(it - fi.begin());
            }
            if (laneToCheckForGaps != -1 && misalignedStart(laneToCheckForGaps) && canRestart) {
                stagLaneInfo.Reset();
                out.restart = true;
                out.scrollDelta = scrollDelta;
                out.fi.assign(std::size_t(lanes), -1);
                out.fo.assign(std::size_t(lanes), fo[std::size_t(laneToCheckForGaps)]);
                return out;
            }
            std::vector<int> ci = fi, co(static_cast<std::size_t>(lanes));
            for (int lane = 0; lane < lanes; ++lane) co[std::size_t(lane)] = -fo[std::size_t(lane)];
            const int minVisibleOffset = minOffset + spacing;
            const int maxOffset = std::max(0, viewport + after);
            auto place = [&](SItem& item, SpanR span, int offset, std::vector<int>* gaps) {
                for (int lane = span.start; lane < span.start + span.size; ++lane) {
                    if (gaps) (*gaps)[std::size_t(lane)] = offset - co[std::size_t(lane)];
                    co[std::size_t(lane)] = offset + item.sws;
                    ci[std::size_t(lane)] = item.index;
                    measured[std::size_t(lane)].push_back(&item);
                }
                if (offset < minVisibleOffset && co[std::size_t(span.start)] <= minVisibleOffset) item.visible = false;
            };
            // The items the lanes start with, in index order (their lanes depend on earlier ones).
            int initialMeasured = 0;
            int initialLane = indexOfMin(ci);
            while (initialLane != -1 && initialMeasured < lanes) {
                const int itemIndex = ci[std::size_t(initialLane)];
                const int laneIndex = initialLane;
                initialLane = indexOfMin(ci, itemIndex);
                ++initialMeasured;
                if (itemIndex < 0) continue;
                const SpanR span = spanRange(itemIndex, laneIndex);
                SItem& item = getAndMeasure(itemIndex, span);
                stagLaneInfo.SetLane(itemIndex, span.LaneInfo());
                const int offset = maxIn(co, span);
                std::vector<int> gaps;
                if (span.Full()) { const auto* g = stagLaneInfo.GetGaps(itemIndex); gaps = g ? *g : std::vector<int>(std::size_t(lanes), 0); }
                place(item, span, offset, span.Full() ? &gaps : nullptr);
                if (span.Full()) { stagLaneInfo.SetGaps(itemIndex, &gaps); initialMeasured = lanes; }
            }
            // Then forward until the viewport is full (at least one item).
            auto allEmpty = [&] { for (auto& q : measured) if (!q.empty()) return false; return true; };
            auto needMore = [&] { for (int v : co) if (v < maxOffset || v <= 0) return true; return false; };
            while (needMore() || allEmpty()) {
                const int lane = indexOfMin(co);
                const int itemIndex = *std::max_element(ci.begin(), ci.end()) + 1;
                if (itemIndex >= itemCount || lane < 0) break;
                const SpanR span = spanRange(itemIndex, lane);
                stagLaneInfo.SetLane(itemIndex, span.LaneInfo());
                SItem& item = getAndMeasure(itemIndex, span);
                const int offset = maxIn(co, span);
                std::vector<int> gaps;
                if (span.Full()) { const auto* g = stagLaneInfo.GetGaps(itemIndex); gaps = g ? *g : std::vector<int>(std::size_t(lanes), 0); }
                place(item, span, offset, span.Full() ? &gaps : nullptr);
                stagLaneInfo.SetGaps(itemIndex, span.Full() ? &gaps : nullptr);
            }
            // Drop items measured only to position the others.
            for (int lane = 0; lane < lanes; ++lane) {
                auto& q = measured[std::size_t(lane)];
                while (q.size() > 1 && !q.front()->visible) {
                    SItem* item = q.front();
                    q.pop_front();
                    const std::vector<int>* gaps = item->span != 1 ? stagLaneInfo.GetGaps(item->index) : nullptr;
                    fo[std::size_t(lane)] -= item->sws + (gaps ? (*gaps)[std::size_t(lane)] : 0);
                }
                fi[std::size_t(lane)] = q.empty() ? LI::kLaneUnset : q.front()->index;
            }
            if (std::find(ci.begin(), ci.end(), itemCount - 1) != ci.end()) offsetBy(co, -spacing);   // no spacing after the last
            // Short of the end: scroll back, filling the start.
            if (std::all_of(co.begin(), co.end(), [&](int v) { return v < viewport; })) {
                const int maxLane = indexOfMax(co);
                const int toScrollBack = viewport - co[std::size_t(maxLane)];
                offsetBy(fo, -toScrollBack);
                offsetBy(co, toScrollBack);
                bool gapDetected = false;
                while (std::any_of(fo.begin(), fo.end(), [&](int v) { return v < before; })) {
                    int laneIndex = indexOfMin(fo);
                    const int nextLane = indexOfMax(fi);
                    if (laneIndex != nextLane) {
                        if (fo[std::size_t(laneIndex)] == fo[std::size_t(nextLane)]) laneIndex = nextLane;
                        else gapDetected = true;
                    }
                    const int currentIndex = fi[std::size_t(laneIndex)] == -1 ? itemCount : fi[std::size_t(laneIndex)];
                    const int previous = stagLaneInfo.FindPreviousItemIndex(currentIndex, laneIndex);
                    if (previous < 0) {
                        if ((gapDetected || misalignedStart(laneIndex)) && canRestart) {
                            stagLaneInfo.Reset();
                            out.restart = true;
                            out.scrollDelta = scrollDelta;
                            out.fi.assign(std::size_t(lanes), -1);
                            out.fo.assign(std::size_t(lanes), fo[std::size_t(laneIndex)]);
                            return out;
                        }
                        break;
                    }
                    const SpanR span = spanRange(previous, laneIndex);
                    stagLaneInfo.SetLane(previous, span.LaneInfo());
                    SItem& item = getAndMeasure(previous, span);
                    const int offset = maxIn(fo, span);
                    const std::vector<int>* gaps = span.Full() ? stagLaneInfo.GetGaps(previous) : nullptr;
                    for (int lane = span.start; lane < span.start + span.size; ++lane) {
                        if (fo[std::size_t(lane)] != offset) gapDetected = true;
                        measured[std::size_t(lane)].push_front(&item);
                        fi[std::size_t(lane)] = previous;
                        fo[std::size_t(lane)] = offset + item.sws + (gaps ? (*gaps)[std::size_t(lane)] : 0);
                    }
                }
                if (gapDetected && canRestart) {
                    stagLaneInfo.Reset();
                    out.restart = true;
                    out.scrollDelta = scrollDelta;
                    out.fi = fi;
                    out.fo = fo;
                    return out;
                }
                scrollDelta += toScrollBack;
                const int minLane = indexOfMin(fo);
                if (minLane >= 0 && fo[std::size_t(minLane)] < 0) {
                    const int v = fo[std::size_t(minLane)];
                    scrollDelta += v;
                    offsetBy(co, v);
                    offsetBy(fo, -v);
                }
            }
            std::vector<int> scrollOffsets(static_cast<std::size_t>(lanes));
            for (int lane = 0; lane < lanes; ++lane) scrollOffsets[std::size_t(lane)] = -fo[std::size_t(lane)];
            // Items fully inside the start padding do not count for the scroll position.
            if (before > spacing) {
                for (int lane = 0; lane < lanes; ++lane) {
                    auto& q = measured[std::size_t(lane)];
                    for (std::size_t i = 0; i < q.size(); ++i) {
                        const std::vector<int>* gaps = stagLaneInfo.GetGaps(q[i]->index);
                        const int size = q[i]->sws + (gaps ? (*gaps)[std::size_t(lane)] : 0);
                        if (i + 1 != q.size() && fo[std::size_t(lane)] != 0 && fo[std::size_t(lane)] >= size) {
                            fo[std::size_t(lane)] -= size;
                            fi[std::size_t(lane)] = q[i + 1]->index;
                        } else {
                            break;
                        }
                    }
                }
            }
            out.scrollDelta = scrollDelta;
            out.fi = std::move(fi);
            out.fo = std::move(fo);
            out.ci = std::move(ci);
            out.co = std::move(co);
            out.measured = std::move(measured);
            out.scrollOffsets = std::move(scrollOffsets);
            return out;
        };
        // Compose's scroll delta is negative going forward; kiln's is positive.
        Out out = measure(-wholeDelta, stagIndices, stagOffsets, true);
        if (out.restart) out = measure(out.scrollDelta, out.fi, out.fo, false);
        const int minOffset = -before, maxOffset = std::max(0, viewport + after);

        // calculateVisibleItems: positions in index order across the lanes.
        std::vector<int> visible;
        std::map<int, SItem*> byIndex;
        {
            auto lanesQ = out.measured;
            std::vector<int> offs = out.scrollOffsets;
            for (;;) {
                int laneIndex = -1, best = std::numeric_limits<int>::max();
                for (int l = 0; l < lanes; ++l) if (!lanesQ[std::size_t(l)].empty() && lanesQ[std::size_t(l)].front()->index < best) { best = lanesQ[std::size_t(l)].front()->index; laneIndex = l; }
                if (laneIndex < 0) break;
                SItem* item = lanesQ[std::size_t(laneIndex)].front();
                lanesQ[std::size_t(laneIndex)].pop_front();
                if (item->lane != laneIndex) continue;
                const SpanR span{ item->lane, item->span };
                const int main = maxIn(offs, span);
                if (main + item->size >= minOffset && main <= maxOffset) {
                    item->main = main;
                    item->cross = positions[std::size_t(laneIndex)];
                    visible.push_back(item->index);
                    byIndex[item->index] = item;
                }
                for (int l = span.start; l < span.start + span.size; ++l) offs[std::size_t(l)] = main + item->sws;
            }
        }

        // Pinned items and a focus search's beyond-bounds range past the viewport, stacked before
        // and after the visible ones (calculateExtraItems).
        std::vector<int> extra;
        {
            std::vector<int> wanted;
            if (beyondInterval) for (int i = beyondInterval->first; i <= beyondInterval->second; ++i) wanted.push_back(i);
            for (const auto& [kv, key] : pinned) {
                auto sl = slots.find(kv);
                wanted.push_back(IndexOfKey(key, sl != slots.end() ? sl->second.index : -1));
            }
            std::sort(wanted.begin(), wanted.end());
            wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
            auto firstIn = [&](int lane) { return out.measured[std::size_t(lane)].empty() ? -1 : out.measured[std::size_t(lane)].front()->index; };
            std::vector<int> beforeList, afterList;
            for (int i : wanted) {
                if (i < 0 || i >= count || byIndex.count(i)) continue;
                const int lane = stagLaneInfo.GetLane(i);
                bool isBefore, isAfter;
                if (lane == LI::kLaneUnset || lane == LI::kLaneFullSpan) {
                    isBefore = true; for (int l = 0; l < lanes; ++l) isBefore = isBefore && firstIn(l) > i;
                    isAfter = true; for (int v : out.ci) isAfter = isAfter && v < i;
                } else {
                    isBefore = firstIn(lane) > i;
                    isAfter = out.ci[std::size_t(lane)] < i;
                }
                if (isBefore) beforeList.push_back(i); else if (isAfter) afterList.push_back(i);
            }
            int off = out.scrollOffsets[0];
            for (auto it = beforeList.rbegin(); it != beforeList.rend(); ++it) {
                const int lane = std::max(0, stagLaneInfo.GetLane(*it));
                SItem& item = getAndMeasure(*it, spanRange(*it, std::min(lane, lanes - 1)));
                off -= item.sws;
                item.main = off;
                item.cross = 0;
                byIndex[*it] = &item;
                extra.push_back(*it);
            }
            off = visible.empty() ? out.scrollOffsets[0] : byIndex[visible.back()]->main + byIndex[visible.back()]->sws;
            for (int i : afterList) {
                const int lane = std::max(0, stagLaneInfo.GetLane(i));
                SItem& item = getAndMeasure(i, spanRange(i, std::min(lane, lanes - 1)));
                item.main = off;
                item.cross = 0;
                off += item.sws;
                byIndex[i] = &item;
                extra.push_back(i);
            }
        }

        // The scroll position and what was consumed (the requested scroll unless clamped).
        float consumed;
        if (-out.scrollDelta == wholeDelta) { consumed = pending; st.m_PendingScroll = pending - float(wholeDelta); }
        else { consumed = float(-out.scrollDelta); st.m_PendingScroll = 0; }
        stagIndices = out.fi;
        stagOffsets = out.fo;
        stagVisibleOffsets.clear();
        for (int i : visible) stagVisibleOffsets[i] = byIndex[i]->main;

        AnimFrame af = AnimBegin(visible);
        std::vector<LayoutNode*> nowActive;
        std::vector<const Slot*> used;
        auto use = [&](int i) { Slot& sl = slots[KeyOf(i).value]; nowActive.push_back(sl.holder.get()); used.push_back(&sl); };
        for (int i : visible) use(i);
        for (int i : extra) use(i);
        for (auto& [k, a] : af.disappearing) { Slot& sl = slots[k]; nowActive.push_back(sl.holder.get()); used.push_back(&sl); }
        for (auto& m : af.movingAway) { Slot& sl = Compose(m.index, d); nowActive.push_back(sl.holder.get()); used.push_back(&sl); }
        self.SetChildrenDuringMeasure(nowActive);
        for (auto it = slots.begin(); it != slots.end();) {
            if (std::find(used.begin(), used.end(), &it->second) == used.end()) { DisposeSlot(it->first, it->second); it = slots.erase(it); }
            else ++it;
        }

        // calculateFirstVisibleIndex / Offset.
        int first = std::numeric_limits<int>::max();
        for (int i : out.fi) { if (i <= 0) { first = 0; break; } first = std::min(first, i); }
        if (first == std::numeric_limits<int>::max()) first = 0;
        int firstOffset = std::numeric_limits<int>::max();
        for (int l = 0; l < lanes; ++l) if (out.fi[std::size_t(l)] == first) firstOffset = std::min(firstOffset, out.fo[std::size_t(l)]);
        if (firstOffset == std::numeric_limits<int>::max()) firstOffset = 0;
        if ((st.m_Index.Peek() != first || st.m_Offset.Peek() != firstOffset) && self.GetOwner()) self.GetOwner()->InvalidateSemantics();
        st.m_Index.Set(first);
        st.m_Offset.Set(firstOffset);
        st.m_FirstKey = KeyOf(std::min(first, count - 1));
        st.m_HasFirstKey = true;
        st.m_LastConsumed = consumed;
        st.m_CanForward = std::any_of(out.co.begin(), out.co.end(), [&](int v) { return v > viewport; }) ||
                          std::all_of(out.ci.begin(), out.ci.end(), [&](int v) { return v < count - 1; });
        st.m_Lines.clear();
        for (int i : visible) st.m_Lines.push_back({ i, i, byIndex[i]->main, byIndex[i]->size });
        st.m_SlotsPerLine = lanes;
        st.m_Spacing = spacing;
        st.m_TotalItems = count;
        st.m_ViewportLength = float(viewport);
        st.m_Density = s.density;
        st.OnMeasured();

        if (stagState) {
            LazyGridLayoutInfo info;
            for (int i : visible) {
                const SItem& it = *byIndex[i];
                LazyGridItemInfo ii;
                ii.index = i;
                ii.key = UserKey(i);
                ii.offset = vertical ? IntOffset{ it.cross, it.main } : IntOffset{ it.main, it.cross };
                ii.size = { it.p->Width(), it.p->Height() };
                ii.row = -1;
                ii.column = it.span != 1 ? 0 : it.lane;
                info.visibleItemsInfo.push_back(ii);
            }
            info.viewportStartOffset = -before;
            info.viewportEndOffset = mainMax - before;
            info.totalItemsCount = count;
            info.viewportSize = { w, h };
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.beforeContentPadding = before;
            info.afterContentPadding = after;
            info.mainAxisItemSpacing = spacing;
            info.reverseLayout = reverse;
            info.maxSpan = lanes;
            if (!(stagState->m_Info.Peek() == info)) stagState->m_Info.Set(std::move(info));
        }

        // animateItem: the same item animator as lists and grids.
        std::vector<std::pair<Placeable*, std::shared_ptr<ItemAnim>>> fading;
        auto at = [&](const SItem& it) {
            const int cross = crossBefore + it.cross, main = it.main + before;
            return vertical ? IntOffset{ cross, main } : IntOffset{ main, cross };
        };
        if (af.animating) {
            // Only the requested scroll moves old targets (not a clamp at the edges).
            const int got = int(std::lround(consumed));
            const int applied = wholeDelta >= 0 ? std::clamp(got, 0, wholeDelta) : std::clamp(got, wholeDelta, 0);
            const IntOffset shift = vertical ? IntOffset{ 0, -applied } : IntOffset{ -applied, 0 };
            std::vector<AnimItem> items;
            for (int i : visible) { const SItem& it = *byIndex[i]; items.push_back({ i, at(it), it.size, it.span != 1 ? 0 : it.lane, it.constraints }); }
            AnimPlace(self, af, items, shift, mainMax, false, spacing, first, fading);
        }
        hadLayout = true;
        auto animOf = [&](int i) -> std::shared_ptr<ItemAnim> {
            if (!af.animating) return nullptr;
            auto it = anims.find(KeyOf(i).value);
            return it != anims.end() && it->second->has ? it->second : nullptr;
        };
        struct Placed { Placeable* p; IntOffset at; std::shared_ptr<ItemAnim> anim; bool relative; };
        std::vector<Placed> placed;
        for (int i : visible) placed.push_back({ byIndex[i]->p, at(*byIndex[i]), animOf(i), true });
        for (int i : extra) placed.push_back({ byIndex[i]->p, at(*byIndex[i]), nullptr, true });
        for (auto& [p, a] : fading) placed.push_back({ p, a->target, a, false });   // leaving, where they were
        return s.Layout(w, h, [placed](PlacementScope& place) {
            for (const auto& pl : placed) {
                const IntOffset dd = pl.anim ? pl.anim->delta.Get() : IntOffset{};
                if (pl.relative && !dd.x && !dd.y) place.PlaceRelative(*pl.p, pl.at.x, pl.at.y);
                else place.Place(*pl.p, pl.at.x + dd.x, pl.at.y + dd.y);
            }
        });
    }

    LazyCore::AnimFrame LazyCore::AnimBegin(const std::vector<int>& placedIndices) {
        AnimFrame f;
        f.animating = std::any_of(anims.begin(), anims.end(), [](const auto& e) { return e.second->has; });
        if (!f.animating) return f;
        if (keyIndexVersion != version) {
            prevKeyIndex = std::move(keyIndex);
            keyIndex.clear();
            for (int i = 0; i < count; ++i) keyIndex[KeyOf(i).value] = i;
            keyIndexVersion = version;
        } else {
            prevKeyIndex = keyIndex;
        }
        for (int i : placedIndices) f.placedKeys.insert(KeyOf(i).value);
        for (auto& [k, a] : anims)
            if (a->has && a->targetSet && !f.placedKeys.count(k) && !keyIndex.count(k) && a->specs.fadeOut && !a->disappeared && slots.count(k))
                f.disappearing.push_back({ k, a });
        // Moved out of view but still in the data: keep it (recomposed at its new index)
        // while it slides to the edge (movingAwayToStart / ToEnd).
        for (auto& [k, a] : anims) {
            if (!a->has || !a->targetSet || f.placedKeys.count(k) || !slots.count(k)) continue;
            auto ni = keyIndex.find(k);
            if (ni == keyIndex.end()) continue;
            auto pi = prevKeyIndex.find(k);
            if (!a->placing && pi != prevKeyIndex.end() && pi->second == ni->second) continue;
            f.movingAway.push_back({ k, a, ni->second });
        }
        return f;
    }

    void LazyCore::AnimPlace(LayoutNode& self, AnimFrame& f, const std::vector<AnimItem>& items, IntOffset shift, int mainMax, bool reverse,
                             int spacing, int firstIndex, std::vector<std::pair<Placeable*, std::shared_ptr<ItemAnim>>>& fading) {
        struct MovingIn { ItemAnim* anim; IntOffset cur; int size; int lane; bool fromStart; int prevIndex; };
        std::vector<MovingIn> movingIn;
        for (const AnimItem& item : items) {
            const std::uint64_t key = KeyOf(item.index).value;
            auto it = anims.find(key);
            if (it == anims.end() || !it->second->has) continue;
            ItemAnim& a = *it->second;
            const IntOffset cur = item.cur;
            a.constraints = item.constraints;
            a.mainSize = item.size;
            a.movingAway = false;
            if (a.disappearing) {   // back in the data before it was gone
                a.disappearing = a.disappeared = false;
                a.fadeJob.Cancel();
                a.fade.SnapTo(1.0f);
                a.alpha.Set(1.0f);
            }
            if (!a.targetSet) {
                a.targetSet = true;
                a.target = cur;
                const auto prev = prevKeyIndex.find(key);
                if (hadLayout && prev == prevKeyIndex.end()) {
                    a.FadeIn();
                } else if (hadLayout && prev->second != item.index) {
                    movingIn.push_back({ &a, cur, item.size, item.lane, prev->second < prevFirstIndex, prev->second });
                }
            } else {
                a.target = { a.target.x + shift.x, a.target.y + shift.y };
                if (a.target != cur) a.AnimateDelta({ cur.x - a.target.x, cur.y - a.target.y });
                a.target = cur;
            }
        }
        // Items that came from beyond an edge start just past it, stacked in their old order
        // (movingInFromStartBound / EndBound), so they arrive as a block.
        const int startBound = reverse ? mainMax : 0, endBound = reverse ? 0 : mainMax;
        const int dirSign = reverse ? -1 : 1;   // the screen direction of the content
        auto slideIn = [&](bool fromStart) {
            std::vector<MovingIn*> list;
            for (auto& m : movingIn) if (m.fromStart == fromStart) list.push_back(&m);
            std::sort(list.begin(), list.end(), [&](MovingIn* x, MovingIn* y) { return fromStart ? x->prevIndex > y->prevIndex : x->prevIndex < y->prevIndex; });
            std::map<int, int> acc;
            for (MovingIn* m : list) {
                acc[m->lane] += m->size + spacing;
                const int a2 = acc[m->lane];
                // Leading edge (in content direction) of the start position.
                int lead = fromStart ? startBound - dirSign * a2 : endBound + dirSign * (a2 - m->size - spacing);
                const int screenTop = reverse ? lead - m->size : lead;
                const IntOffset start = vertical ? IntOffset{ m->cur.x, screenTop } : IntOffset{ screenTop, m->cur.y };
                m->anim->AnimateDelta({ m->cur.x - start.x, m->cur.y - start.y });
            }
        };
        slideIn(true);
        slideIn(false);
        for (auto& [k, a] : f.disappearing) {
            a->target = { a->target.x + shift.x, a->target.y + shift.y };
            if (!a->disappearing) {
                LayoutNode* node = &self;
                a->onDisappeared = [node, alive = self.AliveToken()] { if (!alive.expired()) node->RequestRemeasure(); };
                a->FadeOut();
            }
            fading.push_back({ &slots[k].holder->AsMeasurable().Measure(a->constraints), a });
        }
        // Moving away: placed just past the edge they went beyond, stacked in their new order,
        // and animated there from where they were.
        {
            using Away = AnimFrame::Away;
            std::vector<Away*> toStart, toEnd;
            for (auto& m : f.movingAway) (m.index < firstIndex ? toStart : toEnd).push_back(&m);
            std::sort(toStart.begin(), toStart.end(), [](Away* x, Away* y) { return x->index > y->index; });
            std::sort(toEnd.begin(), toEnd.end(), [](Away* x, Away* y) { return x->index < y->index; });
            auto place = [&](std::vector<Away*>& list, bool toStartEdge) {
                int acc = 0;
                for (Away* m : list) {
                    ItemAnim& a = *m->anim;
                    Placeable& p = slots[m->key].holder->AsMeasurable().Measure(a.constraints);
                    const int size = vertical ? p.Height() : p.Width();
                    acc += size + spacing;
                    const int lead = toStartEdge ? startBound - dirSign * acc : endBound + dirSign * (acc - size - spacing);
                    const int screenTop = reverse ? lead - size : lead;
                    const IntOffset old{ a.target.x + shift.x, a.target.y + shift.y };
                    const IntOffset dest = vertical ? IntOffset{ old.x, screenTop } : IntOffset{ screenTop, old.y };
                    a.target = old;
                    a.movingAway = true;
                    LayoutNode* node = &self;
                    a.onDisappeared = [node, alive = self.AliveToken()] { if (!alive.expired()) node->RequestRemeasure(); };
                    if (dest != old) a.AnimateDelta({ dest.x - old.x, dest.y - old.y });
                    a.target = dest;
                    fading.push_back({ &p, m->anim });
                }
            };
            place(toStart, true);
            place(toEnd, false);
        }
        // Items still in the data, out of view and done moving, forget their place (they come
        // back from an edge).
        for (auto& [k, a] : anims)
            if (a->has && a->targetSet && !f.placedKeys.count(k) && keyIndex.count(k) &&
                std::none_of(f.movingAway.begin(), f.movingAway.end(), [&](const AnimFrame::Away& m) { return m.key == k; })) {
                a->targetSet = false;
                a->movingAway = false;
            }
        prevFirstIndex = firstIndex;
    }

    MeasureResult LazyCore::Measure(LayoutNode& self, MeasureScope& s, const Constraints& c) {
        if (staggered) return MeasureStaggered(self, s, c);
        LazyStateBase& st = *state;
        st.m_Node = &self;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        const int mainMax = vertical ? c.maxHeight : c.maxWidth;
        const int crossMax = vertical ? c.maxWidth : c.maxHeight;
        // before / after: the padding at the scroll start / end (swapped by reverseLayout).
        const int padStart = s.RoundToPx(vertical ? contentPadding.top : contentPadding.start);
        const int padEnd = s.RoundToPx(vertical ? contentPadding.bottom : contentPadding.end);
        const int before = reverse ? padEnd : padStart;
        const int after = reverse ? padStart : padEnd;
        const int crossBefore = s.RoundToPx(vertical ? contentPadding.start : contentPadding.top);
        const int crossPad = crossBefore + s.RoundToPx(vertical ? contentPadding.end : contentPadding.bottom);
        const int spacing = s.RoundToPx(mainSpacing);
        const int crossAvailable = std::max(0, crossMax - crossPad);
        {
            // containerConstraints.offset(-padding): Infinity stays, others shrink to no less than 0.
            const int padH = s.RoundToPx(contentPadding.start) + s.RoundToPx(contentPadding.end);
            const int padV = s.RoundToPx(contentPadding.top) + s.RoundToPx(contentPadding.bottom);
            const auto shrink = [](int max, int pad) { return max == Constraints::Infinity ? max : std::max(0, max - pad); };
            const IntSize maxSize{ shrink(c.maxWidth, padH), shrink(c.maxHeight, padV) };
            if (!(parentMaxSize.Peek() == maxSize)) parentMaxSize.Set(maxSize);
        }

        // Grid slots: sizes and positions across (LazyGridSlots).
        std::vector<int> slotSizes{ crossAvailable }, slotPositions{ 0 };
        if (grid) {
            const int crossSpacing = s.RoundToPx(crossArrangement.Spacing());
            slotSizes = cells.CellSizes(s, crossAvailable, crossSpacing);
            slotPositions.assign(slotSizes.size(), 0);
            crossArrangement.Arrange(s, crossAvailable, slotSizes, LayoutDirection::Ltr, slotPositions);
        }
        slotsPerLine = int(slotSizes.size());
        UpdateLines();
        // Pager pages: a fixed main-axis size (PageSize.Fill: the viewport minus padding).
        const int pagePx = pager && pageSize ? std::max(0, pageSize(s, std::max(0, mainMax - before - after), spacing)) : 0;
        st.m_PageSize = pagePx;
        st.m_PageSnap = pager && pageSnap ? pageSnap(mainMax, pagePx, before, after) : 0;
        const int crossSpacingPx = grid ? s.RoundToPx(crossArrangement.Spacing()) : 0;

        // One measured line: its cells and main-axis size.
        struct Cell { int index; Slot* slot; Placeable* p; int crossPos; int column; Constraints constraints; };
        struct MLine { int line, first, last; std::vector<Cell> cells; int size; };
        // Slots composed earlier stay attached until the final children are set: detaching one
        // mid-measure would detach its content (a focused item would lose its pin).
        std::vector<LayoutNode*> active;
        for (auto& [k, sl] : slots) if (sl.holder) active.push_back(sl.holder.get());
        auto measureLine = [&](int line) -> MLine {
            MLine ml{ line, LineFirst(line), LineEnd(line) - 1, {}, 0 };
            int slot = 0;
            for (int i = ml.first; i <= ml.last; ++i) {
                const int span = std::min(SpanOf(i, slotsPerLine - slot), slotsPerLine - slot);
                Slot& sl = Compose(i, d);
                if (std::find(active.begin(), active.end(), sl.holder.get()) == active.end()) active.push_back(sl.holder.get());
                self.SetChildrenDuringMeasure(active);   // attached before measuring (to get an owner)
                Constraints ic;
                int crossPos = -1;
                if (grid) {
                    int w = 0;
                    for (int k = slot; k < slot + span; ++k) w += slotSizes[std::size_t(k)];
                    w += crossSpacingPx * (span - 1);
                    ic = vertical ? Constraints{ w, w, 0, Constraints::Infinity } : Constraints{ 0, Constraints::Infinity, w, w };
                    crossPos = slotPositions[std::size_t(slot)];
                } else if (pager) {
                    ic = vertical ? Constraints{ 0, crossAvailable, pagePx, pagePx }
                                  : Constraints{ pagePx, pagePx, 0, crossAvailable };
                } else {
                    ic = vertical ? Constraints{ 0, crossAvailable, 0, Constraints::Infinity }
                                  : Constraints{ 0, Constraints::Infinity, 0, crossAvailable };
                }
                Placeable& p = sl.holder->AsMeasurable().Measure(ic);
                ml.cells.push_back({ i, &sl, &p, crossPos, slot, ic });
                ml.size = std::max(ml.size, vertical ? p.Height() : p.Width());
                slot += span;
            }
            return ml;
        };

        auto publishEmpty = [&] {
            const LazyStateBase::MeasuredAxis axis{ before, after, mainMax, pagePx, spacing, reverse };
            if (!(st.m_Axis.Peek() == axis)) st.m_Axis.Set(axis);
            if (listState) {
                LazyListLayoutInfo info;
                info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
                info.viewportSize = { c.minWidth, c.minHeight };
                if (!(listState->m_Info.Peek() == info)) listState->m_Info.Set(std::move(info));
            } else if (gridState) {
                LazyGridLayoutInfo info;
                info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
                info.viewportSize = { c.minWidth, c.minHeight };
                if (!(gridState->m_Info.Peek() == info)) gridState->m_Info.Set(std::move(info));
            }
        };

        // Key-stable position: after a data change, find the first item by key.
        int index = st.m_Index.Peek();
        if (st.m_HasFirstKey) {
            const int found = IndexOfKey(st.m_FirstKey, index);
            if (found >= 0) index = found;
        }
        if (pager && st.m_PendingFraction) {
            // scrollToPage(page, fraction): fractions before the page start belong to the previous page.
            // The page rests at the snap position, `f` pages past it; earlier pages fill the gap.
            const float f = *st.m_PendingFraction;
            st.m_PendingFraction.reset();
            int off = int(std::lround(f * float(pagePx + spacing))) - st.m_PageSnap;
            while (off < 0 && index > 0 && pagePx + spacing > 0) { --index; off += pagePx + spacing; }
            st.m_Index.Set(index);
            st.m_Offset.Set(std::max(0, off));
        }
        if (count == 0) {
            for (auto& [k, slot] : slots) DisposeSlot(k, slot);
            slots.clear();
            self.SetChildrenDuringMeasure({});
            st.m_LastConsumed = st.m_PendingScroll = 0;
            st.m_Lines.clear();
            st.m_TotalItems = 0;
            publishEmpty();
            st.OnMeasured();
            return s.Layout(c.minWidth, c.minHeight);
        }
        index = std::clamp(index, 0, count - 1);
        int line = LineOf(index);
        const int lineCount = LineCount();
        // Whole pixels are applied; the fraction waits for the next scroll (scrollToBeConsumed),
        // so many small deltas add up exactly.
        const float delta = st.m_PendingScroll;
        const int wholeDelta = RoundJava(delta);
        float remainder = delta - float(wholeDelta);
        st.m_PendingScroll = 0;
        // pos: where the first line's leading edge sits relative to the content start.
        int pos = -st.m_Offset.Peek() - wholeDelta;

        std::vector<MLine> lines;
        // Scrolled backward past the first line: measure earlier lines.
        while (pos > 0 && line > 0) {
            --line;
            pos -= measureLine(line).size + spacing;
        }
        float consumed = delta;
        if (pos > 0) { consumed += float(pos) - remainder; remainder = 0; pos = 0; }   // reached the very start

        // Skip lines fully above the viewport, then fill forward.
        const int viewport = mainMax - before - after;
        for (;;) {
            MLine ml = measureLine(line);
            if (pos + ml.size + spacing <= 0 && line < lineCount - 1) { pos += ml.size + spacing; ++line; continue; }
            lines.push_back(std::move(ml));
            break;
        }
        int end = pos + lines.back().size;
        while (end < viewport + after && lines.back().line < lineCount - 1) {
            lines.push_back(measureLine(lines.back().line + 1));
            end += spacing + lines.back().size;
        }
        // Content ends before the viewport does: pull back (measuring earlier lines) and give the
        // unconsumed part back.
        if (end < viewport && lines.back().line == lineCount - 1) {
            int gap = viewport - end;
            while (gap > 0) {
                if (pos + gap <= 0) { pos += gap; consumed -= float(gap) + remainder; remainder = 0; gap = 0; break; }
                gap -= -pos; consumed -= float(-pos) + remainder; remainder = 0; pos = 0;
                if (lines.front().line == 0) break;
                lines.insert(lines.begin(), measureLine(lines.front().line - 1));
                pos -= lines.front().size + spacing;
            }
        }
        // Drop lines that ended up above the viewport after the pull-back.
        while (lines.size() > 1 && pos + lines.front().size + spacing <= 0) {
            pos += lines.front().size + spacing;
            lines.erase(lines.begin());
        }

        const int firstIndex = lines.front().first, lastIndex = lines.back().last;
        const int lastLine = lines.back().line;
        std::vector<int> offsets;
        {
            int p = pos;
            for (auto& l : lines) { offsets.push_back(p); p += l.size + spacing; }
        }
        st.m_Lines.clear();
        for (std::size_t i = 0; i < lines.size(); ++i) st.m_Lines.push_back({ lines[i].first, lines[i].last, offsets[i], lines[i].size });

        // Sticky header (StickToTopPlacement): the last header at or before the first visible
        // item, at the viewport start, pushed back by the next visible header; drawn on top.
        std::optional<MLine> sticking;
        int stickingOffset = 0;
        if (const int header = stickyHeadersEnabled ? StickyAtOrBefore(firstIndex) : -1; header >= 0) {
            const int headerLine = LineOf(header);
            int nextHeader = kNone;
            for (std::size_t i = 0; i < lines.size(); ++i)
                if (lines[i].line != headerLine && IsSticky(lines[i].first)) { nextHeader = offsets[i]; break; }
            int itemOffset = kNone;
            for (std::size_t i = 0; i < lines.size(); ++i)
                if (lines[i].line == headerLine) {
                    sticking = std::move(lines[i]);
                    itemOffset = offsets[i];
                    lines.erase(lines.begin() + std::ptrdiff_t(i));
                    offsets.erase(offsets.begin() + std::ptrdiff_t(i));
                    break;
                }
            if (!sticking) sticking = measureLine(headerLine);
            int off = itemOffset == kNone ? -before : std::max(-before, itemOffset);
            if (nextHeader != kNone) off = std::min(off, nextHeader - (sticking->size + spacing));
            stickingOffset = off;
        }
        stickingSize = sticking ? sticking->size : 0;

        // Lines beyond the viewport that are composed, measured and placed past its edges but
        // are not visible (createItemsBeforeList / AfterList): the pager's beyondViewportPageCount,
        // then pinned items further away (stacked after those).
        std::vector<MLine> extraBefore, extraAfter;
        if (!lines.empty()) {
            const int firstL = lines.front().line, lastL = lines.back().line;
            const int n = pager ? std::min(beyondCount, lineCount) : 0;
            const int startL = std::max(0, firstL - n), endL = std::min(lineCount - 1, lastL + n);
            for (int l = firstL - 1; l >= startL; --l) extraBefore.push_back(measureLine(l));
            for (int l = lastL + 1; l <= endL; ++l) extraAfter.push_back(measureLine(l));
            std::set<int> before_, after_;
            if (beyondInterval)
                for (int idx = std::max(0, beyondInterval->first); idx <= std::min(count - 1, beyondInterval->second); ++idx) {
                    const int l = LineOf(idx);
                    if (l < startL) before_.insert(l);
                    else if (l > endL) after_.insert(l);
                }
            for (const auto& [kv, key] : pinned) {
                auto sl = slots.find(kv);
                const int idx = IndexOfKey(key, sl != slots.end() ? sl->second.index : -1);
                if (idx < 0) continue;
                const int l = LineOf(idx);
                if (sticking && sticking->line == l) continue;
                if (l < startL) before_.insert(l);
                else if (l > endL) after_.insert(l);
            }
            for (auto it = before_.rbegin(); it != before_.rend(); ++it) extraBefore.push_back(measureLine(*it));
            for (int l : after_) extraAfter.push_back(measureLine(l));
        }

        // Item animations: which keys are in the data now and before, and removed items still
        // fading out (their slots stay, frozen, where they were).
        std::vector<int> placedIndices;
        for (auto& l : lines) for (auto& cell : l.cells) placedIndices.push_back(cell.index);
        if (sticking) for (auto& cell : sticking->cells) placedIndices.push_back(cell.index);
        AnimFrame af = AnimBegin(placedIndices);
        const bool animating = af.animating;
        auto& disappearing = af.disappearing;
        auto& movingAway = af.movingAway;

        // Dispose slots not in use (no reuse pool yet).
        std::vector<LayoutNode*> nowActive;
        std::vector<const Slot*> used;
        for (auto& l : lines) for (auto& cell : l.cells) { nowActive.push_back(cell.slot->holder.get()); used.push_back(cell.slot); }
        if (sticking) for (auto& cell : sticking->cells) { nowActive.push_back(cell.slot->holder.get()); used.push_back(cell.slot); }
        for (auto* extra : { &extraBefore, &extraAfter })
            for (auto& l : *extra) for (auto& cell : l.cells) { nowActive.push_back(cell.slot->holder.get()); used.push_back(cell.slot); }
        for (auto& [k, a] : disappearing) { Slot& sl = slots[k]; nowActive.push_back(sl.holder.get()); used.push_back(&sl); }
        for (auto& m : movingAway) { Slot& sl = Compose(m.index, d); nowActive.push_back(sl.holder.get()); used.push_back(&sl); }
        self.SetChildrenDuringMeasure(nowActive);
        for (auto it = slots.begin(); it != slots.end();) {
            if (std::find(used.begin(), used.end(), &it->second) == used.end()) { DisposeSlot(it->first, it->second); it = slots.erase(it); }
            else ++it;
        }
        for (auto it = anims.begin(); it != anims.end();) {
            if (!slots.count(it->first) && !it->second->Busy()) it = anims.erase(it);
            else ++it;
        }

        if (st.m_Index.Peek() != firstIndex || st.m_Offset.Peek() != -pos)
            if (self.GetOwner()) self.GetOwner()->InvalidateSemantics();   // bounds moved
        st.m_Index.Set(firstIndex);
        st.m_Offset.Set(-pos);
        st.m_FirstKey = KeyOf(firstIndex);
        st.m_HasFirstKey = true;
        st.m_LastConsumed = consumed;
        st.m_PendingScroll = remainder;
        st.m_CanForward = !(lastLine == lineCount - 1 && end <= viewport);
        st.m_Density = s.density;
        st.m_SlotsPerLine = slotsPerLine;
        st.m_Spacing = spacing;
        st.m_TotalItems = count;
        st.m_ViewportLength = float(viewport);
        (void)lastIndex;
        {
            const LazyStateBase::MeasuredAxis axis{ before, after, mainMax, pagePx, spacing, reverse };
            if (!(st.m_Axis.Peek() == axis)) st.m_Axis.Set(axis);
        }
        st.OnMeasured();

        int cross = 0;
        if (grid) {
            cross = crossMax == Constraints::Infinity ? crossPad : crossMax;
        } else {
            auto crossOf = [&](const MLine& l) { for (auto& cell : l.cells) cross = std::max(cross, (vertical ? cell.p->Width() : cell.p->Height()) + crossPad); };
            for (auto& l : lines) crossOf(l);
            if (sticking) crossOf(*sticking);
            for (auto& l : extraBefore) crossOf(l);
            for (auto& l : extraAfter) crossOf(l);
        }
        const int w = vertical ? c.ConstrainWidth(cross) : c.ConstrainWidth(mainMax);
        const int h = vertical ? c.ConstrainHeight(mainMax) : c.ConstrainHeight(cross);

        // Main-axis screen positions: content offsets after the start padding, mirrored for
        // reverseLayout; content shorter than the viewport follows the arrangement
        // (calculateItemsOffsets).
        std::vector<int> screen(lines.size());
        const bool spare = mainArrangement && !sticking && !lines.empty() && lines.front().line == 0 &&
                           lines.back().line == lineCount - 1 && pos == 0 && end < viewport;
        if (spare) {
            const std::size_t n = lines.size();
            std::vector<int> sizes(n), arranged(n);
            for (std::size_t i = 0; i < n; ++i) sizes[i] = lines[reverse ? n - 1 - i : i].size;
            mainArrangement->Arrange(s, viewport, sizes, LayoutDirection::Ltr, arranged);
            for (std::size_t i = 0; i < n; ++i) screen[reverse ? n - 1 - i : i] = padStart + arranged[i];
        } else {
            for (std::size_t i = 0; i < lines.size(); ++i)
                screen[i] = reverse ? mainMax - (offsets[i] + before) - lines[i].size : offsets[i] + before;
        }
        const int stickingScreen = !sticking ? 0 : reverse ? mainMax - (stickingOffset + before) - sticking->size : stickingOffset + before;

        // LazyLayoutItemAnimator.onMeasured: scrolling moves old targets along; a changed target
        // animates the difference; new items fade in; items that came from beyond the viewport
        // slide in from that edge; removed ones fade out where they were.
        const AlignmentH ha0 = horizontalAlignment;
        const AlignmentV va0 = verticalAlignment;
        auto layoutPos = [&](const Cell& cell, int mainScreen) {
            const int cross = cell.crossPos >= 0 ? crossBefore + cell.crossPos
                            : crossBefore + (vertical ? ha0.Align(cell.p->Width(), w - crossPad, s.Direction())
                                                      : va0.Align(cell.p->Height(), h - crossPad));
            return vertical ? IntOffset{ cross, mainScreen } : IntOffset{ mainScreen, cross };
        };
        std::vector<std::pair<Placeable*, std::shared_ptr<ItemAnim>>> fading;   // placed after the rest
        if (animating) {
            // Only the requested scroll moves old targets (not a pull-back after a data change).
            const int consumedPx = RoundJava(consumed - remainder);
            const int applied = wholeDelta >= 0 ? std::clamp(consumedPx, 0, wholeDelta) : std::clamp(consumedPx, wholeDelta, 0);
            const int mainShift = reverse ? applied : -applied;
            const IntOffset shift = vertical ? IntOffset{ 0, mainShift } : IntOffset{ mainShift, 0 };
            std::vector<AnimItem> items;
            for (std::size_t i = 0; i < lines.size(); ++i)
                for (const auto& cell : lines[i].cells) items.push_back({ cell.index, layoutPos(cell, screen[i]), lines[i].size, cell.column, cell.constraints });
            AnimPlace(self, af, items, shift, mainMax, reverse, spacing, firstIndex, fading);
        }
        hadLayout = true;

        // Layout info: offsets along the scroll direction from the content start (arranged
        // and reverse-aware, like Compose's).
        auto infoOffset = [&](int scr, int size) { return reverse ? mainMax - scr - size - before : scr - before; };
        if (listState) {
            LazyListLayoutInfo info;
            // The sticking header precedes the items it covers (its index is lower).
            if (sticking) info.visibleItemsInfo.push_back({ sticking->first, UserKey(sticking->first), infoOffset(stickingScreen, sticking->size), sticking->size });
            for (std::size_t i = 0; i < lines.size(); ++i)
                info.visibleItemsInfo.push_back({ lines[i].first, UserKey(lines[i].first), infoOffset(screen[i], lines[i].size), lines[i].size });
            info.viewportStartOffset = -before;
            info.viewportEndOffset = mainMax - before;
            info.totalItemsCount = count;
            info.viewportSize = { w, h };
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.beforeContentPadding = before;
            info.afterContentPadding = after;
            info.mainAxisItemSpacing = spacing;
            info.reverseLayout = reverse;
            if (!(listState->m_Info.Peek() == info)) listState->m_Info.Set(std::move(info));
        } else if (gridState) {
            LazyGridLayoutInfo info;
            auto add = [&](const MLine& l, int off) {
                for (const auto& cell : l.cells) {
                    LazyGridItemInfo it;
                    it.index = cell.index;
                    it.key = UserKey(cell.index);
                    it.offset = vertical ? IntOffset{ cell.crossPos, off } : IntOffset{ off, cell.crossPos };
                    it.size = { cell.p->Width(), cell.p->Height() };
                    it.row = vertical ? l.line : cell.column;
                    it.column = vertical ? cell.column : l.line;
                    info.visibleItemsInfo.push_back(it);
                }
            };
            if (sticking) add(*sticking, infoOffset(stickingScreen, sticking->size));
            for (std::size_t i = 0; i < lines.size(); ++i) add(lines[i], infoOffset(screen[i], lines[i].size));
            info.viewportStartOffset = -before;
            info.viewportEndOffset = mainMax - before;
            info.totalItemsCount = count;
            info.viewportSize = { w, h };
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.beforeContentPadding = before;
            info.afterContentPadding = after;
            info.mainAxisItemSpacing = spacing;
            info.reverseLayout = reverse;
            info.maxSpan = slotsPerLine;
            if (!(gridState->m_Info.Peek() == info)) gridState->m_Info.Set(std::move(info));
        }

        // Placement, in drawing order (the sticking header last, on top).
        struct Placed { Placeable* p; int main; int cross; std::shared_ptr<ItemAnim> anim; IntOffset at; };   // cross -1: aligned (list)
        std::vector<Placed> placed;
        auto animOf = [&](int index) -> std::shared_ptr<ItemAnim> {
            if (!animating) return nullptr;
            auto it = anims.find(KeyOf(index).value);
            return it != anims.end() && it->second->has ? it->second : nullptr;
        };
        for (std::size_t i = 0; i < lines.size(); ++i)
            for (const auto& cell : lines[i].cells) placed.push_back({ cell.p, screen[i], cell.crossPos, animOf(cell.index), {} });
        if ((!extraBefore.empty() || !extraAfter.empty()) && !offsets.empty()) {
            auto toScreen = [&](int off, int size) { return reverse ? mainMax - (off + before) - size : off + before; };
            int acc = offsets.front();
            for (const auto& l : extraBefore) {
                acc -= l.size + spacing;
                for (const auto& cell : l.cells) placed.push_back({ cell.p, toScreen(acc, l.size), cell.crossPos, nullptr, {} });
            }
            acc = offsets.back() + lines.back().size + spacing;
            for (const auto& l : extraAfter) {
                for (const auto& cell : l.cells) placed.push_back({ cell.p, toScreen(acc, l.size), cell.crossPos, nullptr, {} });
                acc += l.size + spacing;
            }
        }
        for (auto& [p, a] : fading) placed.push_back({ p, kNone, kNone, a, a->target });   // removed, fading where they were
        if (sticking) for (const auto& cell : sticking->cells) placed.push_back({ cell.p, stickingScreen, cell.crossPos, nullptr, {} });
        const AlignmentH ha = horizontalAlignment;
        const AlignmentV va = verticalAlignment;
        const bool vert = vertical;
        const LayoutDirection dir = s.Direction();
        return s.Layout(w, h, [placed, crossBefore, crossPad, w, h, ha, va, vert, dir](PlacementScope& place) {
            for (const auto& pl : placed) {
                // placementDelta, read here so its animation re-places without remeasuring.
                const IntOffset d = pl.anim ? pl.anim->delta.Get() : IntOffset{};
                if (pl.main == kNone) { place.Place(*pl.p, pl.at.x + d.x, pl.at.y + d.y); continue; }
                if (d.x || d.y) {
                    // Animated cells: their layout position (as targeted) plus the delta.
                    int x, y;
                    if (vert) { x = pl.cross >= 0 ? crossBefore + pl.cross : crossBefore + ha.Align(pl.p->Width(), w - crossPad, dir); y = pl.main; }
                    else { x = pl.main; y = crossBefore + (pl.cross >= 0 ? pl.cross : va.Align(pl.p->Height(), h - crossPad)); }
                    place.Place(*pl.p, x + d.x, y + d.y);
                    continue;
                }
                const int m = pl.main;
                if (vert) {
                    if (pl.cross >= 0) place.PlaceRelative(*pl.p, crossBefore + pl.cross, m);
                    else place.Place(*pl.p, crossBefore + ha.Align(pl.p->Width(), w - crossPad, dir), m);
                } else {
                    const int y = pl.cross >= 0 ? pl.cross : va.Align(pl.p->Height(), h - crossPad);
                    place.PlaceRelative(*pl.p, m, crossBefore + y);
                }
            }
        });
    }

    namespace {
        struct LazyPolicy final : MeasurePolicy {
            std::shared_ptr<LazyCore> core;
            LayoutNode* node = nullptr;
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                return core->Measure(*node, s, c);
            }
        };

        // The composable part shared by lists and grids; `configure` fills the core.
        // StickyHeaderBringIntoViewSpec: the area under a sticking header does not count as visible.
        class StickyHeaderSpec final : public BringIntoViewSpec {
        public:
            StickyHeaderSpec(std::weak_ptr<LazyCore> c, bool applyOffset, std::shared_ptr<const BringIntoViewSpec> inner)
                : core(std::move(c)), apply(applyOffset), inner(std::move(inner)) {}
            float CalculateScrollDistance(float offset, float size, float container) const override {
                auto c = core.lock();
                const float sticky = c ? float(c->stickingSize) : 0.0f;
                return inner->CalculateScrollDistance(apply ? offset - sticky : offset, size, container - sticky);
            }
        private:
            std::weak_ptr<LazyCore> core;   // the layout owns the core; the spec must not keep it
            bool apply;
            std::shared_ptr<const BringIntoViewSpec> inner;
        };

        // LazyLayoutBeyondBoundsModifierLocal: one more item at a time past the edge in the search
        // direction, up to two viewports' worth, until the block has an answer.
        struct BeyondParams {
            std::weak_ptr<LazyCore> core;
            bool reverse;
            bool operator==(const BeyondParams& o) const { return !core.owner_before(o.core) && !o.core.owner_before(core) && reverse == o.reverse; }
        };
        struct BeyondBoundsNode final : ModifierNode, ui::BeyondBoundsLayout {
            BeyondParams params;
            explicit BeyondBoundsNode(BeyondParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::BeyondBoundsLayout* AsBeyondBoundsLayout() override { return this; }
            std::optional<bool> Layout(ui::FocusDirection d, const std::function<std::optional<bool>(bool)>& block) override {
                auto core = params.core.lock();
                if (!core || core->count <= 0 || !core->HasVisible() || !Owner()) return block(false);
                using FD = ui::FocusDirection;
                const bool vertical = core->vertical;
                const bool rtl = Owner()->GetLayoutDirection() == LayoutDirection::Rtl;
                const bool reverse = params.reverse;
                bool opposite = false, forward = true;
                switch (d) {
                    case FD::Previous: forward = false; break;
                    case FD::Next: forward = true; break;
                    case FD::Up: forward = reverse; opposite = !vertical; break;
                    case FD::Down: forward = !reverse; opposite = !vertical; break;
                    case FD::Left: forward = rtl ? !reverse : reverse; opposite = vertical; break;
                    case FD::Right: forward = rtl ? reverse : !reverse; opposite = vertical; break;
                    default: return std::nullopt;
                }
                const int start = forward ? core->LastPlaced() : core->FirstPlaced();
                std::pair<int, int> interval{ start, start };
                auto hasMore = [&] { return !opposite && (forward ? interval.second < core->count - 1 : interval.first > 0); };
                // itemsPerViewport: the viewport over the average visible line.
                const int average = core->AverageLineSize();
                const int perViewport = average == 0 ? 1 : std::max(1, int(core->state->ViewportLength()) / average);
                const int maxItems = std::min(2 * perViewport, core->count);
                std::optional<bool> found;
                for (int created = 0; !found && hasMore() && created < maxItems; ++created) {
                    if (forward) ++interval.second; else --interval.first;
                    core->beyondInterval = interval;
                    core->RemeasureNow();
                    found = block(hasMore());
                }
                core->beyondInterval.reset();
                core->RemeasureNow();
                return found;
            }
        };

        void LazyLayout(bool vertical, const Modifier& modifier, LazyStateBase& state, bool userScrollEnabled, rt::Key key,
                        const std::function<void(LazyCore&)>& configure, FlingBehavior fling = {},
                        const std::function<Modifier(Modifier)>& extra = {}, bool reverseLayout = false,
                        OverscrollParam overscroll = std::nullopt,
                        std::shared_ptr<const BringIntoViewSpec> bringIntoView = nullptr) {
            auto& comp = *rt::Composition::Current();
            auto& core = comp.Remember<std::shared_ptr<LazyCore>>([] { return std::make_shared<LazyCore>(); }, {}, key);
            core->vertical = vertical;
            core->context = comp.CurrentScope();
            core->holder = &rt::RememberSaveableStateHolder(key);
            core->state = &state;
            core->parentPin = comp.Read(ui::LocalPinnableContainer);
            configure(*core);
            core->count = 0;
            for (const auto& iv : core->intervals) core->count += iv.count;
            ++core->version;   // item lambdas may capture new values: recompose visible items
            if (const bool sticky = !comp.Read(ui::LocalScrollCaptureInProgress); sticky != core->stickyHeadersEnabled) {
                core->stickyHeadersEnabled = sticky;
                core->RequestRemeasure();
            }
            ScrollableOptions so;
            so.enabled = userScrollEnabled;
            so.reverseDirection = !reverseLayout;   // ScrollableDefaults.reverseDirection
            if (!vertical && comp.Read(ui::LocalLayoutDirection) == LayoutDirection::Rtl) so.reverseDirection = !so.reverseDirection;
            so.overscroll = overscroll ? *overscroll : RememberOverscrollEffect(key);
            so.flingBehavior = std::move(fling);
            // rememberLazyLayoutBringIntoViewSpec: the sticking header covers the start.
            const bool rtl = !vertical && comp.Read(ui::LocalLayoutDirection) == LayoutDirection::Rtl;
            so.bringIntoViewSpec = bringIntoView ? std::move(bringIntoView)
                                 : std::make_shared<StickyHeaderSpec>(core, reverseLayout == rtl, CurrentBringIntoViewSpec());
            // lazyLayoutSemantics: an estimated scroll range (index * 500 + offset, a little more
            // while it can go on), scroll to index, collection info, the viewport length.
            LazyCore* cp = core.get();
            LazyStateBase* sp = &state;
            Modifier sem = Semantics(ClipToBounds(modifier), [cp, sp, vertical, userScrollEnabled](SemanticsConfiguration& c) {
                c.isTraversalGroup = true;
                auto value = [sp] { return float(sp->FirstVisibleItemScrollOffset() + sp->FirstVisibleItemIndex() * 500); };
                ui::ScrollAxisRange range{ value, [sp, value] { return value() + (sp->CanScrollForward() ? 100.0f : 0.0f); }, cp->reverse };
                if (vertical) c.verticalScrollAxisRange = range; else c.horizontalScrollAxisRange = range;
                if (userScrollEnabled) c.scrollToIndex = [sp, cp](int index) {
                    if (index < 0 || index >= cp->count) return false;
                    sp->ScrollToItem(index);
                    return true;
                };
                c.scrollViewportLength = [sp] { return sp->ViewportLength(); };
                if (cp->grid || cp->staggered) c.collectionInfo = std::pair{ -1, -1 };
                else c.collectionInfo = vertical ? std::pair{ cp->count, 1 } : std::pair{ 1, cp->count };
            });
            sem = sem.With<BeyondBoundsNode>(BeyondParams{ core, reverseLayout });
            Modifier m = Scrollable(Overscroll(std::move(sem), so.overscroll), state,
                                    vertical ? Orientation::Vertical : Orientation::Horizontal, so);
            if (extra) m = extra(std::move(m));
            auto c = core;
            comp.Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                                  [&](LayoutNode& n) {
                                      n.SetModifier(m);
                                      TakeLocalLayoutDirection(n);
                                      auto* policy = dynamic_cast<LazyPolicy*>(n.Policy().get());
                                      n.SetMeasureManagedChildren(true);
                                      if (!policy) {
                                          auto p = std::make_shared<LazyPolicy>();
                                          p->core = c;
                                          p->node = &n;
                                          n.SetMeasurePolicy(p);
                                      }
                                      n.RequestRemeasure();
                                  }, {}, key);
        }

        void LazyList(bool vertical, const Modifier& modifier, LazyListState& state, std::function<void(LazyListScope&)> content,
                      LazyListOptions options, rt::Key key) {
            LazyListScope scope;
            content(scope);
            LazyLayout(vertical, modifier, state, options.userScrollEnabled, key, [&](LazyCore& core) {
                core.grid = false;
                core.staggered = false;
                core.listState = &state;
                core.gridState = nullptr;
                core.contentPadding = options.contentPadding;
                core.mainSpacing = options.arrangement.Spacing();
                core.mainArrangement = options.arrangement;
                core.reverse = options.reverseLayout;
                core.horizontalAlignment = options.horizontalAlignment;
                core.verticalAlignment = options.verticalAlignment;
                core.intervals = std::move(scope.intervals);
            }, options.flingBehavior, {}, options.reverseLayout, options.overscrollEffect);
        }

        void LazyGrid(bool vertical, const Modifier& modifier, GridCells cells, LazyGridState& state,
                      std::function<void(LazyGridScope&)> content, LazyGridOptions options, rt::Key key) {
            LazyGridScope scope;
            content(scope);
            LazyLayout(vertical, modifier, state, options.userScrollEnabled, key, [&](LazyCore& core) {
                core.grid = true;
                core.staggered = false;
                core.gridState = &state;
                core.listState = nullptr;
                core.contentPadding = options.contentPadding;
                core.mainSpacing = options.mainAxisArrangement.Spacing();
                core.mainArrangement = options.mainAxisArrangement;
                core.reverse = options.reverseLayout;
                core.crossArrangement = options.crossAxisArrangement;
                core.cells = cells;
                core.intervals = std::move(scope.intervals);
            }, options.flingBehavior, {}, options.reverseLayout, options.overscrollEffect);
        }
    }

    namespace {
        void LazyStaggered(bool vertical, const Modifier& modifier, GridCells cells, LazyStaggeredGridState& state,
                           std::function<void(LazyStaggeredGridScope&)> content, LazyStaggeredGridOptions options, rt::Key key) {
            LazyStaggeredGridScope scope;
            content(scope);
            LazyLayout(vertical, modifier, state, options.userScrollEnabled, key, [&](LazyCore& core) {
                core.grid = false;
                core.pager = false;
                core.staggered = true;
                core.stagState = &state;
                core.listState = nullptr;
                core.gridState = nullptr;
                core.contentPadding = options.contentPadding;
                core.mainSpacing = options.mainAxisItemSpacing;
                core.mainArrangement.reset();
                core.reverse = false;
                core.crossArrangement = options.crossAxisArrangement;
                core.cells = cells;
                core.intervals = std::move(scope.intervals);
            }, {}, {}, false, options.overscrollEffect);
        }
    }

    void LazyVerticalStaggeredGrid(const Modifier& m, GridCells columns, LazyStaggeredGridState& s,
                                   std::function<void(LazyStaggeredGridScope&)> content, LazyStaggeredGridOptions o, rt::Key key) {
        LazyStaggered(true, m, columns, s, std::move(content), o, key);
    }
    void LazyHorizontalStaggeredGrid(const Modifier& m, GridCells rows, LazyStaggeredGridState& s,
                                     std::function<void(LazyStaggeredGridScope&)> content, LazyStaggeredGridOptions o, rt::Key key) {
        LazyStaggered(false, m, rows, s, std::move(content), o, key);
    }

    void LazyPagerLayout(bool vertical, const Modifier& modifier, LazyStateBase& state, int pageCount,
                         std::function<void(int)> content, std::function<rt::Key(int)> key, LazyPagerConfig config, rt::Key compositionKey) {
        LazyLayout(vertical, modifier, state, config.userScrollEnabled, compositionKey, [&](LazyCore& core) {
            core.grid = false;
            core.staggered = false;
            core.pager = true;
            core.beyondCount = std::max(0, config.beyondViewportPageCount);
            core.pageSnap = config.snapPosition;
            core.listState = nullptr;
            core.gridState = nullptr;
            core.pageSize = config.pageSize;
            core.contentPadding = config.contentPadding;
            core.mainSpacing = config.pageSpacing;
            core.mainArrangement.reset();
            core.reverse = false;
            core.horizontalAlignment = config.horizontalAlignment;
            core.verticalAlignment = config.verticalAlignment;
            core.intervals.clear();
            core.intervals.push_back({ pageCount, std::move(content), std::move(key) });
        }, std::move(config.flingBehavior), config.extraModifier, false, config.overscrollEffect, std::move(config.bringIntoViewSpec));
    }

    void LazyColumn(const Modifier& m, LazyListState& s, std::function<void(LazyListScope&)> content, LazyListOptions o, rt::Key key) {
        LazyList(true, m, s, std::move(content), o, key);
    }
    void LazyRow(const Modifier& m, LazyListState& s, std::function<void(LazyListScope&)> content, LazyListOptions o, rt::Key key) {
        LazyList(false, m, s, std::move(content), o, key);
    }
    void LazyVerticalGrid(const Modifier& m, GridCells columns, LazyGridState& s, std::function<void(LazyGridScope&)> content,
                          LazyGridOptions o, rt::Key key) {
        LazyGrid(true, m, columns, s, std::move(content), o, key);
    }
    void LazyHorizontalGrid(const Modifier& m, GridCells rows, LazyGridState& s, std::function<void(LazyGridScope&)> content,
                            LazyGridOptions o, rt::Key key) {
        LazyGrid(false, m, rows, s, std::move(content), o, key);
    }

}

namespace kiln::foundation {
    // ScrollIndicatorState estimates (LazyListLayoutInfo.calculateContentSize and friends):
    // the visible items' average size stands for every item.
    namespace {
        int MainSize(const LazyGridLayoutInfo& info, const LazyGridItemInfo& it) {
            return info.orientation == Orientation::Vertical ? it.size.height : it.size.width;
        }
        int ListAverage(const LazyListLayoutInfo& info) {
            if (info.visibleItemsInfo.empty()) return 0;
            int sum = 0;
            for (const auto& it : info.visibleItemsInfo) sum += it.size;
            return sum / int(info.visibleItemsInfo.size()) + info.mainAxisItemSpacing;
        }
        int ListContent(const LazyListLayoutInfo& info) {
            const int padding = info.beforeContentPadding + info.afterContentPadding;
            if (info.totalItemsCount == 0) return padding;
            return (ListAverage(info) - info.mainAxisItemSpacing) * info.totalItemsCount + (info.totalItemsCount - 1) * info.mainAxisItemSpacing + padding;
        }
        int Viewport(const auto& info) { return info.orientation == Orientation::Vertical ? info.viewportSize.height : info.viewportSize.width; }
        int LineOf(const LazyGridLayoutInfo& info, const LazyGridItemInfo& it) { return info.orientation == Orientation::Vertical ? it.row : it.column; }
        // visibleLinesAverageMainAxisSize: lines of consecutive items, sticky ones (line -1) skipped.
        int GridLinesAverage(const LazyGridLayoutInfo& info) {
            const auto& items = info.visibleItemsInfo;
            if (items.empty()) return 0;
            int total = 0, lines = 0;
            std::size_t start = 0;
            while (start < items.size()) {
                const int line = LineOf(info, items[start]);
                if (line == -1) { ++start; continue; }
                int size = 0;
                std::size_t end = start;
                while (end < items.size() && LineOf(info, items[end]) == line) size = std::max(size, MainSize(info, items[end++]));
                total += size;
                ++lines;
                start = end;
            }
            // Compose divides by the line count unguarded; all-sticky visible items would be 0 lines.
            return (lines ? total / lines : 0) + info.mainAxisItemSpacing;
        }
        int GridContent(const LazyGridLayoutInfo& info) {
            const int padding = info.beforeContentPadding + info.afterContentPadding;
            if (info.totalItemsCount == 0) return padding;
            const int lines = int(std::ceil(float(info.totalItemsCount) / float(std::max(1, info.maxSpan))));
            return (GridLinesAverage(info) - info.mainAxisItemSpacing) * lines + (lines - 1) * info.mainAxisItemSpacing + padding;
        }
        int StaggeredAverage(const LazyGridLayoutInfo& info) {
            if (info.visibleItemsInfo.empty()) return 0;
            int sum = 0;
            for (const auto& it : info.visibleItemsInfo) sum += MainSize(info, it);
            return sum / int(info.visibleItemsInfo.size()) + info.mainAxisItemSpacing;
        }
        int StaggeredContent(const LazyGridLayoutInfo& info) {
            const int padding = info.beforeContentPadding + info.afterContentPadding;
            if (info.totalItemsCount == 0 || info.maxSpan <= 0) return padding;
            return StaggeredAverage(info) * info.totalItemsCount / info.maxSpan - info.mainAxisItemSpacing + padding;
        }
    }

    int LazyListState::Indicator::ScrollOffset() const {
        const auto& info = s->LayoutInfo();
        const int offset = ListAverage(info) * s->FirstVisibleItemIndex() + s->FirstVisibleItemScrollOffset();
        return info.reverseLayout ? ListContent(info) - Viewport(info) - offset : offset;
    }
    int LazyListState::Indicator::ContentSize() const { return ListContent(s->LayoutInfo()); }
    int LazyListState::Indicator::ViewportSize() const { return Viewport(s->LayoutInfo()); }

    int LazyGridState::Indicator::ScrollOffset() const {
        const auto& info = s->LayoutInfo();
        const int first = s->FirstVisibleItemIndex();
        int line = -1;
        for (const auto& it : info.visibleItemsInfo) if (it.index == first) { line = LineOf(info, it); break; }
        if (line == -1) {
            // A sticky header first: the line before the first real one.
            int firstValid = 0;
            for (const auto& it : info.visibleItemsInfo) if (LineOf(info, it) != -1) { firstValid = LineOf(info, it); break; }
            line = std::max(0, firstValid - 1);
        }
        const int offset = GridLinesAverage(info) * line + s->FirstVisibleItemScrollOffset();
        return info.reverseLayout ? GridContent(info) - Viewport(info) - offset : offset;
    }
    int LazyGridState::Indicator::ContentSize() const { return GridContent(s->LayoutInfo()); }
    int LazyGridState::Indicator::ViewportSize() const { return Viewport(s->LayoutInfo()); }

    int LazyStaggeredGridState::Indicator::ScrollOffset() const {
        const auto& info = s->LayoutInfo();
        const int offset = info.totalItemsCount == 0 || info.maxSpan <= 0
                               ? 0 : StaggeredAverage(info) * s->FirstVisibleItemIndex() / info.maxSpan + s->FirstVisibleItemScrollOffset();
        return info.reverseLayout ? StaggeredContent(info) - Viewport(info) - offset : offset;
    }
    int LazyStaggeredGridState::Indicator::ContentSize() const { return StaggeredContent(s->LayoutInfo()); }
    int LazyStaggeredGridState::Indicator::ViewportSize() const { return Viewport(s->LayoutInfo()); }
}

namespace kiln::foundation {
    // ParentSizeNode: exact constraints of `fraction` of the lazy layout's size on the chosen axes.
    namespace {
        struct ParentSizeParams {
            const rt::State<IntSize>* state;
            float fraction;
            bool width, height;
            bool operator==(const ParentSizeParams&) const = default;
        };
        struct ParentSizeNode final : ModifierNode, LayoutModifier {
            ParentSizeParams params;
            explicit ParentSizeNode(ParentSizeParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                const IntSize max = params.state ? params.state->Get() : IntSize{ Constraints::Infinity, Constraints::Infinity };
                const auto scaled = [&](bool on, int v) {
                    return on && v != Constraints::Infinity ? int(RoundJava(float(v) * params.fraction)) : Constraints::Infinity;
                };
                const int w = scaled(params.width, max.width), h = scaled(params.height, max.height);
                Constraints child{ w != Constraints::Infinity ? w : c.minWidth, w != Constraints::Infinity ? w : c.maxWidth,
                                   h != Constraints::Infinity ? h : c.minHeight, h != Constraints::Infinity ? h : c.maxHeight };
                Placeable& p = m.Measure(child);
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }
        };
        Modifier ParentSize(Modifier m, float fraction, bool width, bool height) {
            const rt::State<IntSize>* state = rt::Composition::Current() ? rt::Composition::Current()->Read(LocalItemParentSize) : nullptr;
            return m.With<ParentSizeNode>(ParentSizeParams{ state, fraction, width, height });
        }
    }
    Modifier FillParentMaxSize(Modifier m, float fraction) { return ParentSize(std::move(m), fraction, true, true); }
    Modifier FillParentMaxWidth(Modifier m, float fraction) { return ParentSize(std::move(m), fraction, true, false); }
    Modifier FillParentMaxHeight(Modifier m, float fraction) { return ParentSize(std::move(m), fraction, false, true); }
}
