#include "kiln/foundation/Subcompose.h"

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
            std::shared_ptr<const std::function<void()>> stable;   // content set by identity
        };

        struct State;

        class Scope final : public SubcomposeMeasureScope {
        public:
            Scope(const MeasureScope& s, State& st) : SubcomposeMeasureScope(s), m_State(st) {}
            std::vector<Measurable*> Subcompose(rt::Key slotId, std::function<void()> content) override;
            std::vector<Measurable*> Subcompose(rt::Key slotId, const std::shared_ptr<const std::function<void()>>& content) override;
        private:
            std::vector<Measurable*> Claim(rt::Key slotId, Slot& slot);
            State& m_State;
        };

        struct State {
            std::map<std::uint64_t, Slot> slots;
            std::vector<std::uint64_t> used;   // this measure, in order
            rt::Scope* context = nullptr;
            SubcomposeMeasure measure;
            LayoutNode* node = nullptr;
            bool measuring = false;   // inside Measure: holders measured here are expected

            ~State() {
                // Compositions go before their holders.
                for (auto& [k, s] : slots) s.composition.reset();
            }

            MeasureResult Measure(const MeasureScope& ms, const Constraints& c) {
                used.clear();
                Scope scope(ms, *this);
                measuring = true;
                MeasureResult r = measure ? measure(scope, c) : MeasureResult{ c.minWidth, c.minHeight, {} };
                // Unused slots are disposed (precomposed ones wait); the used holders become the
                // placed children, precomposed ones follow unplaced.
                std::set<std::uint64_t> keep(used.begin(), used.end());
                for (auto it = slots.begin(); it != slots.end();) {
                    if (keep.count(it->first) || it->second.precomposed) { ++it; continue; }
                    it->second.composition.reset();
                    it = slots.erase(it);
                }
                std::vector<LayoutNode*> children;
                for (std::uint64_t k : used) children.push_back(slots[k].holder.get());
                const std::size_t placed = children.size();
                for (auto& [k, sl] : slots) if (sl.precomposed) children.push_back(sl.holder.get());
                node->SetChildrenDuringMeasure(children);
                std::vector<Placeable*> holders;
                for (std::size_t i = 0; i < placed; ++i) holders.push_back(&children[i]->AsMeasurable().Measure(Constraints::Fixed(r.width, r.height)));
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
        Slot& ComposeSlot(State& st, std::uint64_t id, std::function<void()> content) {
            Slot& slot = SlotFor(st, id);
            slot.stable = nullptr;
            slot.composition->SetContent(std::move(content));
            slot.composition->Recompose();
            return slot;
        }

        std::vector<Measurable*> Scope::Subcompose(rt::Key slotId, std::function<void()> content) {
            return Claim(slotId, ComposeSlot(m_State, slotId.value, std::move(content)));
        }
        std::vector<Measurable*> Scope::Subcompose(rt::Key slotId, const std::shared_ptr<const std::function<void()>>& content) {
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
            for (LayoutNode* c : slot.holder->Children()) out.push_back(&c->AsMeasurable());
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

    SubcomposeLayoutState::SubcomposeLayoutState() : m_Impl(std::make_shared<State>()) {}

    SubcomposeLayoutState::PrecomposedSlotHandle SubcomposeLayoutState::Precompose(rt::Key slotId, std::function<void()> content) {
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
                            std::function<void(const BoxWithConstraintsScope&)> content, rt::Key key) {
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
