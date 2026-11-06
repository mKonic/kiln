#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Overscroll.h"
#include "kiln/runtime/Saveable.h"

#include <cmath>

namespace kiln::foundation {

    void LazyListScope::Item(std::function<void()> content, rt::Key key) {
        auto c = std::make_shared<std::function<void()>>(std::move(content));
        intervals.push_back({ 1, [c](int) { (*c)(); }, key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr });
    }
    void LazyListScope::StickyHeader(std::function<void(int)> content, rt::Key key) {
        // The interval's content gets the local index (0); the global index is filled in by the core.
        intervals.push_back({ 1, std::move(content), key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr, true });
    }
    void LazyListScope::Items(int count, std::function<void(int)> content, std::function<rt::Key(int)> key) {
        intervals.push_back({ count, std::move(content), std::move(key) });
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
        struct Slot {
            std::unique_ptr<LayoutNode> holder;
            std::unique_ptr<rt::Composition> composition;
            std::shared_ptr<rt::SaveableStateRegistry> registry;   // the item's rememberSaveable values
            int index = -1;
            std::uint64_t contentVersion = 0;
        };
    }

    struct LazyCore {
        bool vertical = true;
        LazyListState* state = nullptr;
        LazyListOptions options;
        std::vector<LazyListScope::Interval> intervals;
        std::uint64_t version = 0;
        std::map<std::uint64_t, Slot> slots;     // by item key value
        rt::Scope* context = nullptr;            // Locals the items see
        rt::SaveableStateHolder* holder = nullptr;   // keeps item state while items are off-screen
        int count = 0;

        static std::string SaveKey(std::uint64_t keyValue) { return std::to_string(keyValue); }
        // Saves the item's state (while it is still composed), then disposes it.
        void DisposeSlot(std::uint64_t keyValue, Slot& slot) {
            if (holder && slot.registry) holder->Close(SaveKey(keyValue));
            slot.composition.reset();
            slot.registry.reset();
        }

        rt::Key UserKey(int index) const {
            int base = 0;
            for (const auto& iv : intervals) {
                if (index < base + iv.count) { if (iv.key) return iv.key(index - base); break; }
                base += iv.count;
            }
            rt::Key k = rt::Key::Positional();
            k.value = std::uint64_t(index);
            return k;
        }
        rt::Key KeyOf(int index) const {
            int base = 0;
            for (const auto& iv : intervals) {
                if (index < base + iv.count) {
                    if (iv.key) { rt::Key k = iv.key(index - base); k.value = k.value * 31 + 0x51ed; return k; }
                    break;
                }
                base += iv.count;
            }
            rt::Key k = rt::Key::Positional();
            k.value = std::uint64_t(index) * 2 + 1;   // index-keyed items
            return k;
        }
        void Content(int index) const {
            int base = 0;
            for (const auto& iv : intervals) {
                if (index < base + iv.count) { iv.content(iv.sticky ? index : index - base); return; }
                base += iv.count;
            }
        }
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
        bool IsSticky(int index) const {
            int base = 0;
            for (const auto& iv : intervals) {
                if (index < base + iv.count) return iv.sticky;
                base += iv.count;
            }
            return false;
        }
        int IndexOfKey(rt::Key key, int hint) const {
            if (hint >= 0 && hint < count && KeyOf(hint).value == key.value) return hint;
            for (int i = 0; i < count; ++i) if (KeyOf(i).value == key.value) return i;
            return -1;
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
            if (slot.index != index || slot.contentVersion != version) {
                slot.index = index;
                slot.contentVersion = version;
                rt::SaveableStateRegistry* reg = slot.registry.get();
                slot.composition->SetContent([this, index, reg] {
                    if (!reg) { Content(index); return; }
                    rt::Composition::Current()->Provide(rt::LocalSaveableStateRegistry, reg, [this, index] { Content(index); });
                });
            }
            slot.composition->Recompose();
            return slot;
        }

        MeasureResult Measure(LayoutNode& self, MeasureScope& s, const Constraints& c);
    };

    float LazyListState::ConsumeScrollDelta(float delta) {
        if (!m_Node) return 0;
        if (delta < 0 && !CanScrollBackward()) return 0;
        if (delta > 0 && !CanScrollForward()) return 0;
        m_PendingScroll += delta;
        m_LastConsumed = 0;
        m_Node->ForceRemeasure();
        return m_LastConsumed;
    }

    void LazyListState::ScrollToItem(int index, int offset) {
        StopScroll();
        m_Index.Set(index);
        m_Offset.Set(offset);
        m_HasFirstKey = false;
        m_PendingScroll = 0;
        if (m_Node) m_Node->RequestRemeasure();
    }

    void LazyListState::SnapToItem(int index, int offset) {
        m_Index.Set(index);
        m_Offset.Set(offset);
        m_HasFirstKey = false;
        m_PendingScroll = 0;
        if (m_Node) m_Node->ForceRemeasure();
    }

    int LazyListState::LastVisibleIndex() const {
        const auto& v = m_Info.Peek().visibleItemsInfo;
        return v.empty() ? 0 : v.back().index;
    }
    bool LazyListState::Visible(int index) const { return index >= m_Index.Peek() && index <= LastVisibleIndex(); }

    // calculateDistanceTo: exact for composed items, else by the average visible item size.
    int LazyListState::DistanceTo(int index, int targetOffset) const {
        const auto& info = m_Info.Peek();
        if (info.visibleItemsInfo.empty()) return 0;
        int d = 0;
        if (!Visible(index)) {
            int sum = 0;
            for (const auto& it : info.visibleItemsInfo) sum += it.size;
            const int average = sum / int(info.visibleItemsInfo.size()) + info.mainAxisItemSpacing;
            d = average * (index - m_Index.Peek()) - m_Offset.Peek();
        } else {
            for (const auto& it : info.visibleItemsInfo) if (it.index == index) { d = it.offset; break; }
        }
        return d + targetOffset;
    }

    rt::Task<> LazyListState::AnimateScrollToItem(int index, int scrollOffset) {
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
        while (loop && m_Info.Peek().totalItemsCount > 0) {
            const int expected = DistanceTo(index) + scrollOffset;
            const float target = std::abs(float(expected)) < targetDistance
                ? (forward ? 1.0f : -1.0f) * std::max(std::abs(float(expected)), minDistance)
                : (forward ? targetDistance : -targetDistance);
            anim::Animatable a(0.0f);
            running = &a;
            float prev = 0;
            co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, anim::Spring(), anim::Vec{ velocity, 0, 0, 0 }, [&](const anim::Vec& vv) {
                const float value = vv[0];
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
            velocity = a.Velocity()[0];
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

    MeasureResult LazyCore::Measure(LayoutNode& self, MeasureScope& s, const Constraints& c) {
        LazyListState& st = *state;
        st.m_Node = &self;
        rt::Dispatcher& d = rt::Dispatcher::Main();
        const int mainMax = vertical ? c.maxHeight : c.maxWidth;
        const int crossMax = vertical ? c.maxWidth : c.maxHeight;
        const int before = s.RoundToPx(vertical ? options.contentPadding.top : options.contentPadding.start);
        const int after = s.RoundToPx(vertical ? options.contentPadding.bottom : options.contentPadding.end);
        const int crossBefore = s.RoundToPx(vertical ? options.contentPadding.start : options.contentPadding.top);
        const int crossPad = crossBefore + s.RoundToPx(vertical ? options.contentPadding.end : options.contentPadding.bottom);
        const int spacing = s.RoundToPx(options.spacing);
        const Constraints itemC = vertical ? Constraints{ 0, std::max(0, crossMax - crossPad), 0, Constraints::Infinity }
                                           : Constraints{ 0, Constraints::Infinity, 0, std::max(0, crossMax - crossPad) };
        auto mainOf = [&](Placeable& p) { return vertical ? p.Height() : p.Width(); };

        // Key-stable position: after a data change, find the first item by key.
        int index = st.m_Index.Peek();
        if (st.m_HasFirstKey) {
            const int found = IndexOfKey(st.m_FirstKey, index);
            if (found >= 0) index = found;
        }
        if (count == 0) {
            for (auto& [k, slot] : slots) DisposeSlot(k, slot);
            slots.clear();
            self.SetChildrenDuringMeasure({});
            st.m_LastConsumed = st.m_PendingScroll = 0;
            LazyListLayoutInfo info;
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.viewportSize = { c.minWidth, c.minHeight };
            if (!(st.m_Info.Peek() == info)) st.m_Info.Set(std::move(info));
            return s.Layout(c.minWidth, c.minHeight);
        }
        index = std::clamp(index, 0, count - 1);
        const float delta = st.m_PendingScroll;
        st.m_PendingScroll = 0;
        // pos: where the first item's leading edge sits relative to the content start.
        float posF = -float(st.m_Offset.Peek()) - delta;
        int pos = RoundJava(posF);

        struct Visible { int index; Slot* slot; Placeable* p; int size; };
        std::vector<Visible> items;
        auto measureItem = [&](int i) -> Visible {
            Slot& slot = Compose(i, d);
            Placeable& p = slot.holder->AsMeasurable().Measure(itemC);
            return { i, &slot, &p, mainOf(p) };
        };
        std::vector<LayoutNode*> active;
        auto activate = [&](Slot* slot) {
            if (std::find(active.begin(), active.end(), slot->holder.get()) == active.end()) active.push_back(slot->holder.get());
        };
        // Children must be attached before they are measured (to get an owner).
        auto attachForMeasure = [&](int i) {
            Slot& slot = Compose(i, d);
            activate(&slot);
            self.SetChildrenDuringMeasure(active);
        };

        // Scrolled backward past the first item: compose earlier items.
        while (pos > 0 && index > 0) {
            --index;
            attachForMeasure(index);
            Visible v = measureItem(index);
            pos -= v.size + spacing;
        }
        float consumed = delta;
        if (pos > 0) { consumed += float(pos); pos = 0; }   // reached the very start

        // Skip items fully above the viewport, then fill forward.
        const int viewport = mainMax - before - after;
        for (;;) {
            attachForMeasure(index);
            Visible v = measureItem(index);
            if (pos + v.size + spacing <= 0 && index < count - 1) { pos += v.size + spacing; ++index; continue; }
            items.push_back(v);
            break;
        }
        int end = pos + items.back().size;
        while (end < viewport + after && items.back().index < count - 1) {
            attachForMeasure(items.back().index + 1);
            Visible v = measureItem(items.back().index + 1);
            items.push_back(v);
            end += spacing + v.size;
        }
        // Content ends before the viewport does: pull back (composing earlier items) and give the
        // unconsumed part back.
        if (end < viewport && items.back().index == count - 1) {
            int gap = viewport - end;
            while (gap > 0) {
                if (pos + gap <= 0) { pos += gap; consumed -= float(gap); gap = 0; break; }
                gap -= -pos; consumed -= float(-pos); pos = 0;
                if (items.front().index == 0) { break; }
                attachForMeasure(items.front().index - 1);
                Visible v = measureItem(items.front().index - 1);
                items.insert(items.begin(), v);
                pos -= v.size + spacing;
            }
        }
        // Drop items that ended up above the viewport after the pull-back.
        while (items.size() > 1 && pos + items.front().size + spacing <= 0) {
            pos += items.front().size + spacing;
            items.erase(items.begin());
        }

        const int firstIndex = items.front().index, lastIndex = items.back().index;
        // Offsets of the items from the content start.
        std::vector<int> offsets;
        {
            int p = pos;
            for (auto& v : items) { offsets.push_back(p); p += v.size + spacing; }
        }
        // Sticky header (StickToTopPlacement): the last header at or before the first visible
        // item, at the viewport start, pushed back by the next visible header; drawn on top.
        std::optional<Visible> sticking;
        int stickingOffset = 0;
        if (const int header = StickyAtOrBefore(items.front().index); header >= 0) {
            int nextHeader = std::numeric_limits<int>::min();
            for (std::size_t i = 0; i < items.size(); ++i)
                if (items[i].index != header && IsSticky(items[i].index)) { nextHeader = offsets[i]; break; }
            int itemOffset = std::numeric_limits<int>::min();
            for (std::size_t i = 0; i < items.size(); ++i)
                if (items[i].index == header) {
                    sticking = items[i];
                    itemOffset = offsets[i];
                    items.erase(items.begin() + std::ptrdiff_t(i));
                    offsets.erase(offsets.begin() + std::ptrdiff_t(i));
                    break;
                }
            if (!sticking) {
                attachForMeasure(header);
                sticking = measureItem(header);
            }
            int off = itemOffset == std::numeric_limits<int>::min() ? -before : std::max(-before, itemOffset);
            if (nextHeader != std::numeric_limits<int>::min()) off = std::min(off, nextHeader - (sticking->size + spacing));
            stickingOffset = off;
        }

        // Dispose slots not in use (no reuse pool yet).
        std::vector<LayoutNode*> nowActive;
        for (auto& v : items) nowActive.push_back(v.slot->holder.get());
        if (sticking) nowActive.push_back(sticking->slot->holder.get());
        self.SetChildrenDuringMeasure(nowActive);
        for (auto it = slots.begin(); it != slots.end();) {
            bool used = false;
            for (auto& v : items) if (v.slot == &it->second) used = true;
            if (sticking && sticking->slot == &it->second) used = true;
            if (!used) { DisposeSlot(it->first, it->second); it = slots.erase(it); } else ++it;
        }

        st.m_Index.Set(firstIndex);
        st.m_Offset.Set(-pos);
        st.m_FirstKey = KeyOf(firstIndex);
        st.m_HasFirstKey = true;
        st.m_LastConsumed = consumed;
        st.m_CanForward = !(lastIndex == count - 1 && end <= viewport);
        st.m_Density = s.density;

        int cross = 0;
        for (auto& v : items) cross = std::max(cross, (vertical ? v.p->Width() : v.p->Height()) + crossPad);
        if (sticking) cross = std::max(cross, (vertical ? sticking->p->Width() : sticking->p->Height()) + crossPad);
        const int w = vertical ? c.ConstrainWidth(cross) : c.ConstrainWidth(mainMax);
        const int h = vertical ? c.ConstrainHeight(mainMax) : c.ConstrainHeight(cross);
        {
            LazyListLayoutInfo info;
            // The sticking header precedes the items it covers (its index is lower).
            if (sticking) info.visibleItemsInfo.push_back({ sticking->index, UserKey(sticking->index), stickingOffset, sticking->size });
            for (std::size_t i = 0; i < items.size(); ++i)
                info.visibleItemsInfo.push_back({ items[i].index, UserKey(items[i].index), offsets[i], items[i].size });
            info.viewportStartOffset = -before;
            info.viewportEndOffset = mainMax - before;
            info.totalItemsCount = count;
            info.viewportSize = { w, h };
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.beforeContentPadding = before;
            info.afterContentPadding = after;
            info.mainAxisItemSpacing = spacing;
            if (!(st.m_Info.Peek() == info)) st.m_Info.Set(std::move(info));
        }
        const LazyListOptions opt = options;
        const bool vert = vertical;
        const LayoutDirection dir = s.Direction();
        std::vector<std::pair<Placeable*, int>> placed;   // in drawing order
        for (std::size_t i = 0; i < items.size(); ++i) placed.push_back({ items[i].p, offsets[i] });
        if (sticking) placed.push_back({ sticking->p, stickingOffset });
        return s.Layout(w, h, [placed, before, crossBefore, crossPad, w, h, opt, vert, dir](PlacementScope& place) {
            for (const auto& [pl, off] : placed) {
                const int p = off + before;
                if (vert) {
                    const int x = crossBefore + opt.horizontalAlignment.Align(pl->Width(), w - crossPad, dir);
                    place.Place(*pl, x, p);
                } else {
                    const int y = crossBefore + opt.verticalAlignment.Align(pl->Height(), h - crossPad);
                    place.PlaceRelative(*pl, p, y);
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

        void LazyList(bool vertical, const Modifier& modifier, LazyListState& state, std::function<void(LazyListScope&)> content,
                      LazyListOptions options, rt::Key key) {
            auto& comp = *rt::Composition::Current();
            auto& core = comp.Remember<std::shared_ptr<LazyCore>>([] { return std::make_shared<LazyCore>(); }, {}, key);
            LazyListScope scope;
            content(scope);
            core->vertical = vertical;
            core->context = comp.CurrentScope();
            core->holder = &rt::RememberSaveableStateHolder(key);
            core->state = &state;
            core->options = options;
            core->intervals = std::move(scope.intervals);
            core->count = 0;
            for (const auto& iv : core->intervals) core->count += iv.count;
            ++core->version;   // item lambdas may capture new values: recompose visible items
            ScrollableOptions so;
            so.enabled = options.userScrollEnabled;
            so.reverseDirection = true;
            so.overscroll = RememberOverscrollEffect(key);
            Modifier m = Scrollable(Overscroll(ClipToBounds(modifier), so.overscroll), state,
                                    vertical ? Orientation::Vertical : Orientation::Horizontal, so);
            auto c = core;
            comp.Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                                  [&](LayoutNode& n) {
                                      n.SetModifier(m);
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
    }

    void LazyColumn(const Modifier& m, LazyListState& s, std::function<void(LazyListScope&)> content, LazyListOptions o, rt::Key key) {
        LazyList(true, m, s, std::move(content), o, key);
    }
    void LazyRow(const Modifier& m, LazyListState& s, std::function<void(LazyListScope&)> content, LazyListOptions o, rt::Key key) {
        LazyList(false, m, s, std::move(content), o, key);
    }

}
