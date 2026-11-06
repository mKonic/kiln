#include "kiln/foundation/Lazy.h"

#include <cmath>

namespace kiln::foundation {

    void LazyListScope::Item(std::function<void()> content, rt::Key key) {
        auto c = std::make_shared<std::function<void()>>(std::move(content));
        intervals.push_back({ 1, [c](int) { (*c)(); }, key.value ? std::function<rt::Key(int)>([key](int) { return key; }) : nullptr });
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
        int count = 0;

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
                if (index < base + iv.count) { iv.content(index - base); return; }
                base += iv.count;
            }
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
            }
            slot.composition->SetParentContext(context);
            if (slot.index != index || slot.contentVersion != version) {
                slot.index = index;
                slot.contentVersion = version;
                slot.composition->SetContent([this, index] { Content(index); });
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
        m_Index.Set(index);
        m_Offset.Set(offset);
        m_HasFirstKey = false;
        m_PendingScroll = 0;
        if (m_Node) m_Node->RequestRemeasure();
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
            for (auto& [k, slot] : slots) slot.composition.reset();
            slots.clear();
            self.SetChildrenDuringMeasure({});
            st.m_LastConsumed = st.m_PendingScroll = 0;
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

        // Dispose slots not in use (no reuse pool yet).
        std::vector<LayoutNode*> nowActive;
        for (auto& v : items) nowActive.push_back(v.slot->holder.get());
        self.SetChildrenDuringMeasure(nowActive);
        for (auto it = slots.begin(); it != slots.end();) {
            bool used = false;
            for (auto& v : items) if (v.slot == &it->second) used = true;
            if (!used) { it->second.composition.reset(); it = slots.erase(it); } else ++it;
        }

        st.m_Index.Set(items.front().index);
        st.m_Offset.Set(-pos);
        st.m_FirstKey = KeyOf(items.front().index);
        st.m_HasFirstKey = true;
        st.m_LastConsumed = consumed;
        st.m_CanForward = !(items.back().index == count - 1 && end <= viewport);

        int cross = 0;
        for (auto& v : items) cross = std::max(cross, (vertical ? v.p->Width() : v.p->Height()) + crossPad);
        const int w = vertical ? c.ConstrainWidth(cross) : c.ConstrainWidth(mainMax);
        const int h = vertical ? c.ConstrainHeight(mainMax) : c.ConstrainHeight(cross);
        const LazyListOptions opt = options;
        const bool vert = vertical;
        const LayoutDirection dir = s.Direction();
        return s.Layout(w, h, [items, pos, before, spacing, crossBefore, crossPad, w, h, opt, vert, dir](PlacementScope& place) {
            int p = pos + before;
            for (const auto& v : items) {
                if (vert) {
                    const int x = crossBefore + opt.horizontalAlignment.Align(v.p->Width(), w - crossPad, dir);
                    place.Place(*v.p, x, p);
                } else {
                    const int y = crossBefore + opt.verticalAlignment.Align(v.p->Height(), h - crossPad);
                    place.PlaceRelative(*v.p, p, y);
                }
                p += v.size + spacing;
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
            core->state = &state;
            core->options = options;
            core->intervals = std::move(scope.intervals);
            core->count = 0;
            for (const auto& iv : core->intervals) core->count += iv.count;
            ++core->version;   // item lambdas may capture new values: recompose visible items
            ScrollableOptions so;
            so.enabled = options.userScrollEnabled;
            so.reverseDirection = true;
            Modifier m = Scrollable(ClipToBounds(modifier), state, vertical ? Orientation::Vertical : Orientation::Horizontal, so);
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
