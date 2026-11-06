#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Overscroll.h"
#include "kiln/runtime/Saveable.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
        struct Slot {
            std::unique_ptr<LayoutNode> holder;
            std::unique_ptr<rt::Composition> composition;
            std::shared_ptr<rt::SaveableStateRegistry> registry;   // the item's rememberSaveable values
            int index = -1;
            std::uint64_t contentVersion = 0;
        };
        constexpr int kNone = std::numeric_limits<int>::min();
    }

    struct LazyCore {
        bool vertical = true;
        bool grid = false;
        LazyStateBase* state = nullptr;
        LazyListState* listState = nullptr;
        LazyGridState* gridState = nullptr;
        PaddingValues contentPadding;
        Dp mainSpacing{ 0 };
        AlignmentH horizontalAlignment = AlignmentH::Start();   // list cross alignment
        AlignmentV verticalAlignment = AlignmentV::Top();
        Arrangement crossArrangement = Arrangement::Start();    // grid slots
        GridCells cells;
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

        static std::string SaveKey(std::uint64_t keyValue) { return std::to_string(keyValue); }
        void DisposeSlot(std::uint64_t keyValue, Slot& slot) {
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

    // --- state ------------------------------------------------------------------------------------

    float LazyStateBase::ConsumeScrollDelta(float delta) {
        if (!m_Node) return 0;
        if (delta < 0 && !CanScrollBackward()) return 0;
        if (delta > 0 && !CanScrollForward()) return 0;
        m_PendingScroll += delta;
        m_LastConsumed = 0;
        m_Node->ForceRemeasure();
        return m_LastConsumed;
    }

    void LazyStateBase::ScrollToItem(int index, int offset) {
        StopScroll();
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

    // --- measure ----------------------------------------------------------------------------------

    MeasureResult LazyCore::Measure(LayoutNode& self, MeasureScope& s, const Constraints& c) {
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
        const int crossSpacingPx = grid ? s.RoundToPx(crossArrangement.Spacing()) : 0;

        // One measured line: its cells and main-axis size.
        struct Cell { int index; Slot* slot; Placeable* p; int crossPos; int column; };
        struct MLine { int line, first, last; std::vector<Cell> cells; int size; };
        std::vector<LayoutNode*> active;
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
                } else {
                    ic = vertical ? Constraints{ 0, crossAvailable, 0, Constraints::Infinity }
                                  : Constraints{ 0, Constraints::Infinity, 0, crossAvailable };
                }
                Placeable& p = sl.holder->AsMeasurable().Measure(ic);
                ml.cells.push_back({ i, &sl, &p, crossPos, slot });
                ml.size = std::max(ml.size, vertical ? p.Height() : p.Width());
                slot += span;
            }
            return ml;
        };

        auto publishEmpty = [&] {
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
        if (count == 0) {
            for (auto& [k, slot] : slots) DisposeSlot(k, slot);
            slots.clear();
            self.SetChildrenDuringMeasure({});
            st.m_LastConsumed = st.m_PendingScroll = 0;
            st.m_Lines.clear();
            st.m_TotalItems = 0;
            publishEmpty();
            return s.Layout(c.minWidth, c.minHeight);
        }
        index = std::clamp(index, 0, count - 1);
        int line = LineOf(index);
        const int lineCount = LineCount();
        const float delta = st.m_PendingScroll;
        st.m_PendingScroll = 0;
        // pos: where the first line's leading edge sits relative to the content start.
        int pos = RoundJava(-float(st.m_Offset.Peek()) - delta);

        std::vector<MLine> lines;
        // Scrolled backward past the first line: measure earlier lines.
        while (pos > 0 && line > 0) {
            --line;
            pos -= measureLine(line).size + spacing;
        }
        float consumed = delta;
        if (pos > 0) { consumed += float(pos); pos = 0; }   // reached the very start

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
                if (pos + gap <= 0) { pos += gap; consumed -= float(gap); gap = 0; break; }
                gap -= -pos; consumed -= float(-pos); pos = 0;
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
        if (const int header = StickyAtOrBefore(firstIndex); header >= 0) {
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

        // Dispose slots not in use (no reuse pool yet).
        std::vector<LayoutNode*> nowActive;
        std::vector<const Slot*> used;
        for (auto& l : lines) for (auto& cell : l.cells) { nowActive.push_back(cell.slot->holder.get()); used.push_back(cell.slot); }
        if (sticking) for (auto& cell : sticking->cells) { nowActive.push_back(cell.slot->holder.get()); used.push_back(cell.slot); }
        self.SetChildrenDuringMeasure(nowActive);
        for (auto it = slots.begin(); it != slots.end();) {
            if (std::find(used.begin(), used.end(), &it->second) == used.end()) { DisposeSlot(it->first, it->second); it = slots.erase(it); }
            else ++it;
        }

        st.m_Index.Set(firstIndex);
        st.m_Offset.Set(-pos);
        st.m_FirstKey = KeyOf(firstIndex);
        st.m_HasFirstKey = true;
        st.m_LastConsumed = consumed;
        st.m_CanForward = !(lastLine == lineCount - 1 && end <= viewport);
        st.m_Density = s.density;
        st.m_SlotsPerLine = slotsPerLine;
        st.m_Spacing = spacing;
        st.m_TotalItems = count;
        (void)lastIndex;

        int cross = 0;
        if (grid) {
            cross = crossMax == Constraints::Infinity ? crossPad : crossMax;
        } else {
            auto crossOf = [&](const MLine& l) { for (auto& cell : l.cells) cross = std::max(cross, (vertical ? cell.p->Width() : cell.p->Height()) + crossPad); };
            for (auto& l : lines) crossOf(l);
            if (sticking) crossOf(*sticking);
        }
        const int w = vertical ? c.ConstrainWidth(cross) : c.ConstrainWidth(mainMax);
        const int h = vertical ? c.ConstrainHeight(mainMax) : c.ConstrainHeight(cross);

        // Layout info.
        if (listState) {
            LazyListLayoutInfo info;
            // The sticking header precedes the items it covers (its index is lower).
            if (sticking) info.visibleItemsInfo.push_back({ sticking->first, UserKey(sticking->first), stickingOffset, sticking->size });
            for (std::size_t i = 0; i < lines.size(); ++i)
                info.visibleItemsInfo.push_back({ lines[i].first, UserKey(lines[i].first), offsets[i], lines[i].size });
            info.viewportStartOffset = -before;
            info.viewportEndOffset = mainMax - before;
            info.totalItemsCount = count;
            info.viewportSize = { w, h };
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.beforeContentPadding = before;
            info.afterContentPadding = after;
            info.mainAxisItemSpacing = spacing;
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
            if (sticking) add(*sticking, stickingOffset);
            for (std::size_t i = 0; i < lines.size(); ++i) add(lines[i], offsets[i]);
            info.viewportStartOffset = -before;
            info.viewportEndOffset = mainMax - before;
            info.totalItemsCount = count;
            info.viewportSize = { w, h };
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.beforeContentPadding = before;
            info.afterContentPadding = after;
            info.mainAxisItemSpacing = spacing;
            if (!(gridState->m_Info.Peek() == info)) gridState->m_Info.Set(std::move(info));
        }

        // Placement, in drawing order (the sticking header last, on top).
        struct Placed { Placeable* p; int main; int cross; };   // cross -1: aligned (list)
        std::vector<Placed> placed;
        for (std::size_t i = 0; i < lines.size(); ++i)
            for (const auto& cell : lines[i].cells) placed.push_back({ cell.p, offsets[i], cell.crossPos });
        if (sticking) for (const auto& cell : sticking->cells) placed.push_back({ cell.p, stickingOffset, cell.crossPos });
        const AlignmentH ha = horizontalAlignment;
        const AlignmentV va = verticalAlignment;
        const bool vert = vertical;
        const LayoutDirection dir = s.Direction();
        return s.Layout(w, h, [placed, before, crossBefore, crossPad, w, h, ha, va, vert, dir](PlacementScope& place) {
            for (const auto& pl : placed) {
                const int m = pl.main + before;
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
        void LazyLayout(bool vertical, const Modifier& modifier, LazyStateBase& state, bool userScrollEnabled, rt::Key key,
                        const std::function<void(LazyCore&)>& configure) {
            auto& comp = *rt::Composition::Current();
            auto& core = comp.Remember<std::shared_ptr<LazyCore>>([] { return std::make_shared<LazyCore>(); }, {}, key);
            core->vertical = vertical;
            core->context = comp.CurrentScope();
            core->holder = &rt::RememberSaveableStateHolder(key);
            core->state = &state;
            configure(*core);
            core->count = 0;
            for (const auto& iv : core->intervals) core->count += iv.count;
            ++core->version;   // item lambdas may capture new values: recompose visible items
            ScrollableOptions so;
            so.enabled = userScrollEnabled;
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

        void LazyList(bool vertical, const Modifier& modifier, LazyListState& state, std::function<void(LazyListScope&)> content,
                      LazyListOptions options, rt::Key key) {
            LazyListScope scope;
            content(scope);
            LazyLayout(vertical, modifier, state, options.userScrollEnabled, key, [&](LazyCore& core) {
                core.grid = false;
                core.listState = &state;
                core.gridState = nullptr;
                core.contentPadding = options.contentPadding;
                core.mainSpacing = options.spacing;
                core.horizontalAlignment = options.horizontalAlignment;
                core.verticalAlignment = options.verticalAlignment;
                core.intervals = std::move(scope.intervals);
            });
        }

        void LazyGrid(bool vertical, const Modifier& modifier, GridCells cells, LazyGridState& state,
                      std::function<void(LazyGridScope&)> content, LazyGridOptions options, rt::Key key) {
            LazyGridScope scope;
            content(scope);
            LazyLayout(vertical, modifier, state, options.userScrollEnabled, key, [&](LazyCore& core) {
                core.grid = true;
                core.gridState = &state;
                core.listState = nullptr;
                core.contentPadding = options.contentPadding;
                core.mainSpacing = options.mainAxisArrangement.Spacing();
                core.crossArrangement = options.crossAxisArrangement;
                core.cells = cells;
                core.intervals = std::move(scope.intervals);
            });
        }
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
