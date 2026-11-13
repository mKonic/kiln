#include "kiln/foundation/Subcompose.h"
#include <kiln/Function.h>

#include <algorithm>
#include <map>
#include <set>

namespace kiln::foundation {

    using namespace ui;

    namespace {
        struct State;
        void HolderRemeasured(State& st);

        // A slot's holder spans the SubcomposeLayout and places nothing itself: its children are
        // the slot content's roots, measured and placed by the user's measure block. Measured
        // again on its own, a root changed: the layout measures again, as it would for a direct
        // child (the holder's fixed size would otherwise hide the change).
        struct HolderPolicy final : MeasurePolicy {
            State* state = nullptr;
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                if (state) HolderRemeasured(*state);
                return s.Layout(c.minWidth, c.minHeight);
            }
        };

        struct Slot {
            std::unique_ptr<LayoutNode> holder;
            std::unique_ptr<rt::Composition> composition;
            bool precomposed = false;   // composed ahead, not yet claimed by a measure
            std::shared_ptr<const kiln::Function<void()>> stable;   // content set by identity
        };

        struct State;

        class Scope final : public SubcomposeMeasureScope {
        public:
            Scope(const MeasureScope& s, State& st) : SubcomposeMeasureScope(s), m_State(st) {}
            std::vector<Measurable*> Subcompose(rt::Key slotId, kiln::Function<void()> content) override;
            std::vector<Measurable*> Subcompose(rt::Key slotId, const std::shared_ptr<const kiln::Function<void()>>& content) override;
        private:
            std::vector<Measurable*> Claim(rt::Key slotId, Slot& slot);
            State& m_State;
        };

        // Which pass measures: outside a lookahead scope only Main; inside it, Lookahead then
        // Approach (LayoutNodeSubcompositionsState's approach measure scope).
        enum class Pass : std::uint8_t { Main, Lookahead, Approach };

        struct State {
            std::map<std::uint64_t, Slot> slots;
            std::vector<std::uint64_t> used;   // this measure, in order
            // In a lookahead scope: the slots each pass used last. The approach pass reuses
            // what lookahead composed and composes only what it alone needs; a slot goes when
            // neither pass uses it.
            std::vector<std::uint64_t> lookaheadUsed, approachUsed;
            Pass pass = Pass::Main;
            rt::Scope* context = nullptr;
            SubcomposeMeasure measure;
            LayoutNode* node = nullptr;
            bool measuring = false;   // inside Measure: holders measured here are expected

            ~State() {
                // Compositions go before their holders.
                for (auto& [k, s] : slots) s.composition.reset();
            }

            bool UsedInLookahead(std::uint64_t id) const { return std::find(lookaheadUsed.begin(), lookaheadUsed.end(), id) != lookaheadUsed.end(); }

            MeasureResult Measure(const MeasureScope& ms, const Constraints& c) {
                pass = ms.IsLookingAhead() ? Pass::Lookahead : (node && node->LookaheadRoot() ? Pass::Approach : Pass::Main);
                used.clear();
                Scope scope(ms, *this);
                measuring = true;
                MeasureResult r = measure ? measure(scope, c) : MeasureResult{ c.minWidth, c.minHeight, {} };
                if (pass == Pass::Lookahead) lookaheadUsed = used;
                else if (pass == Pass::Approach) approachUsed = used;
                else lookaheadUsed.clear(), approachUsed.clear();
                // Unused slots are disposed (precomposed ones wait): outside a scope after each
                // measure, inside it after the approach (disposeUnusedSlotsInApproach). The used
                // holders become the placed children; the ones the other pass keeps, then
                // precomposed ones, follow unplaced.
                std::set<std::uint64_t> keep(used.begin(), used.end());
                if (pass != Pass::Main) {
                    keep.insert(lookaheadUsed.begin(), lookaheadUsed.end());
                    keep.insert(approachUsed.begin(), approachUsed.end());
                }
                const bool disposes = pass != Pass::Lookahead;
                const auto kept = [&](std::uint64_t k, const Slot& sl) { return !disposes || keep.count(k) || sl.precomposed; };
                std::vector<LayoutNode*> children;
                for (std::uint64_t k : used) children.push_back(slots[k].holder.get());
                const std::size_t placed = children.size();
                for (std::uint64_t k : keep)
                    if (std::find(used.begin(), used.end(), k) == used.end() && slots.count(k) && !slots[k].precomposed) children.push_back(slots[k].holder.get());
                for (auto& [k, sl] : slots)
                    if (sl.precomposed) children.push_back(sl.holder.get());
                    else if (!disposes && !keep.count(k)) children.push_back(sl.holder.get());   // lookahead keeps what it does not use
                // The holders leave the layout before their slots free them.
                node->SetChildrenDuringMeasure(children);
                for (auto it = slots.begin(); it != slots.end();) {
                    if (kept(it->first, it->second)) { ++it; continue; }
                    it->second.composition.reset();
                    it = slots.erase(it);
                }
                std::vector<Placeable*> holders;
                for (std::size_t i = 0; i < placed; ++i) {
                    Measurable& m = pass == Pass::Lookahead ? children[i]->AsLookaheadMeasurable() : children[i]->AsMeasurable();
                    holders.push_back(&m.Measure(Constraints::Fixed(r.width, r.height)));
                }
                measuring = false;
                auto place = std::move(r.place);
                r.place = [holders, place](PlacementScope& p) {
                    for (Placeable* h : holders) p.Place(*h, 0, 0);
                    if (place) place(p);
                };
                return r;
            }
        };

        void HolderRemeasured(State& st) {
            if (!st.measuring && st.node) st.node->RequestRemeasure();
        }

        // Creates the slot if needed. A new holder joins the layout's children at once, so its
        // content is measured attached (with the owner's density and locals), not detached.
        Slot& SlotFor(State& st, std::uint64_t id) {
            Slot& slot = st.slots[id];
            if (!slot.holder) {
                slot.holder = std::make_unique<LayoutNode>();
                auto policy = std::make_shared<HolderPolicy>();
                policy->state = &st;
                slot.holder->SetMeasurePolicy(policy);
                slot.holder->SetZIndexFromChildren(true);
                if (st.node) {
                    std::vector<LayoutNode*> children(st.node->Children().begin(), st.node->Children().end());
                    children.push_back(slot.holder.get());
                    st.node->SetChildrenDuringMeasure(children);
                }
                slot.composition = std::make_unique<rt::Composition>(*slot.holder, rt::Dispatcher::Main());
                slot.composition->SetParentContext(st.context);
            }
            return slot;
        }
        Slot& ComposeSlot(State& st, std::uint64_t id, kiln::Function<void()> content) {
            Slot& slot = SlotFor(st, id);
            slot.stable = nullptr;
            slot.composition->SetContent(std::move(content));
            slot.composition->Recompose();
            return slot;
        }

        std::vector<Measurable*> Scope::Subcompose(rt::Key slotId, kiln::Function<void()> content) {
            // The approach pass takes what lookahead composed as it is.
            if (m_State.pass == Pass::Approach && m_State.UsedInLookahead(slotId.value) && m_State.slots.count(slotId.value))
                return Claim(slotId, m_State.slots[slotId.value]);
            return Claim(slotId, ComposeSlot(m_State, slotId.value, std::move(content)));
        }
        std::vector<Measurable*> Scope::Subcompose(rt::Key slotId, const std::shared_ptr<const kiln::Function<void()>>& content) {
            if (m_State.pass == Pass::Approach && m_State.UsedInLookahead(slotId.value) && m_State.slots.count(slotId.value))
                return Claim(slotId, m_State.slots[slotId.value]);
            Slot& slot = SlotFor(m_State, slotId.value);
            if (slot.stable != content) {
                slot.stable = content;
                slot.composition->SetContent([content] { if (content && *content) (*content)(); });
            }
            slot.composition->Recompose();   // applies pending writes; runs only invalid scopes
            return Claim(slotId, slot);
        }
        std::vector<Measurable*> Scope::Claim(rt::Key slotId, Slot& slot) {
            State& st = m_State;
            st.used.push_back(slotId.value);
            slot.precomposed = false;
            std::vector<Measurable*> out;
            const bool approachOnly = st.pass == Pass::Approach && !st.UsedInLookahead(slotId.value);
            for (LayoutNode* c : slot.holder->Children()) {
                if (approachOnly) c->MarkDetachedFromParentLookaheadPass();
                out.push_back(st.pass == Pass::Lookahead ? &c->AsLookaheadMeasurable() : &c->AsMeasurable());
            }
            return out;
        }

        struct Policy final : MeasurePolicy {
            std::shared_ptr<State> state;
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                return state->Measure(s, c);
            }
        };
    }

    namespace {
        void SubcomposeLayoutWith(const std::shared_ptr<State>& state, const Modifier& modifier, SubcomposeMeasure measure, rt::Key key) {
            auto& comp = *rt::Composition::Current();
            state->measure = std::move(measure);
            state->context = comp.CurrentScope();
            auto st = state;
            comp.Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                                  [&](LayoutNode& n) {
                                      n.SetModifier(modifier);
                                      TakeLocalLayoutDirection(n);
                                      n.SetMeasureManagedChildren(true);
                                      st->node = &n;
                                      if (!dynamic_cast<Policy*>(n.Policy().get())) {
                                          auto p = std::make_shared<Policy>();
                                          p->state = st;
                                          n.SetMeasurePolicy(p);
                                      }
                                      n.RequestRemeasure();   // the measure block may capture new values
                                  }, {}, key);
        }
    }

    SubcomposeLayoutState::SubcomposeLayoutState() : m_Impl(std::make_shared<State>()), m_Policy(MakeSubcomposeSlotReusePolicy(0)) {}
    SubcomposeLayoutState::SubcomposeLayoutState(std::shared_ptr<SubcomposeSlotReusePolicy> policy)
        : m_Impl(std::make_shared<State>()), m_Policy(policy ? std::move(policy) : MakeSubcomposeSlotReusePolicy(0)) {}

    std::shared_ptr<SubcomposeSlotReusePolicy> MakeSubcomposeSlotReusePolicy(int maxSlotsToRetainForReuse) {
        struct FixedCount final : SubcomposeSlotReusePolicy {
            int max;
            explicit FixedCount(int m) : max(m) {}
            void GetSlotsToRetain(SlotIdsSet& ids) override {
                // The set lists the oldest first: drop from the front past the limit.
                std::vector<rt::Key> keep(ids.Ids().end() - std::min<std::ptrdiff_t>(max, std::ptrdiff_t(ids.Size())), ids.Ids().end());
                ids.Clear();
                for (const rt::Key& k : keep) ids.Add(k);
            }
            bool AreCompatible(rt::Key, rt::Key) override { return true; }
        };
        return std::make_shared<FixedCount>(std::max(0, maxSlotsToRetainForReuse));
    }

    SubcomposeLayoutState::PausedPrecomposition SubcomposeLayoutState::CreatePausedPrecomposition(rt::Key slotId, kiln::Function<void()> content) {
        PausedPrecomposition p;
        p.m_Owner = this;
        p.m_Slot = slotId;
        p.m_Content = std::move(content);
        return p;
    }

    bool SubcomposeLayoutState::PausedPrecomposition::Resume(const rt::ShouldPauseCallback&) {
        if (!m_Complete && !m_Cancelled && m_Owner) {
            m_Handle = m_Owner->Precompose(m_Slot, std::move(m_Content));
            m_Complete = true;
        }
        return m_Complete;
    }

    SubcomposeLayoutState::PrecomposedSlotHandle SubcomposeLayoutState::PausedPrecomposition::Apply() {
        Resume({});
        return std::move(m_Handle);
    }

    void SubcomposeLayoutState::PausedPrecomposition::Cancel() {
        m_Cancelled = true;
        m_Handle.Dispose();
    }

    SubcomposeLayoutState::PrecomposedSlotHandle SubcomposeLayoutState::Precompose(rt::Key slotId, kiln::Function<void()> content) {
        PrecomposedSlotHandle h;
        auto st = std::static_pointer_cast<State>(m_Impl);
        if (!st->node || !st->node->GetOwner()) return h;   // root.isAttached
        const bool inUse = st->slots.count(slotId.value) && !st->slots[slotId.value].precomposed;
        if (!inUse) {
            Slot& slot = ComposeSlot(*st, slotId.value, std::move(content));
            slot.precomposed = true;
            // Attached (unplaced) after the current children, so it can be premeasured.
            std::vector<LayoutNode*> children(st->node->Children().begin(), st->node->Children().end());
            if (std::find(children.begin(), children.end(), slot.holder.get()) == children.end()) {
                children.push_back(slot.holder.get());
                st->node->SetChildrenDuringMeasure(children);
            }
        }
        h.m_State = st;
        h.m_Slot = slotId.value;
        return h;
    }

    void SubcomposeLayoutState::PrecomposedSlotHandle::Dispose() {
        auto st = std::static_pointer_cast<State>(m_State.lock());
        if (!st) return;
        auto it = st->slots.find(m_Slot);
        if (it == st->slots.end() || !it->second.precomposed) return;   // claimed by a measure meanwhile
        LayoutNode* holder = it->second.holder.get();
        std::vector<LayoutNode*> children;
        for (LayoutNode* c : st->node->Children()) if (c != holder) children.push_back(c);
        st->node->SetChildrenDuringMeasure(children);
        it->second.composition.reset();
        st->slots.erase(it);
        m_State.reset();
    }

    int SubcomposeLayoutState::PrecomposedSlotHandle::PlaceablesCount() const {
        auto st = std::static_pointer_cast<State>(m_State.lock());
        if (!st) return 0;
        auto it = st->slots.find(m_Slot);
        return it == st->slots.end() ? 0 : int(it->second.holder->Children().size());
    }

    void SubcomposeLayoutState::PrecomposedSlotHandle::Premeasure(int index, const Constraints& c) {
        auto st = std::static_pointer_cast<State>(m_State.lock());
        if (!st) return;
        auto it = st->slots.find(m_Slot);
        if (it == st->slots.end()) return;
        const auto& kids = it->second.holder->Children();
        if (index >= 0 && std::size_t(index) < kids.size()) kids[std::size_t(index)]->AsMeasurable().Measure(c);
    }

    void SubcomposeLayout(SubcomposeLayoutState& state, const Modifier& modifier, SubcomposeMeasure measure) {
        SubcomposeLayoutWith(std::static_pointer_cast<State>(state.Impl()), modifier, std::move(measure), {});
    }

    void SubcomposeLayout(const Modifier& modifier, SubcomposeMeasure measure, rt::Key key) {
        auto& comp = *rt::Composition::Current();
        auto& state = comp.Remember<std::shared_ptr<State>>([] { return std::make_shared<State>(); }, {}, key);
        SubcomposeLayoutWith(state, modifier, std::move(measure), key);
    }

    void BoxWithConstraints(const Modifier& modifier, Alignment alignment, bool propagateMin,
                            kiln::Function<void(const BoxWithConstraintsScope&)> content, rt::Key key) {
        auto box = std::make_shared<BoxPolicy>(alignment, propagateMin);
        SubcomposeLayout(modifier, [box, content = std::move(content)](SubcomposeMeasureScope& s, const Constraints& c) {
            const BoxWithConstraintsScope scope{ c, s };
            // Policies may keep the span they measure until placement: keep the list alive with it.
            auto ms = std::make_shared<std::vector<Measurable*>>(s.Subcompose(rt::Key(0), [content, scope] { content(scope); }));
            MeasureResult r = box->Measure(s, *ms, c);
            r.place = [ms, place = std::move(r.place)](PlacementScope& p) { if (place) place(p); };
            return r;
        }, key);
    }

}
