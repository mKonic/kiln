#include "kiln/foundation/Subcompose.h"

#include <map>
#include <set>

namespace kiln::foundation {

    using namespace ui;

    namespace {
        // A slot's holder spans the SubcomposeLayout and places nothing itself: its children are
        // the slot content's roots, measured and placed by the user's measure block.
        struct HolderPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints& c) override {
                return s.Layout(c.minWidth, c.minHeight);
            }
        };

        struct Slot {
            std::unique_ptr<LayoutNode> holder;
            std::unique_ptr<rt::Composition> composition;
        };

        struct State;

        class Scope final : public SubcomposeMeasureScope {
        public:
            Scope(const MeasureScope& s, State& st) : SubcomposeMeasureScope(s), m_State(st) {}
            std::vector<Measurable*> Subcompose(rt::Key slotId, std::function<void()> content) override;
        private:
            State& m_State;
        };

        struct State {
            std::map<std::uint64_t, Slot> slots;
            std::vector<std::uint64_t> used;   // this measure, in order
            rt::Scope* context = nullptr;
            SubcomposeMeasure measure;
            LayoutNode* node = nullptr;

            ~State() {
                // Compositions go before their holders.
                for (auto& [k, s] : slots) s.composition.reset();
            }

            MeasureResult Measure(const MeasureScope& ms, const Constraints& c) {
                used.clear();
                Scope scope(ms, *this);
                MeasureResult r = measure ? measure(scope, c) : MeasureResult{ c.minWidth, c.minHeight, {} };
                // Unused slots are disposed; the used holders become the children.
                std::set<std::uint64_t> keep(used.begin(), used.end());
                for (auto it = slots.begin(); it != slots.end();) {
                    if (keep.count(it->first)) { ++it; continue; }
                    it->second.composition.reset();
                    it = slots.erase(it);
                }
                std::vector<LayoutNode*> children;
                for (std::uint64_t k : used) children.push_back(slots[k].holder.get());
                node->SetChildrenDuringMeasure(children);
                std::vector<Placeable*> holders;
                for (LayoutNode* h : children) holders.push_back(&h->AsMeasurable().Measure(Constraints::Fixed(r.width, r.height)));
                auto place = std::move(r.place);
                r.place = [holders, place](PlacementScope& p) {
                    for (Placeable* h : holders) p.Place(*h, 0, 0);
                    if (place) place(p);
                };
                return r;
            }
        };

        std::vector<Measurable*> Scope::Subcompose(rt::Key slotId, std::function<void()> content) {
            State& st = m_State;
            Slot& slot = st.slots[slotId.value];
            if (!slot.holder) {
                slot.holder = std::make_unique<LayoutNode>();
                slot.holder->SetMeasurePolicy(std::make_shared<HolderPolicy>());
                slot.composition = std::make_unique<rt::Composition>(*slot.holder, rt::Dispatcher::Main());
                slot.composition->SetParentContext(st.context);
            }
            st.used.push_back(slotId.value);
            slot.composition->SetContent(std::move(content));
            slot.composition->Recompose();
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

    void SubcomposeLayout(const Modifier& modifier, SubcomposeMeasure measure, rt::Key key) {
        auto& comp = *rt::Composition::Current();
        auto& state = comp.Remember<std::shared_ptr<State>>([] { return std::make_shared<State>(); }, {}, key);
        state->measure = std::move(measure);
        state->context = comp.CurrentScope();
        auto st = state;
        comp.Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                              [&](LayoutNode& n) {
                                  n.SetModifier(modifier);
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
