#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Accessibility.h"
#include "kiln/ui/InsetsRulers.h"
#include "kiln/ui/MyersDiff.h"

#include <cmath>
#include "kiln/ui/NestedScroll.h"

#include <algorithm>

namespace kiln::ui {

    // --- ModifierNode ---------------------------------------------------------------------

    namespace {
        // The ancestors with a connection, nearest first, each with its node's alive token.
        struct Link { NestedScrollConnection* connection; std::weak_ptr<const char> alive; };
        std::vector<Link> Ancestors(NestedScrollNode* first) {
            std::vector<Link> chain;
            for (NestedScrollNode* n = first; n; n = n->NestedParent())
                if (n->connection) chain.push_back({ n->connection, n->AliveToken() });
            return chain;
        }
    }

    rt::Task<Offset> NestedScrollNode::DispatchPreFling(Offset v) {
        const auto chain = Ancestors(Parent());
        Offset consumed;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (it->alive.expired()) continue;
            consumed = consumed + co_await it->connection->OnPreFling(v - consumed);
        }
        co_return consumed;
    }

    rt::Task<Offset> NestedScrollNode::DispatchPostFling(Offset consumed, Offset available) {
        const auto chain = Ancestors(Parent());
        Offset total;
        for (const Link& link : chain) {
            if (link.alive.expired()) continue;
            const Offset own = co_await link.connection->OnPostFling(consumed, available);
            total = total + own;
            consumed = consumed + own;
            available = available - own;
        }
        co_return total;
    }

    NestedScrollNode* NestedScrollNode::Parent() const {
        // The nearest nested-scroll node before this one in the chain, else in an ancestor.
        LayoutNode* n = Owner();
        if (!n) return nullptr;
        NestedScrollNode* before = nullptr;
        for (auto& m : n->m_Nodes) {
            if (m.get() == this) break;
            if (auto* x = m->AsNestedScroll()) before = x;
        }
        if (before) return before;
        for (LayoutNode* p = n->m_Parent; p; p = p->m_Parent)
            for (auto it = p->m_Nodes.rbegin(); it != p->m_Nodes.rend(); ++it)
                if (auto* x = (*it)->AsNestedScroll()) return x;
        return nullptr;
    }

    void ModifierNode::InvalidateSemantics() {
        if (m_Owner && m_Owner->GetOwner()) m_Owner->GetOwner()->InvalidateSemantics();
    }

    void ModifierNode::DispatchOnScrollChanged(Offset delta) {
        if (m_Owner && m_Owner->GetOwner()) m_Owner->GetOwner()->DispatchOnScrollChanged(*m_Owner, delta);
    }

    void ModifierNode::InvalidateMeasure() { if (m_Owner) m_Owner->RequestRemeasure(); }
    void ModifierNode::InvalidateDraw() { if (m_Owner) m_Owner->InvalidateDraw(); }

    // --- Coordinators -------------------------------------------------------------------------

    // A coordinator's lookahead pass (LookaheadDelegate): its own measure result, constraints
    // and position, measuring and placing the lookahead delegates of what it wraps (or of the
    // children's outer coordinators).
    struct LayoutNode::LookaheadDelegate final : Measurable, Placeable {
        LayoutNode& node;
        std::size_t index;
        MeasureResult result;
        Constraints constraints;
        bool measured = false;
        IntOffset position;      // within the enclosing coordinator (0: in the parent)
        bool placed = false;
        std::vector<Measurable*> children;

        LookaheadDelegate(LayoutNode& n, std::size_t i) : node(n), index(i) {}
        LookaheadDelegate* Next() { return index + 1 < node.m_Coordinators.size() ? &node.La(index + 1) : nullptr; }
        IntSize Size() const { return { Width(), Height() }; }
        MeasureScope Scope() const { return MeasureScope(node.NodeDensity(), node.GetLayoutDirection(), true); }
        std::vector<Measurable*> ChildMeasurables() {
            std::vector<Measurable*> out;
            out.reserve(node.m_Children.size());
            for (LayoutNode* c : node.m_Children) out.push_back(&c->AsLookaheadMeasurable());
            return out;
        }
        Placeable& Measure(const Constraints& c) override;
        Placeable& MeasureHere(const Constraints& c);
        int Get(const AlignmentLine& line) override;
        void PlaceAt(IntOffset p, float z) override;
        int MinIntrinsicWidth(int h) override;
        int MaxIntrinsicWidth(int h) override;
        int MinIntrinsicHeight(int w) override;
        int MaxIntrinsicHeight(int w) override;
        const ParentData& GetParentData() const override { return node.m_ParentData; }
        void SetMeasuredSize(IntSize size, const Constraints& c) { SetMeasured(size, c); }
    };

    struct LayoutNode::Coordinator final : Measurable, Placeable {
        LayoutNode& node;
        std::size_t index;
        LayoutModifier* mod;                 // null for the inner coordinator
        std::vector<DrawModifier*> draws;    // drawn within this coordinator's size
        MeasureResult result;
        // Ruler values captured from the result, and the nodes that read each (or read the
        // default here at the top, for rulers nothing provided).
        std::vector<std::pair<const RulerData*, float>> rulerValues;
        std::vector<std::pair<const RulerData*, std::vector<std::pair<LayoutNode*, std::weak_ptr<const char>>>>> rulerReaders;
        IntOffset position;                  // within the enclosing coordinator (0: in parent)
        float zIndex = 0;                    // as placed by the enclosing coordinator (or parent)
        bool placed = false;                // enclosing layout modifier placed this coordinator
        // Lookahead: this coordinator's lookahead pass, and the approach pass's state (an approach
        // modifier that is not approaching measures what it wraps with the lookahead constraints
        // and places it at its lookahead offset).
        std::unique_ptr<LookaheadDelegate> la;
        bool forceLookaheadConstraints = false, forceLookaheadOffset = false, approachMeasureRequired = false;
        bool InLookahead() const { return node.m_Lookahead && la && la->measured; }

        // The list handed to the policy. It lives until the next measure, so a policy's placement
        // lambda may keep the span it was given (they usually do).
        std::vector<Measurable*> children;

        Coordinator(LayoutNode& n, std::size_t i, LayoutModifier* m) : node(n), index(i), mod(m) {}

        Coordinator* Next() { return index + 1 < node.m_Coordinators.size() ? node.m_Coordinators[index + 1].get() : nullptr; }

        std::vector<Measurable*> ChildMeasurables() {
            std::vector<Measurable*> out;
            out.reserve(node.m_Children.size());
            for (LayoutNode* c : node.m_Children) out.push_back(&c->AsMeasurable());
            return out;
        }

        Placeable& Measure(const Constraints& c) override {
            if (index == 0) return node.MeasureNode(c);
            return MeasureHere(forceLookaheadConstraints && la ? la->constraints : c);
        }

        Placeable& MeasureHere(const Constraints& c) {
            MeasureScope scope = node.Scope();
            ApproachLayoutModifier* approach = mod ? mod->AsApproach() : nullptr;
            if (approach && InLookahead()) {
                // LayoutModifierNodeCoordinator.measure in the approach pass.
                const IntSize lookaheadSize = la->Size();
                ApproachMeasureScope as(scope, la->constraints, lookaheadSize);
                approachMeasureRequired = approach->IsMeasurementApproachInProgress(lookaheadSize) || !(c == la->constraints);
                Coordinator* w = Next();
                if (!approachMeasureRequired) w->forceLookaheadConstraints = true;
                result = approach->ApproachMeasure(as, *w, c);
                w->forceLookaheadConstraints = false;
                const bool reachedLookaheadSize = result.width == la->Width() && result.height == la->Height();
                if (!approachMeasureRequired && w->la && w->Width() == w->la->Width() && w->Height() == w->la->Height() && !reachedLookaheadSize) {
                    result.width = la->Width();
                    result.height = la->Height();
                }
            } else if (mod) {
                result = mod->Measure(scope, *Next(), c);
            } else {
                children = ChildMeasurables();
                result = node.m_Policy ? node.m_Policy->Measure(scope, children, c) : MeasureResult{ c.minWidth, c.minHeight, {} };
            }
            SetMeasured({ result.width, result.height }, c);
            return *this;
        }

        // Placeable[line]: provided by this coordinator's result, else inherited from what it
        // wraps at its placed offset (running placement first, as Compose does when a parent
        // reads a line during measure).
        int Get(const AlignmentLine& line) override {
            if (index == 0) {
                const int v = GetHere(line);
                node.RecordLineRead(line, v);
                return v;
            }
            return GetHere(line);
        }

        int GetHere(const AlignmentLine& line) {
            for (const auto& [l, v] : result.alignmentLines)
                if (l == &line) return v;
            if (node.m_InMeasure) {
                // A modifier of this node reads a line of what it wraps while the node measures
                // (paddingFrom): the outer results are not ready, so lay out from here only —
                // this coordinator's placement places what it wraps (Compose lays children out
                // for alignment-line queries the same way).
                PlacementScope scope(node.GetLayoutDirection(), Width());
                const std::size_t i = index;
                LayoutNode* n = &node;
                scope.SetRulerLookup([n, i](const Ruler& r, float d) { return n->FindRulerValue(i, r, d); });
                if (node.m_Owner) scope.SetInsets(&node.m_Owner->Insets());
                if (result.place) result.place(scope);
            } else {
                node.LayoutIfNeeded();
            }
            if (mod) {
                Coordinator* w = Next();
                const int v = w->Get(line);
                if (v == AlignmentLine::Unspecified) return v;
                return v + (line.horizontal ? w->position.y : w->position.x);
            }
            int merged = AlignmentLine::Unspecified;
            for (LayoutNode* c : node.m_Children) {
                if (!c->m_Placed) continue;
                Coordinator& outer = *c->m_Coordinators.front();
                const int v = outer.Get(line);
                if (v == AlignmentLine::Unspecified) continue;
                const int at = v + (line.horizontal ? c->m_Position.y : c->m_Position.x);   // placed in this node
                merged = merged == AlignmentLine::Unspecified ? at : line.Combine(merged, at);
            }
            return merged;
        }

        void PlaceAt(IntOffset p, float z) override {
            if (index == 0 && InLookahead() && (node.IsOutMostLookaheadRoot() || !node.m_Lookahead->placed)) {
                // A lookahead root (or a node its parent did not place in lookahead) is placed in
                // lookahead where the main pass places it, first.
                la->PlaceAt(p, z);
            }
            placed = true;
            if (index == 0) {
                LayoutNode* parent = node.m_Parent;
                const bool moved = !node.m_Placed || node.m_Position != p || zIndex != z;
                if (node.m_Placed && node.m_Position != p) node.OnMoved();
                node.m_Position = p;
                zIndex = z;
                node.UpdateZIndex();
                node.m_PlaceOrder = parent ? parent->m_NextPlaceOrder++ : 0;
                node.m_Placed = true;
                node.m_PlacedThisPass = true;
                if (moved) node.InvalidateDraw();
                if (Owner* o = node.GetOwner(); o && (moved || node.Size() != node.m_ReportedSize)) {
                    node.m_ReportedSize = node.Size();
                    o->NotifyLayoutChange(node);
                }
                node.LayoutIfNeeded();
            } else {
                if (forceLookaheadOffset && la) p = la->position;
                if (position != p) {
                    if (Owner* o = node.GetOwner()) o->NotifyLayoutChange(node);
                    if (placed) node.OnMoved();
                }
                position = p;
                // A layout modifier's zIndex adds to the node's (Modifier.zIndex).
                if (zIndex != z) { zIndex = z; node.UpdateZIndex(); }
            }
        }

        int MinIntrinsicWidth(int h) override {
            MeasureScope s = node.Scope();
            if (mod && mod->AsApproach() && node.m_Lookahead) {
                if (!InLookahead()) return mod->AsApproach()->MinApproachIntrinsicWidth(nullptr, *Next(), h);
                ApproachMeasureScope as(s, la->constraints, la->Size());
                return mod->AsApproach()->MinApproachIntrinsicWidth(&as, *Next(), h);
            }
            if (mod) return mod->MinIntrinsicWidth(s, *Next(), h);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MinIntrinsicWidth(s, ch, h) : 0;
        }
        int MaxIntrinsicWidth(int h) override {
            MeasureScope s = node.Scope();
            if (mod && mod->AsApproach() && node.m_Lookahead) {
                if (!InLookahead()) return mod->AsApproach()->MaxApproachIntrinsicWidth(nullptr, *Next(), h);
                ApproachMeasureScope as(s, la->constraints, la->Size());
                return mod->AsApproach()->MaxApproachIntrinsicWidth(&as, *Next(), h);
            }
            if (mod) return mod->MaxIntrinsicWidth(s, *Next(), h);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MaxIntrinsicWidth(s, ch, h) : 0;
        }
        int MinIntrinsicHeight(int w) override {
            MeasureScope s = node.Scope();
            if (mod && mod->AsApproach() && node.m_Lookahead) {
                if (!InLookahead()) return mod->AsApproach()->MinApproachIntrinsicHeight(nullptr, *Next(), w);
                ApproachMeasureScope as(s, la->constraints, la->Size());
                return mod->AsApproach()->MinApproachIntrinsicHeight(&as, *Next(), w);
            }
            if (mod) return mod->MinIntrinsicHeight(s, *Next(), w);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MinIntrinsicHeight(s, ch, w) : 0;
        }
        int MaxIntrinsicHeight(int w) override {
            MeasureScope s = node.Scope();
            if (mod && mod->AsApproach() && node.m_Lookahead) {
                if (!InLookahead()) return mod->AsApproach()->MaxApproachIntrinsicHeight(nullptr, *Next(), w);
                ApproachMeasureScope as(s, la->constraints, la->Size());
                return mod->AsApproach()->MaxApproachIntrinsicHeight(&as, *Next(), w);
            }
            if (mod) return mod->MaxIntrinsicHeight(s, *Next(), w);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MaxIntrinsicHeight(s, ch, w) : 0;
        }
        const ParentData& GetParentData() const override { return node.m_ParentData; }
    };

    // --- LayoutNode ---------------------------------------------------------------------------

    LayoutNode::LayoutNode() { RebuildChain(); }

    LayoutNode::~LayoutNode() {
        // Children may already be destroyed (the composition frees a removed subtree bottom-up),
        // so only unhook this node itself.
        if (m_Virtual && m_Parent) std::erase(m_Parent->m_Virtuals, this);
        if (m_Owner) m_Owner->Forget(*this);
        for (auto& n : m_Nodes) if (n->m_Owner) { n->OnDetach(); n->m_Owner = nullptr; }
    }

    void LayoutNode::Watcher::OnStateChanged() {
        switch (phase) {
            case 0: node.RequestMainRemeasure(); break;
            case 1: node.RequestMainRelayout(); break;
            case 3: node.RequestLookaheadRemeasure(); break;
            case 4: node.RequestLookaheadRelayout(); break;
            default: node.InvalidateDraw(); break;
        }
    }

    MeasureScope LayoutNode::Scope() const {
        return MeasureScope(NodeDensity(), GetLayoutDirection());
    }

    Density LayoutNode::NodeDensity() const {
        if (m_LocalDensity) return *m_LocalDensity;
        return m_Owner ? m_Owner->GetDensity() : Density{};
    }

    void LayoutNode::SetLocalDensity(std::optional<Density> density) {
        if (density == m_LocalDensity) return;
        m_LocalDensity = density;
        RequestRemeasure();
        InvalidateDraw();
    }

    LayoutDirection LayoutNode::GetLayoutDirection() const {
        if (m_LocalDirection) return *m_LocalDirection;
        return m_Owner ? m_Owner->Direction() : LayoutDirection::Ltr;
    }

    void LayoutNode::SetLayoutDirection(std::optional<LayoutDirection> direction) {
        if (direction == m_LocalDirection) return;
        m_LocalDirection = direction;
        RequestRemeasure();
    }

    Measurable& LayoutNode::AsMeasurable() { return *m_Coordinators.front(); }
    Measurable& LayoutNode::AsLookaheadMeasurable() { return La(0); }

    LayoutNode::LookaheadDelegate& LayoutNode::La(std::size_t index) {
        auto& c = *m_Coordinators[index];
        if (!c.la) c.la = std::make_unique<LookaheadDelegate>(*this, index);
        return *c.la;
    }

    std::optional<IntSize> LayoutNode::LookaheadSize() const {
        const auto& la = m_Coordinators.front()->la;
        if (!m_Lookahead || !la || !la->measured) return std::nullopt;
        return la->Size();
    }

    std::optional<IntOffset> LayoutNode::LookaheadPosition() const {
        if (!m_Lookahead || !m_Lookahead->placed) return std::nullopt;
        return m_Lookahead->position;
    }

    LayoutCoordinates LayoutNode::CoordinatesOf(const ModifierNode* at) const {
        std::size_t ci = 0;
        if (at)
            for (const auto& node : m_Nodes) {
                if (node.get() == at) break;
                if (node->AsLayout()) ++ci;
            }
        return { const_cast<LayoutNode*>(this), ci, false };
    }

    IntSize LayoutNode::Size() const {
        const auto& c = *m_Coordinators.front();
        return { c.Width(), c.Height() };
    }

    namespace {
        gfx::Matrix MatMul(const gfx::Matrix& x, const gfx::Matrix& y) {
            return { x.a * y.a + x.c * y.b, x.b * y.a + x.d * y.b, x.a * y.c + x.c * y.d, x.b * y.c + x.d * y.d,
                     x.a * y.e + x.c * y.f + x.e, x.b * y.e + x.d * y.f + x.f };
        }
    }

    gfx::Matrix LayoutNode::ContentToRoot(bool inner) const {
        // Ancestors contribute their full chains (children sit in the innermost space); this node
        // its position and, for its own bounds, only the outer coordinator's layers.
        gfx::Matrix m = m_Parent ? m_Parent->ContentToRoot(true) : gfx::Matrix{};
        m = MatMul(m, gfx::Matrix::Translate(float(m_Position.x), float(m_Position.y)));
        const std::size_t count = inner ? m_Coordinators.size() : std::min<std::size_t>(1, m_Coordinators.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& c = *m_Coordinators[i];
            if (i > 0) m = MatMul(m, gfx::Matrix::Translate(float(c.position.x), float(c.position.y)));
            const ui::Size size{ float(c.Width()), float(c.Height()) };
            for (const DrawModifier* d : c.draws) {
                gfx::Matrix t;
                if (d->LayerTransform(size, t)) m = MatMul(m, t);
            }
        }
        return m;
    }

    namespace {
        using Mat3 = LayoutNode::Mat3;
        constexpr Mat3 kIdentity3{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        Mat3 Mul3(const Mat3& x, const Mat3& y) {
            Mat3 r{};
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r[i * 3 + j] = x[i * 3] * y[j] + x[i * 3 + 1] * y[3 + j] + x[i * 3 + 2] * y[6 + j];
            return r;
        }
        Mat3 Translate3(float x, float y) { return { 1, 0, x, 0, 1, y, 0, 0, 1 }; }
        Mat3 FromAffine(const gfx::Matrix& m) { return { m.a, m.c, m.e, m.b, m.d, m.f, 0, 0, 1 }; }
        // A draw modifier's layer transform, projective when it has one.
        bool LayerOf(const DrawModifier* d, ui::Size size, Mat3& out) {
            float h[9];
            if (d->LayerProjective(size, h)) { std::copy(h, h + 9, out.begin()); return true; }
            gfx::Matrix t;
            if (d->LayerTransform(size, t)) { out = FromAffine(t); return true; }
            return false;
        }
    }

    bool LayoutNode::IsModifierPlaced(const ModifierNode* at) const {
        for (const LayoutNode* n = this; n && n->m_Parent; n = n->m_Parent)
            if (!n->m_Placed) return false;
        std::size_t ci = 0;
        for (const auto& node : m_Nodes) {
            if (ci > 0 && !m_Coordinators[ci]->placed) return false;
            if (node.get() == at) return true;
            if (node->AsLayout()) ++ci;
        }
        return !at && (ci == 0 || m_Coordinators[ci]->placed);
    }

    void LayoutNode::HitSteps(const Mat3& parentInner, std::vector<HitStep>& out, Mat3& inner) const {
        Mat3 m = Mul3(parentInner, Translate3(float(m_Position.x), float(m_Position.y)));
        // Nodes up to and including a layout modifier sit in that modifier's coordinator.
        std::size_t ci = 0;
        auto sizeOf = [&](std::size_t i) { const auto& c = *m_Coordinators[i]; return ui::Size{ float(c.Width()), float(c.Height()) }; };
        for (const auto& node : m_Nodes) {
            if (ci > 0 && !m_Coordinators[ci]->placed) break;
            const ui::Size size = sizeOf(ci);
            if (auto* p = node->AsPointerInput()) out.push_back({ p, m, size });
            if (const DrawModifier* d = node->AsDraw()) {
                Mat3 t;
                if (LayerOf(d, size, t)) m = Mul3(m, t);
                if (node->ClipsChildren()) out.push_back({ nullptr, m, size, d });
            }
            if (node->AsLayout() && ci + 1 < m_Coordinators.size()) {
                ++ci;
                const auto& c = *m_Coordinators[ci];
                m = Mul3(m, Translate3(float(c.position.x), float(c.position.y)));
            }
        }
        inner = m;
    }

    bool LayoutNode::PointerSpace(const PointerInputModifier* mod, Mat3& toRoot, ui::Size& size) const {
        std::vector<const LayoutNode*> chain;
        for (const LayoutNode* n = this; n; n = n->m_Parent) chain.push_back(n);
        Mat3 m = kIdentity3;
        std::vector<HitStep> steps;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            steps.clear();
            Mat3 inner;
            (*it)->HitSteps(m, steps, inner);
            m = inner;
        }
        for (const HitStep& s : steps)
            if (s.mod == mod) { toRoot = s.toRoot; size = s.size; return true; }
        return false;
    }

    namespace {
        Rect MapBox(const Mat3& m, const Rect& r) {
            Rect out{ INFINITY, INFINITY, -INFINITY, -INFINITY };
            for (const Offset q : { Offset{ r.left, r.top }, Offset{ r.right, r.top }, Offset{ r.left, r.bottom }, Offset{ r.right, r.bottom } }) {
                const float w = m[6] * q.x + m[7] * q.y + m[8];
                if (w <= 1e-6f) continue;
                const float x = (m[0] * q.x + m[1] * q.y + m[2]) / w, y = (m[3] * q.x + m[4] * q.y + m[5]) / w;
                out = { std::min(out.left, x), std::min(out.top, y), std::max(out.right, x), std::max(out.bottom, y) };
            }
            return out.left > out.right ? Rect{} : out;
        }
        // The parent's children space (root <- parent inner).
        Mat3 ParentInner(const LayoutNode* parent) {
            Mat3 m = kIdentity3;
            std::vector<const LayoutNode*> chain;
            for (const LayoutNode* n = parent; n; n = n->Parent()) chain.push_back(n);
            std::vector<LayoutNode::HitStep> steps;
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                steps.clear();
                Mat3 inner;
                (*it)->HitSteps(m, steps, inner);
                m = inner;
            }
            return m;
        }
    }

    LayoutNode::Mat3 LayoutNode::SpaceToRoot(const ModifierNode* at, ui::Size* sizeOut) const {
        Mat3 m = Mul3(ParentInner(m_Parent), Translate3(float(m_Position.x), float(m_Position.y)));
        if (at) {
            std::size_t ci = 0;
            for (const auto& node : m_Nodes) {
                const auto& c = *m_Coordinators[ci];
                const ui::Size size{ float(c.Width()), float(c.Height()) };
                if (node.get() == at) {
                    if (sizeOut) *sizeOut = size;
                    return m;
                }
                if (const DrawModifier* d = node->AsDraw()) {
                    Mat3 t;
                    if (LayerOf(d, size, t)) m = Mul3(m, t);
                }
                if (node->AsLayout() && ci + 1 < m_Coordinators.size()) {
                    ++ci;
                    const auto& n = *m_Coordinators[ci];
                    m = Mul3(m, Translate3(float(n.position.x), float(n.position.y)));
                }
            }
            m = Mul3(ParentInner(m_Parent), Translate3(float(m_Position.x), float(m_Position.y)));
        }
        const IntSize sz = Size();
        if (!m_Coordinators.empty()) {
            const ui::Size size{ float(sz.width), float(sz.height) };
            for (const DrawModifier* d : m_Coordinators.front()->draws) {
                Mat3 t;
                if (LayerOf(d, size, t)) m = Mul3(m, t);
            }
        }
        if (sizeOut) *sizeOut = { float(sz.width), float(sz.height) };
        return m;
    }

    LayoutNode::Mat3 LayoutNode::InnerToRoot() const {
        std::vector<HitStep> steps;
        Mat3 inner;
        HitSteps(ParentInner(m_Parent), steps, inner);
        return inner;
    }

    Rect LayoutNode::InnerRectInRoot() const {
        const auto& c = *m_Coordinators.back();
        return MapBox(InnerToRoot(), { 0, 0, float(c.Width()), float(c.Height()) });
    }

    IntSize LayoutNode::InnerSize() const {
        const auto& c = *m_Coordinators.back();
        return { c.Width(), c.Height() };
    }

    Offset LayoutNode::MapPoint(const Mat3& m, Offset p) {
        const float w = m[6] * p.x + m[7] * p.y + m[8];
        return { (m[0] * p.x + m[1] * p.y + m[2]) / w, (m[3] * p.x + m[4] * p.y + m[5]) / w };
    }

    std::optional<Offset> LayoutNode::UnmapPoint(const Mat3& m, Offset p) {
        const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
        const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
        const double det = a * A + b * B + c * C;
        if (std::abs(det) < 1e-12) return std::nullopt;
        const double inv[9] = { A / det, -(b * i - c * h) / det, (b * f - c * e) / det,
                                B / det, (a * i - c * g) / det, -(a * f - c * d) / det,
                                C / det, -(a * h - b * g) / det, (a * e - b * d) / det };
        const double w = inv[6] * p.x + inv[7] * p.y + inv[8];
        if (w <= 1e-9) return std::nullopt;
        return Offset{ float((inv[0] * p.x + inv[1] * p.y + inv[2]) / w), float((inv[3] * p.x + inv[4] * p.y + inv[5]) / w) };
    }

    Rect LayoutNode::RectInRoot(std::optional<Rect> local, const ModifierNode* at) const {
        ui::Size size;
        const Mat3 m = SpaceToRoot(at, &size);
        return MapBox(m, local.value_or(Rect{ 0, 0, size.width, size.height }));
    }

    Rect LayoutNode::LayoutModifierRectInRoot(const LayoutModifier* modifier) const {
        // The walk of HitSteps, stopping after the modifier's coordinator (its layers included).
        Mat3 m = Mul3(ParentInner(m_Parent), Translate3(float(m_Position.x), float(m_Position.y)));
        std::size_t ci = 0;
        for (const auto& node : m_Nodes) {
            const auto& c = *m_Coordinators[ci];
            const ui::Size size{ float(c.Width()), float(c.Height()) };
            if (const DrawModifier* d = node->AsDraw()) {
                Mat3 t;
                if (LayerOf(d, size, t)) m = Mul3(m, t);
            }
            if (auto* l = node->AsLayout()) {
                if (l == modifier) return MapBox(m, { 0, 0, size.width, size.height });
                if (ci + 1 < m_Coordinators.size()) {
                    ++ci;
                    const auto& n = *m_Coordinators[ci];
                    m = Mul3(m, Translate3(float(n.position.x), float(n.position.y)));
                }
            }
        }
        return RectInRoot(std::nullopt);
    }

    std::vector<LayoutNode*> LayoutNode::ZSortedChildren() const {
        std::vector<LayoutNode*> v(Children().begin(), Children().end());
        std::stable_sort(v.begin(), v.end(), [](const LayoutNode* a, const LayoutNode* b) {
            if (a->ZIndex() != b->ZIndex()) return a->ZIndex() < b->ZIndex();
            return a->PlaceOrder() < b->PlaceOrder();
        });
        return v;
    }

    Offset LayoutNode::PositionInRoot() const {
        Offset o;
        for (const LayoutNode* n = this; n; n = n->m_Parent) {
            o = o + Offset{ float(n->m_Position.x), float(n->m_Position.y) };
            if (n->m_Parent)
                for (std::size_t i = 1; i < n->m_Parent->m_Coordinators.size(); ++i) {
                    const auto& c = *n->m_Parent->m_Coordinators[i];
                    o = o + Offset{ float(c.position.x), float(c.position.y) };
                }
        }
        return o;
    }

    void LayoutNode::SetMeasurePolicy(std::shared_ptr<MeasurePolicy> policy) {
        if (policy == m_Policy) return;
        m_Policy = std::move(policy);
        RequestRemeasure();
    }

    void LayoutNode::SetModifier(const Modifier& modifier) {
        const auto& next = modifier.Elements();
        bool structural = next.size() != m_Elements.size();
        bool updated = false;
        std::vector<std::unique_ptr<ModifierNode>> nodes;
        nodes.reserve(next.size());
        auto focusKind = [](ModifierNode& m) { return m.AsFocusTarget() || m.AsFocusEvent() || m.AsFocusProperties(); };
        bool focusChanged = false;
        // NodeChain.updateFrom: which old node each new element keeps (-1: a new node). With
        // equal lengths the chains match position by position up to the first change of type;
        // from there (or from the start, when the lengths differ) a Myers diff matches nodes of
        // the same type, so an element added or removed in the middle keeps the nodes around it.
        const auto sameType = [&](std::size_t o, std::size_t n) { return m_Nodes[o] && m_Elements[o]->Type() == next[n]->Type(); };
        std::vector<int> from(next.size(), -1);
        std::size_t prefix = 0;
        if (next.size() == m_Elements.size())
            for (; prefix < next.size() && sameType(prefix, prefix); ++prefix) from[prefix] = int(prefix);
        if (prefix < next.size() || prefix < m_Elements.size()) {
            ExecuteDiff(int(m_Elements.size() - prefix), int(next.size() - prefix),
                        [&](int o, int n) { return sameType(prefix + std::size_t(o), prefix + std::size_t(n)); },
                        [&](int o, int n) { from[prefix + std::size_t(n)] = int(prefix) + o; });
        }
        std::vector<char> kept(m_Nodes.size(), 0);
        for (std::size_t n = 0; n < next.size(); ++n) {
            if (from[n] < 0) continue;
            kept[std::size_t(from[n])] = 1;
            if (from[n] != int(n)) structural = true;
        }
        // Detach against the complete old chain. Focus removal walks that chain to cancel
        // indirect input and notify listeners; moving reusable nodes first leaves null holes.
        for (std::size_t i = 0; i < m_Nodes.size(); ++i) {
            if (kept[i]) continue;
            auto& old = m_Nodes[i];
            if (!old) continue;
            if (focusKind(*old)) focusChanged = true;
            if (m_Owner) {
                if (auto* p = old->AsPointerInput()) m_Owner->ForgetPointerModifier(p);
                old->OnDetach();
            }
            old->m_Owner = nullptr;
        }
        for (std::size_t n = 0; n < next.size(); ++n) {
            if (from[n] < 0) continue;
            const std::size_t o = std::size_t(from[n]);
            if (!m_Elements[o]->Equals(*next[n])) {
                next[n]->Update(*m_Nodes[o]);
                updated = true;
                if (focusKind(*m_Nodes[o])) focusChanged = true;
            }
        }
        std::vector<ModifierNode*> attach;
        for (std::size_t i = 0; i < next.size(); ++i) {
            const ModifierElement& e = *next[i];
            if (from[i] >= 0) {
                nodes.push_back(std::move(m_Nodes[std::size_t(from[i])]));
            } else {
                structural = true;
                auto n = e.Create();
                if (focusKind(*n)) focusChanged = true;
                n->m_Owner = this;
                attach.push_back(n.get());
                nodes.push_back(std::move(n));
            }
        }
        m_Nodes = std::move(nodes);
        m_Elements = next;
        if (structural) RebuildChain();
        // Attach only after the new chain and its coordinators are available to callbacks.
        if (m_Owner) for (auto* node : attach) node->OnAttach();
        if (m_Owner && focusChanged) m_Owner->ScheduleFocusInvalidation(*this);
        if (m_Owner && (structural || updated)) m_Owner->TrackPositionAware(*this);
        if (m_Owner && (structural || updated)) m_Owner->InvalidateSemantics();
        // Parent data may have changed: the parent measures with it.
        ParentData pd;
        for (auto& n : m_Nodes) if (auto* p = n->AsParentData()) p->ModifyParentData(pd);
        m_ParentData = std::move(pd);
        if (m_Parent) m_Parent->RequestRemeasure();
        RequestRemeasure();
        InvalidateDraw();
    }

    float LayoutNode::ZIndex() const {
        if (!m_ZIndexFromChildren) return m_ZIndex;
        float z = m_ZIndex;
        bool any = false;
        for (const LayoutNode* ch : m_Children)
            if (ch->m_Placed) { z = any ? std::max(z, ch->ZIndex()) : ch->ZIndex(); any = true; }
        return z;
    }

    // The node's draw order among its siblings: the sum of every placement zIndex in its chain.
    void LayoutNode::UpdateZIndex() {
        float sum = 0;
        for (const auto& c : m_Coordinators) sum += c->zIndex;
        if (sum == m_ZIndex) return;
        m_ZIndex = sum;
        if (m_Parent) m_Parent->InvalidateDraw();
    }

    void LayoutNode::RebuildChain() {
        std::vector<std::unique_ptr<Coordinator>> chain;
        std::vector<DrawModifier*> pending;
        (void)0;
        for (auto& n : m_Nodes) {
            if (auto* d = n->AsDraw()) pending.push_back(d);
            if (auto* l = n->AsLayout()) {
                chain.push_back(std::make_unique<Coordinator>(*this, chain.size(), l));
                chain.back()->draws = std::move(pending);
                pending.clear();
            }
        }
        chain.push_back(std::make_unique<Coordinator>(*this, chain.size(), nullptr));
        chain.back()->draws = std::move(pending);
        m_Coordinators = std::move(chain);
        m_Measured = false;
        m_MeasureDirty = true;
        if (m_Lookahead) { m_Lookahead->measured = false; m_Lookahead->measureDirty = m_Lookahead->layoutDirty = true; }
        // An approach modifier may have come or gone: it makes a node outside a scope a root.
        if (m_Owner) UpdateLookaheadRoot();
    }

    void LayoutNode::SetChildren(std::vector<rt::TreeNode*> children) {
        if (m_MeasureManaged) return;
        m_Composed = std::move(children);
        if (m_Virtual) {
            if (m_Parent) m_Parent->Refold();
            return;
        }
        Refold();
    }

    void LayoutNode::Fold(std::vector<rt::TreeNode*>& composed, LayoutNode* from, std::vector<LayoutNode*>& out) {
        for (rt::TreeNode* t : composed) {
            auto* c = static_cast<LayoutNode*>(t);
            LayoutNode* folded = from == this ? nullptr : from;
            const bool refolded = c->m_FoldedParent != folded;
            c->m_FoldedParent = folded;
            if (c->m_Virtual) {
                if (c->m_Parent != this) {
                    if (c->m_Parent) std::erase(c->m_Parent->m_Virtuals, c);
                    c->m_Parent = this;
                    m_Virtuals.push_back(c);
                    if (m_Owner) c->Attach(*m_Owner, m_Depth + 1);
                } else if (refolded && m_Owner) {
                    c->UpdateLookaheadRoot();
                }
                Fold(c->m_Composed, c, out);
            } else {
                if (refolded && c->m_Parent == this && m_Owner) c->UpdateLookaheadRoot();
                out.push_back(c);
            }
        }
    }

    void LayoutNode::Refold() {
        std::vector<LayoutNode*> next;
        next.reserve(m_Composed.size());
        std::vector<LayoutNode*> virtualsBefore = m_Virtuals;
        Fold(m_Composed, this, next);
        // Virtual nodes no longer composed here leave.
        for (LayoutNode* v : virtualsBefore) {
            bool kept = false;
            std::function<bool(const std::vector<rt::TreeNode*>&)> has = [&](const std::vector<rt::TreeNode*>& list) {
                for (rt::TreeNode* t : list) {
                    auto* c = static_cast<LayoutNode*>(t);
                    if (c == v) return true;
                    if (c->m_Virtual && has(c->m_Composed)) return true;
                }
                return false;
            };
            kept = has(m_Composed);
            if (!kept) {
                std::erase(m_Virtuals, v);
                v->Detach();
                v->m_Parent = nullptr;
            }
        }
        if (next == m_Children) return;
        for (LayoutNode* old : m_Children)
            if (std::find(next.begin(), next.end(), old) == next.end()) {
                old->Detach();
                old->m_Parent = nullptr;
            }
        for (LayoutNode* c : next) {
            if (c->m_Parent != this) {
                c->m_Parent = this;
                if (m_Owner) c->Attach(*m_Owner, m_Depth + 1);
            }
        }
        m_Children = std::move(next);
        RequestRemeasure();
        if (m_Owner) m_Owner->InvalidateSemantics();
    }

    void LayoutNode::SetChildrenDuringMeasure(std::vector<LayoutNode*> next) {
        if (next == m_Children) return;
        for (LayoutNode* old : m_Children)
            if (std::find(next.begin(), next.end(), old) == next.end()) { old->Detach(); old->m_Parent = nullptr; }
        for (LayoutNode* c : next)
            if (c->m_Parent != this) { c->m_Parent = this; if (m_Owner) c->Attach(*m_Owner, m_Depth + 1); }
        m_Children = std::move(next);
        InvalidateDraw();
    }

    void LayoutNode::ForceRemeasure() {
        if (!m_Measured) { RequestRemeasure(); return; }
        if (m_Lookahead && m_Lookahead->measured && !IsOutMostLookaheadRoot()) {
            m_Lookahead->measureDirty = true;
            LookaheadMeasureNode(m_Lookahead->constraints);
        }
        m_MeasureDirty = true;
        const IntSize before = Size();
        MeasureNode(m_LastConstraints);
        if (before != Size() && m_Parent) m_Parent->RequestRemeasure();
        LayoutChain();
    }

    void LayoutNode::Attach(Owner& owner, int depth) {
        if (m_Virtual) {
            // Takes no part in layout: only its lookahead root (for its folded children).
            m_Owner = &owner;
            m_Depth = depth;
            UpdateLookaheadRoot();
            return;
        }
        if (m_Owner) owner.Forget(*this);
        const bool fresh = m_Owner == nullptr;
        m_Owner = &owner;
        if (m_SemanticsId == 0) m_SemanticsId = owner.NextSemanticsId();
        m_Depth = depth;
        if (fresh) for (auto& n : m_Nodes) n->OnAttach();
        if (fresh)
            for (auto& n : m_Nodes)
                if (n->AsFocusTarget() || n->AsFocusEvent()) { owner.ScheduleFocusInvalidation(*this); break; }
        owner.TrackPositionAware(*this);
        for (LayoutNode* v : m_Virtuals) v->Attach(owner, depth + 1);
        UpdateLookaheadRoot();
        for (LayoutNode* c : m_Children) c->Attach(owner, depth + 1);
        m_MeasureDirty = true;
        owner.OnMeasureRequested(*this);
    }

    void LayoutNode::Detach() {
        if (m_Virtual) { m_Owner = nullptr; return; }
        for (LayoutNode* c : m_Children) c->Detach();
        for (LayoutNode* v : m_Virtuals) v->Detach();
        if (m_Owner) {
            m_Owner->Forget(*this);
            for (auto& n : m_Nodes) n->OnDetach();
        }
        m_MeasureWatch.Unsubscribe();
        m_LayoutWatch.Unsubscribe();
        m_DrawWatch.Unsubscribe();
        m_LookaheadMeasureWatch.Unsubscribe();
        m_LookaheadLayoutWatch.Unsubscribe();
        m_Owner = nullptr;
        m_Placed = false;
        if (m_Lookahead) { m_Lookahead->placed = false; m_Lookahead->measureDirty = m_Lookahead->layoutDirty = true; }
    }

    bool LayoutNode::HasApproachModifier() const {
        for (const auto& c : m_Coordinators)
            if (c->mod && c->mod->AsApproach()) return true;
        return false;
    }

    void LayoutNode::UpdateLookaheadRoot() {
        LayoutNode* root = nullptr;
        if (m_IsVirtualLookaheadRoot) root = this;
        else if (LayoutNode* f = FoldedParent()) root = f->m_LookaheadRoot;
        if (!root && !m_Virtual && HasApproachModifier()) root = this;
        if (root == m_LookaheadRoot) return;
        m_LookaheadRoot = root;
        if (root && !m_Virtual) {
            if (!m_Lookahead) m_Lookahead = std::make_unique<LookaheadState>();
        } else {
            m_Lookahead.reset();
            for (auto& c : m_Coordinators) c->la.reset();
        }
        if (!m_Virtual) RequestRemeasure();
        // Down the composed tree (virtual nodes first hand it to their folded children).
        if (m_Virtual || !m_MeasureManaged) {
            for (rt::TreeNode* t : m_Composed) {
                auto* c = static_cast<LayoutNode*>(t);
                if (c->m_Owner) c->UpdateLookaheadRoot();
            }
        }
        if (!m_Virtual && m_MeasureManaged)
            for (LayoutNode* c : m_Children) if (c->m_Owner) c->UpdateLookaheadRoot();
    }

    void LayoutNode::RequestRemeasure() {
        if (m_Lookahead) m_Lookahead->measureDirty = m_Lookahead->layoutDirty = true;
        RequestMainRemeasure();
    }

    void LayoutNode::RequestRelayout() {
        if (m_Lookahead) m_Lookahead->layoutDirty = true;
        RequestMainRelayout();
    }

    void LayoutNode::RequestLookaheadRemeasure() { RequestRemeasure(); }
    void LayoutNode::RequestLookaheadRelayout() { RequestRelayout(); }

    void LayoutNode::RequestMainRemeasure() {
        if (m_Virtual) return;
        m_MeasureDirty = true;
        m_LayoutDirty = true;
        if (m_Owner) m_Owner->OnMeasureRequested(*this);
    }

    void LayoutNode::RequestMainRelayout() {
        if (m_Virtual) return;
        m_LayoutDirty = true;
        if (m_Owner) m_Owner->OnLayoutRequested(*this);
    }

    void LayoutNode::OnMoved() {
        if (!m_Owner || m_Owner->m_CoordinateReaders.empty()) return;
        for (LayoutNode* reader : m_Owner->m_CoordinateReaders)
            for (const LayoutNode* n = reader; n; n = n->m_Parent)
                if (n == this) { reader->RequestRelayout(); break; }
    }

    void LayoutNode::InvalidateDraw() {
        if (m_Owner) m_Owner->OnDrawRequested();
    }

    Placeable& LayoutNode::MeasureNode(const Constraints& c) {
        // MeasurePassDelegate.measure: the outermost lookahead root measures its lookahead first.
        if (IsOutMostLookaheadRoot()) LookaheadMeasureNode(c);
        Coordinator& outer = *m_Coordinators.front();
        if (m_Measured && !m_MeasureDirty && c == m_LastConstraints) return outer;
        const IntSize before = Size();
        m_InMeasure = true;
        rt::Observation::Track(m_MeasureWatch, [&] { outer.MeasureHere(c); });
        m_InMeasure = false;
        m_LastConstraints = c;
        m_Measured = true;
        m_MeasureDirty = false;
        m_LayoutDirty = true;
        if (m_Owner) {
            m_Owner->m_MeasureDirty.erase(this);
            m_Owner->m_LayoutDirty.insert(this);
        }
        if (before != Size()) InvalidateDraw();
        return outer;
    }

    void LayoutNode::LayoutChain() {
        // The main placement depends on the lookahead one (lookahead offsets).
        if (m_Lookahead && m_Lookahead->placed) LookaheadLayoutIfNeeded();
        m_LayoutDirty = false;
        if (m_Owner) m_Owner->m_LayoutDirty.erase(this);
        m_NextPlaceOrder = 0;
        const LayoutDirection dir = GetLayoutDirection();
        for (LayoutNode* c : m_Children) c->m_PlacedThisPass = false;
        const bool innerWasPlaced = m_Coordinators.back()->placed;
        for (std::size_t i = 1; i < m_Coordinators.size(); ++i) m_Coordinators[i]->placed = false;
        m_InLayout = true;
        bool readCoordinates = false;
        rt::Observation::Track(m_LayoutWatch, [&] {
            for (std::size_t i = 0; i < m_Coordinators.size(); ++i) {
                auto& c = m_Coordinators[i];
                // A modifier may deliberately skip its wrapped placeable (FAB visibility at
                // zero alpha). Do not run stale inner placement results from an earlier frame.
                if (i > 0 && !c->placed) break;
                CaptureRulers(i);
                PlacementScope scope(dir, c->Width());
                scope.SetRulerLookup([this, i](const Ruler& r, float d) { return FindRulerValue(i, r, d); });
                if (m_Owner) scope.SetInsets(&m_Owner->Insets());
                scope.SetCoordinates({ this, i, false });
                // LayoutModifierNodeCoordinator.onAfterPlaceAt: an approach that is complete
                // places what it wraps at its lookahead offset.
                Coordinator* next = i + 1 < m_Coordinators.size() ? m_Coordinators[i + 1].get() : nullptr;
                ApproachLayoutModifier* approach = c->mod ? c->mod->AsApproach() : nullptr;
                if (approach && next && c->InLookahead()) {
                    const bool complete = !approach->IsPlacementApproachInProgress(scope, { this, i, true }) && !c->approachMeasureRequired &&
                                          c->Width() == c->la->Width() && c->Height() == c->la->Height() && next->la &&
                                          next->Width() == next->la->Width() && next->Height() == next->la->Height();
                    next->forceLookaheadOffset = complete;
                }
                if (c->result.place) c->result.place(scope);
                if (next) next->forceLookaheadOffset = false;
                readCoordinates = readCoordinates || scope.CoordinatesRead();
            }
        });
        m_InLayout = false;
        m_ReadsCoordinates = readCoordinates;
        if (m_Owner) {
            if (m_ReadsCoordinates) m_Owner->m_CoordinateReaders.insert(this);
            else if (!m_Lookahead || !m_Lookahead->readsCoordinates) m_Owner->m_CoordinateReaders.erase(this);
        }
        if (m_Owner && innerWasPlaced != m_Coordinators.back()->placed) m_Owner->InvalidateSemantics();
        // Children this pass did not place stop drawing and hit testing (markNodeAndSubtreeAsNotPlaced).
        for (LayoutNode* c : m_Children)
            if (!c->m_PlacedThisPass && c->m_Placed) { c->m_Placed = false; InvalidateDraw(); }
        InvalidateDraw();
        CheckLinesReadByParent();
    }

    // --- Lookahead -----------------------------------------------------------------------------

    Placeable& LayoutNode::LookaheadMeasureNode(const Constraints& c) {
        if (!m_Lookahead) m_Lookahead = std::make_unique<LookaheadState>();
        LookaheadState& l = *m_Lookahead;
        LookaheadDelegate& outer = La(0);
        if (l.measured && !l.measureDirty && c == l.constraints) return outer;
        l.inMeasure = true;
        rt::Observation::Track(m_LookaheadMeasureWatch, [&] { outer.MeasureHere(c); });
        l.inMeasure = false;
        l.constraints = c;
        l.measured = true;
        l.measureDirty = false;
        l.layoutDirty = true;
        // The main pass follows (LookaheadPassDelegate.performMeasure marks it pending).
        m_MeasureDirty = true;
        m_LayoutDirty = true;
        if (m_Owner) {
            m_Owner->m_MeasureDirty.insert(this);
            m_Owner->m_LayoutDirty.insert(this);
        }
        return outer;
    }

    void LayoutNode::LookaheadLayoutIfNeeded() {
        if (m_Lookahead && m_Lookahead->layoutDirty) LookaheadLayoutChain();
    }

    void LayoutNode::LookaheadLayoutChain() {
        LookaheadState& l = *m_Lookahead;
        l.layoutDirty = false;
        l.nextPlaceOrder = 0;
        const LayoutDirection dir = GetLayoutDirection();
        for (LayoutNode* c : m_Children) if (c->m_Lookahead) c->m_Lookahead->placedThisPass = false;
        for (std::size_t i = 1; i < m_Coordinators.size(); ++i) if (m_Coordinators[i]->la) m_Coordinators[i]->la->placed = false;
        bool readCoordinates = false;
        rt::Observation::Track(m_LookaheadLayoutWatch, [&] {
            for (std::size_t i = 0; i < m_Coordinators.size(); ++i) {
                LookaheadDelegate& d = La(i);
                if (!d.measured || (i > 0 && !d.placed)) break;
                PlacementScope scope(dir, d.Width());
                // Rulers: the main pass's values (Compose keeps lookahead ones apart).
                scope.SetRulerLookup([this, i](const Ruler& r, float v) { return FindRulerValue(i, r, v); });
                if (m_Owner) scope.SetInsets(&m_Owner->Insets());
                scope.SetCoordinates({ this, i, true });
                if (d.result.place) d.result.place(scope);
                readCoordinates = readCoordinates || scope.CoordinatesRead();
            }
        });
        l.readsCoordinates = readCoordinates;
        if (m_Owner) {
            if (readCoordinates) m_Owner->m_CoordinateReaders.insert(this);
            else if (!m_ReadsCoordinates) m_Owner->m_CoordinateReaders.erase(this);
        }
        for (LayoutNode* c : m_Children)
            if (c->m_Lookahead && !c->m_Lookahead->placedThisPass) c->m_Lookahead->placed = false;
        // The main placement reads the lookahead one.
        RequestMainRelayout();
    }

    Placeable& LayoutNode::LookaheadDelegate::Measure(const Constraints& c) {
        if (index == 0) return node.LookaheadMeasureNode(c);
        return MeasureHere(c);
    }

    Placeable& LayoutNode::LookaheadDelegate::MeasureHere(const Constraints& c) {
        MeasureScope scope = Scope();
        Coordinator& co = *node.m_Coordinators[index];
        if (co.mod) {
            result = co.mod->Measure(scope, *Next(), c);
        } else {
            children = ChildMeasurables();
            result = node.m_Policy ? node.m_Policy->Measure(scope, children, c) : MeasureResult{ c.minWidth, c.minHeight, {} };
        }
        constraints = c;
        measured = true;
        SetMeasured({ result.width, result.height }, c);
        return *this;
    }

    int LayoutNode::LookaheadDelegate::Get(const AlignmentLine& line) {
        if (!measured) return AlignmentLine::Unspecified;
        for (const auto& [l, v] : result.alignmentLines)
            if (l == &line) return v;
        if (node.m_Lookahead && node.m_Lookahead->inMeasure) {
            PlacementScope scope(node.GetLayoutDirection(), Width());
            const std::size_t i = index;
            LayoutNode* n = &node;
            scope.SetRulerLookup([n, i](const Ruler& r, float d) { return n->FindRulerValue(i, r, d); });
            scope.SetCoordinates({ n, i, true });
            if (result.place) result.place(scope);
        } else {
            node.LookaheadLayoutIfNeeded();
        }
        if (node.m_Coordinators[index]->mod) {
            LookaheadDelegate* w = Next();
            const int v = w->Get(line);
            if (v == AlignmentLine::Unspecified) return v;
            return v + (line.horizontal ? w->position.y : w->position.x);
        }
        int merged = AlignmentLine::Unspecified;
        for (LayoutNode* c : node.m_Children) {
            if (!c->m_Lookahead || !c->m_Lookahead->placed) continue;
            const int v = c->La(0).Get(line);
            if (v == AlignmentLine::Unspecified) continue;
            const int at = v + (line.horizontal ? c->m_Lookahead->position.y : c->m_Lookahead->position.x);
            merged = merged == AlignmentLine::Unspecified ? at : line.Combine(merged, at);
        }
        return merged;
    }

    void LayoutNode::LookaheadDelegate::PlaceAt(IntOffset p, float) {
        placed = true;
        if (index == 0) {
            if (!node.m_Lookahead) node.m_Lookahead = std::make_unique<LookaheadState>();
            LookaheadState& l = *node.m_Lookahead;
            const bool moved = l.placed && l.position != p;
            l.position = p;
            l.placed = true;
            l.placedThisPass = true;
            if (LayoutNode* parent = node.m_Parent; parent && parent->m_Lookahead) l.placeOrder = parent->m_Lookahead->nextPlaceOrder++;
            position = p;
            if (moved) node.OnMoved();
            node.LookaheadLayoutIfNeeded();
        } else {
            if (position != p) node.OnMoved();
            position = p;
        }
    }

    int LayoutNode::LookaheadDelegate::MinIntrinsicWidth(int h) {
        MeasureScope s = Scope();
        if (auto* m = node.m_Coordinators[index]->mod) return m->MinIntrinsicWidth(s, *Next(), h);
        auto ch = ChildMeasurables();
        return node.m_Policy ? node.m_Policy->MinIntrinsicWidth(s, ch, h) : 0;
    }
    int LayoutNode::LookaheadDelegate::MaxIntrinsicWidth(int h) {
        MeasureScope s = Scope();
        if (auto* m = node.m_Coordinators[index]->mod) return m->MaxIntrinsicWidth(s, *Next(), h);
        auto ch = ChildMeasurables();
        return node.m_Policy ? node.m_Policy->MaxIntrinsicWidth(s, ch, h) : 0;
    }
    int LayoutNode::LookaheadDelegate::MinIntrinsicHeight(int w) {
        MeasureScope s = Scope();
        if (auto* m = node.m_Coordinators[index]->mod) return m->MinIntrinsicHeight(s, *Next(), w);
        auto ch = ChildMeasurables();
        return node.m_Policy ? node.m_Policy->MinIntrinsicHeight(s, ch, w) : 0;
    }
    int LayoutNode::LookaheadDelegate::MaxIntrinsicHeight(int w) {
        MeasureScope s = Scope();
        if (auto* m = node.m_Coordinators[index]->mod) return m->MaxIntrinsicHeight(s, *Next(), w);
        auto ch = ChildMeasurables();
        return node.m_Policy ? node.m_Policy->MaxIntrinsicHeight(s, ch, w) : 0;
    }

    LayoutNode::LookaheadOffset LayoutNode::LookaheadOffsetOf(std::size_t coordinator) const {
        IntOffset offset;
        const LayoutNode* n = this;
        std::size_t k = coordinator;
        while (true) {
            for (std::size_t i = k; i >= 1; --i) {
                const auto& c = *n->m_Coordinators[i];
                offset = offset + (c.la && c.la->measured ? c.la->position : c.position);
            }
            offset = offset + (n->m_Lookahead && n->m_Lookahead->placed ? n->m_Lookahead->position : n->m_Position);
            if (n->IsOutMostLookaheadRoot() || !n->m_Parent || !n->m_LookaheadRoot) return { n, offset };
            n = n->m_Parent;
            k = n->m_Coordinators.size() - 1;
        }
    }

    namespace {
        Offset ToOffset(IntOffset p) { return { float(p.x), float(p.y) }; }
    }

    bool LayoutCoordinates::IsAttached() const {
        return node && (!tracked || !alive.expired()) && node->GetOwner() && node->IsPlaced();
    }

    IntSize LayoutCoordinates::Size() const {
        if (!node || (tracked && alive.expired())) return {};
        if (at) {
            ui::Size size;
            node->SpaceToRoot(at, &size);
            return { int(std::lround(size.width)), int(std::lround(size.height)) };
        }
        const auto& c = *node->m_Coordinators[coordinator];
        if (lookahead && c.la && c.la->measured) return c.la->Size();
        return { c.Width(), c.Height() };
    }

    namespace {
        // The main space of coordinates -> root.
        LayoutNode::Mat3 MainToRoot(const LayoutCoordinates& c) {
            return c.at ? c.node->SpaceToRoot(c.at) : c.node->CoordinatorToRoot(c.coordinator);
        }
    }

    Offset LayoutCoordinates::PositionInRoot() const {
        if (!IsAttached()) return {};
        return LayoutNode::MapPoint(MainToRoot(*this), {});
    }
    Offset LayoutCoordinates::PositionInParent() const {
        if (!IsAttached()) return {};
        const Offset root = PositionInRoot();
        const LayoutNode* parent = node->Parent();
        if (!parent) return root;
        return LayoutNode::UnmapPoint(parent->InnerToRoot(), root).value_or(Offset{});
    }
    IntSize LayoutCoordinates::ParentSize() const {
        if (!IsAttached()) return {};
        const LayoutNode* parent = node->Parent();
        return parent ? parent->InnerSize() : IntSize{};
    }
    Rect LayoutCoordinates::BoundsInRoot() const {
        if (!IsAttached()) return {};
        const IntSize size = Size();
        if (at) return node->RectInRoot(Rect{ 0, 0, float(size.width), float(size.height) }, at);
        const LayoutNode::Mat3 m = MainToRoot(*this);
        const Offset corners[] = { { 0, 0 }, { float(size.width), 0 }, { 0, float(size.height) }, { float(size.width), float(size.height) } };
        Rect r{ INFINITY, INFINITY, -INFINITY, -INFINITY };
        for (Offset p : corners) {
            const Offset q = LayoutNode::MapPoint(m, p);
            r.left = std::min(r.left, q.x); r.top = std::min(r.top, q.y);
            r.right = std::max(r.right, q.x); r.bottom = std::max(r.bottom, q.y);
        }
        return r;
    }

    Offset LayoutCoordinates::LocalPositionOf(const LayoutCoordinates& source, Offset rel) const {
        if (!node || !source.node) return rel;
        // A lookahead root's parent's children space (where lookahead offsets cross into main).
        auto rootSpace = [](const LayoutNode* root) -> LayoutCoordinates {
            return root->m_Parent ? root->m_Parent->InnerCoordinates() : root->OuterCoordinates();
        };
        if (!lookahead && !source.lookahead) {
            const Offset inRoot = LayoutNode::MapPoint(MainToRoot(source), rel);
            return LayoutNode::UnmapPoint(MainToRoot(*this), inRoot).value_or(Offset{});
        }
        if (lookahead && source.lookahead) {
            const auto a = node->LookaheadOffsetOf(coordinator), b = source.node->LookaheadOffsetOf(source.coordinator);
            if (a.root == b.root) return ToOffset(b.offset) + rel - ToOffset(a.offset);
            return rootSpace(a.root).LocalPositionOf(rootSpace(b.root), ToOffset(b.offset) + rel) - ToOffset(a.offset);
        }
        if (lookahead) {
            const auto a = node->LookaheadOffsetOf(coordinator);
            return rootSpace(a.root).LocalPositionOf(source, rel) - ToOffset(a.offset);
        }
        const auto b = source.node->LookaheadOffsetOf(source.coordinator);
        return LocalPositionOf(rootSpace(b.root), ToOffset(b.offset) + rel);
    }

    LayoutCoordinates LookaheadScope::ToLookaheadCoordinates(const LayoutCoordinates& c) const {
        if (c.lookahead || !c.node) return c;
        const std::size_t index = c.at ? c.node->CoordinatesOf(c.at).coordinator : c.coordinator;
        const auto& co = *c.node->m_Coordinators[index];
        if (c.node->m_Lookahead && co.la && co.la->measured) return { c.node, index, true };
        return c;
    }

    Offset LookaheadScope::LocalLookaheadPositionOf(const LayoutCoordinates& self, const LayoutCoordinates& source, Offset rel) const {
        return ToLookaheadCoordinates(self).LocalPositionOf(ToLookaheadCoordinates(source), rel);
    }

    LayoutNode::Mat3 LayoutNode::CoordinatorToRoot(std::size_t k) const {
        // The walk of HitSteps: coordinator k's space is reached at its layout modifier.
        Mat3 m = Mul3(ParentInner(m_Parent), Translate3(float(m_Position.x), float(m_Position.y)));
        std::size_t ci = 0;
        for (const auto& node : m_Nodes) {
            const auto& c = *m_Coordinators[ci];
            const ui::Size size{ float(c.Width()), float(c.Height()) };
            if (const DrawModifier* d = node->AsDraw()) {
                Mat3 t;
                if (LayerOf(d, size, t)) m = Mul3(m, t);
            }
            if (node->AsLayout() && ci + 1 < m_Coordinators.size()) {
                if (ci == k) return m;
                ++ci;
                const auto& n = *m_Coordinators[ci];
                m = Mul3(m, Translate3(float(n.position.x), float(n.position.y)));
            }
        }
        return m;
    }

    void LayoutNode::CaptureRulers(std::size_t index) {
        Coordinator& c = *m_Coordinators[index];
        std::vector<std::pair<const RulerData*, float>> values;
        if (c.result.rulers) {
            const Density d = NodeDensity();
            RulerScope scope(d, GetLayoutDirection(), c.Width(),
                             [this, index](Offset p) { return MapPoint(CoordinatorToRoot(index), p); });
            c.result.rulers(scope);
            values = scope.Values();
        }
        if (values == c.rulerValues) return;
        // Readers of rulers that changed, appeared or went away place again.
        std::vector<const RulerData*> changed;
        for (const auto& [r, v] : values) {
            auto it = std::find_if(c.rulerValues.begin(), c.rulerValues.end(), [&](const auto& p) { return p.first == r; });
            if (it == c.rulerValues.end() || it->second != v) changed.push_back(r);
        }
        for (const auto& [r, v] : c.rulerValues)
            if (std::none_of(values.begin(), values.end(), [&](const auto& p) { return p.first == r; })) changed.push_back(r);
        c.rulerValues = std::move(values);
        for (const RulerData* r : changed) InvalidateRulerReaders(*this, index, r);
    }

    void LayoutNode::InvalidateRulerReaders(LayoutNode& definer, std::size_t index, const RulerData* ruler) {
        // invalidateChildrenOfDefiningRuler: readers registered here, and those that read it (or
        // its default) from further up and sit inside this node.
        auto relayout = [&](Coordinator& c, bool onlyInside) {
            for (auto it = c.rulerReaders.begin(); it != c.rulerReaders.end(); ++it) {
                if (it->first != ruler) continue;
                auto& readers = it->second;
                for (auto r = readers.begin(); r != readers.end();) {
                    if (r->second.expired()) { r = readers.erase(r); continue; }
                    bool inside = !onlyInside;
                    for (const LayoutNode* n = r->first; n && !inside; n = n->m_Parent) inside = n == &definer;
                    if (inside) { if (r->first != &definer) r->first->RequestRelayout(); r = readers.erase(r); }
                    else ++r;
                }
            }
        };
        relayout(*definer.m_Coordinators[index], false);
        for (LayoutNode* n = &definer; n; n = n->m_Parent) {
            const std::size_t start = n == &definer ? index : n->m_Coordinators.size();
            for (std::size_t i = start; i-- > 0;) relayout(*n->m_Coordinators[i], true);
        }
    }

    float LayoutNode::FindRulerValue(std::size_t index, const Ruler& ruler, float defaultValue) {
        const RulerData* id = ruler.Id();
        auto addReader = [&](Coordinator& c) {
            for (auto& [r, readers] : c.rulerReaders)
                if (r == id) {
                    for (auto& [n, alive] : readers) if (n == this) return;
                    readers.emplace_back(this, m_Alive);
                    return;
                }
            c.rulerReaders.push_back({ id, { { this, m_Alive } } });
        };
        LayoutNode* n = this;
        std::size_t i = index + 1;
        Coordinator* top = nullptr;
        while (n) {
            while (i-- > 0) {
                Coordinator& c = *n->m_Coordinators[i];
                top = &c;
                for (const auto& [r, v] : c.rulerValues) {
                    if (r != id) continue;
                    addReader(c);
                    // calculateCoordinate: through the middle of the definer, into this space.
                    const Offset local = ruler.IsVertical() ? Offset{ v, float(c.Height()) / 2 } : Offset{ float(c.Width()) / 2, v };
                    const Offset root = MapPoint(n->CoordinatorToRoot(i), local);
                    const auto mine = UnmapPoint(CoordinatorToRoot(index), root);
                    if (!mine) return defaultValue;
                    return ruler.IsVertical() ? mine->x : mine->y;
                }
            }
            n = n->m_Parent;
            if (n) i = n->m_Coordinators.size();
        }
        if (top) addReader(*top);
        return defaultValue;
    }

    void LayoutNode::RecordLineRead(const AlignmentLine& line, int value) {
        if (m_CheckingLines || !m_Parent || !(m_Parent->m_InMeasure || m_Parent->m_InLayout)) return;
        if (m_Parent->m_InMeasure) m_LinesReadInMeasure = true;
        for (auto& [l, v] : m_LinesReadByParent)
            if (l == &line) { v = value; return; }
        m_LinesReadByParent.emplace_back(&line, value);
    }

    // After this node was measured or laid out on its own: a line the parent read that moved
    // invalidates the parent's measure (read while measuring) or its placement.
    void LayoutNode::CheckLinesReadByParent() {
        if (m_LinesReadByParent.empty() || !m_Parent || m_Parent->m_InMeasure || m_Parent->m_InLayout) return;
        m_CheckingLines = true;
        bool changed = false;
        for (auto& [l, v] : m_LinesReadByParent) {
            const int now = m_Coordinators.front()->Get(*l);
            if (now != v) { v = now; changed = true; }
        }
        m_CheckingLines = false;
        if (!changed) return;
        if (m_LinesReadInMeasure) m_Parent->RequestRemeasure();
        else m_Parent->RequestRelayout();
    }

    void LayoutNode::Draw(ContentDrawScope& scope) {
        if (!m_Placed && m_Parent) return;
        const LayoutDirection outer = scope.Direction();
        const Density outerDensity = scope;
        scope.SetDirection(GetLayoutDirection());
        static_cast<Density&>(scope) = NodeDensity();
        DrawCoordinator(0, scope);
        scope.SetDirection(outer);
        static_cast<Density&>(scope) = outerDensity;
    }

    void LayoutNode::DrawCoordinator(std::size_t index, ContentDrawScope& scope) {
        Coordinator& c = *m_Coordinators[index];
        if (index > 0 && !c.placed) return;
        const ui::Size size{ float(c.Width()), float(c.Height()) };
        // Draw modifiers nest: each one's DrawContent runs the next, the last draws the wrapped
        // content. Their reads subscribe this node's draw watcher.
        std::function<void(std::size_t)> step = [&](std::size_t d) {
            if (d == c.draws.size()) { DrawWrapped(index, scope); return; }
            scope.SetSize(size);
            scope.SetContent([&, d] { step(d + 1); });
            rt::Observation::Track(m_DrawWatch, [&] { c.draws[d]->Draw(scope); });
        };
        step(0);
    }

    bool LayoutNode::DrawAfter(const DrawModifier* after, ContentDrawScope& scope) {
        for (std::size_t index = 0; index < m_Coordinators.size(); ++index) {
            Coordinator& c = *m_Coordinators[index];
            const auto it = std::find(c.draws.begin(), c.draws.end(), after);
            if (it == c.draws.end()) continue;
            const ui::Size size{ float(c.Width()), float(c.Height()) };
            std::function<void(std::size_t)> step = [&](std::size_t d) {
                if (d == c.draws.size()) { DrawWrapped(index, scope); return; }
                scope.SetSize(size);
                scope.SetContent([&, d] { step(d + 1); });
                c.draws[d]->Draw(scope);
            };
            step(std::size_t(it - c.draws.begin()) + 1);
            return true;
        }
        return false;
    }

    void LayoutNode::DrawWrapped(std::size_t index, ContentDrawScope& scope) {
        if (index + 1 < m_Coordinators.size()) {
            const IntOffset p = m_Coordinators[index + 1]->position;
            scope.Translate({ float(p.x), float(p.y) });
            DrawCoordinator(index + 1, scope);
            scope.Translate({ -float(p.x), -float(p.y) });
            return;
        }
        // Children in zIndex order, ties by placement order (spec 03 §layout).
        std::vector<LayoutNode*> order;
        for (LayoutNode* ch : m_Children) if (ch->m_Placed) order.push_back(ch);
        std::stable_sort(order.begin(), order.end(), [](const LayoutNode* a, const LayoutNode* b) {
            const float za = a->ZIndex(), zb = b->ZIndex();
            return za != zb ? za < zb : a->m_PlaceOrder < b->m_PlaceOrder;
        });
        for (LayoutNode* ch : order) {
            const Offset p{ float(ch->m_Position.x), float(ch->m_Position.y) };
            scope.Translate(p);
            ch->Draw(scope);
            scope.Translate({ -p.x, -p.y });
        }
    }

    // --- Owner --------------------------------------------------------------------------------

    namespace {
        // Children get the constraints as they are and sit at the origin (RootMeasurePolicy).
        MeasureResult StackAtOrigin(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) {
            if (ch.empty()) return s.Layout(c.minWidth, c.minHeight);
            std::vector<Placeable*> ps;
            int w = 0, h = 0;
            for (Measurable* m : ch) {
                Placeable& p = m->Measure(c);
                w = std::max(w, p.Width());
                h = std::max(h, p.Height());
                ps.push_back(&p);
            }
            return s.Layout(c.ConstrainWidth(w), c.ConstrainHeight(h), [ps](PlacementScope& place) {
                for (Placeable* p : ps) place.PlaceRelative(*p, 0, 0);
            });
        }

        // The content root: RootMeasurePolicy (ui/layout/RootMeasurePolicy.kt).
        struct ContentRootPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) override {
                return StackAtOrigin(s, ch, c);
            }
        };

        // The window root: the content root with the window's constraints, then each popup
        // measured up to the display bounds (PopupLayout.internalOnMeasure) and placed where its
        // provider says (updatePosition), kept inside the display unless clipping is off
        // (Gravity.applyDisplay without FLAG_LAYOUT_NO_LIMITS). A popup whose anchor is not
        // placed is not placed either (hidden until it can be positioned).
        struct WindowRootPolicy final : MeasurePolicy {
            const WindowInsetsHolder* insets = nullptr;
            Owner* owner = nullptr;
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) override {
                std::vector<Placeable*> ps;
                std::vector<PopupLayer*> layers;
                const std::optional<Constraints> content = owner->ContentConstraintsForRoot();
                for (std::size_t i = 0; i < ch.size(); ++i) {
                    if (i == 0) { ps.push_back(&ch[0]->Measure(content ? *content : c)); layers.push_back(nullptr); continue; }
                    PopupLayer* l = i - 1 < owner->Popups().size() ? owner->Popups()[i - 1] : nullptr;
                    const IntRect d = owner->PopupDisplayBounds(!l || l->properties.clippingEnabled);
                    ps.push_back(&ch[i]->Measure(Constraints{ 0, std::max(0, d.right - d.left), 0, std::max(0, d.bottom - d.top) }));
                    layers.push_back(l);
                }
                const int rootW = c.ConstrainWidth(ps.empty() ? 0 : ps[0]->Width()), rootH = c.ConstrainHeight(ps.empty() ? 0 : ps[0]->Height());
                const bool centre = content.has_value();
                MeasureResult r = s.Layout(rootW, rootH, [ps, layers, o = owner, centre, rootW, rootH](PlacementScope& place) {
                    for (std::size_t i = 0; i < ps.size(); ++i) {
                        if (!layers[i]) {
                            // DialogLayout.internalOnLayout: centred in the window.
                            if (centre) place.Place(*ps[i], (rootW - ps[i]->Width()) / 2, (rootH - ps[i]->Height()) / 2);
                            else place.PlaceRelative(*ps[i], 0, 0);
                            continue;
                        }
                        PopupLayer& l = *layers[i];
                        const IntSize size{ ps[i]->Width(), ps[i]->Height() };
                        const auto anchor = l.anchorBounds ? l.anchorBounds() : std::nullopt;
                        if (!anchor || !l.positionProvider) { l.bounds.reset(); continue; }
                        const IntRect d = o->PopupDisplayBounds(l.properties.clippingEnabled);
                        IntOffset pos = l.positionProvider->CalculatePosition(*anchor, { d.right - d.left, d.bottom - d.top }, l.direction, size);
                        if (l.properties.clippingEnabled) {
                            pos.x = std::max(d.left, std::min(pos.x, d.right - size.width));
                            pos.y = std::max(d.top, std::min(pos.y, d.bottom - size.height));
                        }
                        place.Place(*ps[i], pos.x, pos.y);
                        l.bounds = IntRect{ pos.x, pos.y, pos.x + size.width, pos.y + size.height };
                    }
                });
                // The window insets as rulers for every layout (WindowInsetsRulersProvider).
                if (insets)
                    r.rulers = [h = insets, w = r.width, hh = r.height](RulerScope& scope) { ProvideWindowInsetsRulers(scope, *h, w, hh); };
                return r;
            }
        };
    }

    Owner::Owner() : m_Root(std::make_unique<LayoutNode>()), m_ContentRoot(std::make_unique<LayoutNode>()) {
        m_RootFocus.m_IsRoot = true;
        m_RootFocus.focusability = Focusability::Never;
        auto root = std::make_shared<WindowRootPolicy>();
        root->insets = &m_Insets;
        root->owner = this;
        m_Root->SetMeasurePolicy(std::move(root));
        m_ContentRoot->SetMeasurePolicy(std::make_shared<ContentRootPolicy>());
        m_Root->Attach(*this, 0);
        m_Root->SetChildren({ m_ContentRoot.get() });
    }

    namespace {
        // Outlives nothing: the owner detaches it when it goes.
        struct OwnerKeyboardController final : SoftwareKeyboardController {
            Owner* owner;
            explicit OwnerKeyboardController(Owner* o) : owner(o) {}
            void Show() override { if (owner) owner->ShowSoftKeyboard(); }
            void Hide() override { if (owner) owner->HideSoftKeyboard(); }
        };
    }

    std::shared_ptr<SoftwareKeyboardController> Owner::KeyboardController() {
        if (!m_KeyboardController) m_KeyboardController = std::make_shared<OwnerKeyboardController>(this);
        return m_KeyboardController;
    }

    std::shared_ptr<SoftwareKeyboardController> CurrentSoftwareKeyboardController() {
        auto& c = *rt::Composition::Current();
        if (auto provided = c.Read(LocalSoftwareKeyboardController)) return provided;
        Owner* owner = c.Read(LocalOwner);
        return owner ? owner->KeyboardController() : nullptr;
    }

    Owner::~Owner() {
        if (m_KeyboardController) static_cast<OwnerKeyboardController&>(*m_KeyboardController).owner = nullptr;
        m_Popups.clear();
        m_Root->SetChildren({ m_ContentRoot.get() });
        m_Root->Detach();
    }

    // --- dialog window content -------------------------------------------------------------------

    void Owner::SetContentConstraints(std::optional<Constraints> c) {
        if (m_ContentConstraints == c) return;
        m_ContentConstraints = c;
        m_Root->RequestRemeasure();
    }

    IntSize Owner::MeasureContent(Constraints c) {
        const auto saved = m_ContentConstraints;
        m_ContentConstraints = c;
        m_ContentRoot->MeasureNode(c);
        m_ContentConstraints = saved;
        m_Root->RequestRemeasure();
        return m_ContentRoot->Size();
    }

    IntRect Owner::ContentBounds() const {
        const IntOffset p = m_ContentRoot->Position();
        const IntSize s = m_ContentRoot->Size();
        return { p.x, p.y, p.x + s.width, p.y + s.height };
    }

    // --- popups ---------------------------------------------------------------------------------

    void Owner::UpdatePopupChildren() {
        std::vector<rt::TreeNode*> children{ m_ContentRoot.get() };
        for (PopupLayer* l : m_Popups) if (l->node) children.push_back(l->node);
        m_Root->SetChildren(std::move(children));
        m_Root->RequestRemeasure();
        m_PopupsVersion.Set(m_PopupsVersion.Peek() + 1);
        ApplyFlagSecure();
    }

    void Owner::AddPopup(PopupLayer* layer) {
        if (std::find(m_Popups.begin(), m_Popups.end(), layer) != m_Popups.end()) return;
        m_Popups.push_back(layer);
        UpdatePopupChildren();
        SyncPopupFocus();
        // A focusable popup window takes the key focus: the host view has to have it.
        if (layer->properties.focusable && m_RequestHostFocus) m_RequestHostFocus();
    }

    void Owner::SyncPopupFocus() {
        PopupLayer* top = TopFocusablePopup();
        if (!top || !m_ActiveFocus) return;
        if (PopupOf(*NodeOf(*m_ActiveFocus)) == top) return;
        // The window under it loses the key focus, not its focused item: no focus events.
        if (!top->parkedFocus) top->parkedFocus = m_ActiveFocus;
        m_ActiveFocus = nullptr;
        m_FocusCaptured = false;
        DispatchFocusEvents();
    }

    void Owner::RemovePopup(PopupLayer* layer) {
        const auto it = std::find(m_Popups.begin(), m_Popups.end(), layer);
        if (it == m_Popups.end()) return;
        m_Popups.erase(it);
        if (layer->properties.excludeFromSystemGesture) ReplaceViewRect(0, layer, std::nullopt);
        FocusTargetNode* parked = layer->parkedFocus;
        layer->parkedFocus = nullptr;
        layer->bounds.reset();
        // The window under it gets the key focus back, its focused item still focused.
        const bool focusInside = m_ActiveFocus && layer->node && NodeOf(*m_ActiveFocus) &&
                                 [&] { for (LayoutNode* n = NodeOf(*m_ActiveFocus); n; n = n->m_Parent) if (n == layer->node) return true; return false; }();
        UpdatePopupChildren();
        if (parked && (!m_ActiveFocus || focusInside)) {
            m_ActiveFocus = parked;
            m_FocusCaptured = false;
            DispatchFocusEvents();
        }
    }

    void Owner::UpdatePopup(PopupLayer* layer) {
        if (layer && layer->node) layer->node->RequestRemeasure();
        m_Root->RequestRemeasure();
        m_PopupsVersion.Set(m_PopupsVersion.Peek() + 1);
        ApplyFlagSecure();
        // No longer focusable: the focus it parked comes back.
        if (layer && !layer->properties.focusable && layer->parkedFocus) {
            FocusTargetNode* parked = layer->parkedFocus;
            layer->parkedFocus = nullptr;
            if (!m_ActiveFocus || PopupOf(*NodeOf(*m_ActiveFocus)) == layer) {
                m_ActiveFocus = parked;
                DispatchFocusEvents();
            }
        }
        SyncPopupFocus();
        // The top focusable popup has the key focus: the host view has to have it.
        if (layer && layer->properties.focusable && TopFocusablePopup() == layer && m_RequestHostFocus) m_RequestHostFocus();
    }

    PopupLayer* Owner::TopFocusablePopup() const {
        (void)m_PopupsVersion.Get();
        for (auto it = m_Popups.rbegin(); it != m_Popups.rend(); ++it)
            if ((*it)->properties.focusable) return *it;
        return nullptr;
    }

    PopupLayer* Owner::PopupOf(const LayoutNode& n) const {
        for (const LayoutNode* p = &n; p; p = p->m_Parent)
            for (PopupLayer* l : m_Popups) if (l->node == p) return l;
        return nullptr;
    }

    IntRect Owner::PopupDisplayBounds(bool clippingEnabled) const {
        IntRect r{ 0, 0, m_Window.width, m_Window.height };
        if (!clippingEnabled) return r;
        // The visible display frame: the window less the system bars and the keyboard.
        InsetsValue in;
        for (InsetsType t : { InsetsType::StatusBars, InsetsType::NavigationBars, InsetsType::CaptionBar, InsetsType::Ime })
            in = in.Union(m_Insets.Get(t));
        return { in.left, in.top, std::max(in.left, r.right - in.right), std::max(in.top, r.bottom - in.bottom) };
    }

    LayoutNode& Owner::FocusScopeRoot() const {
        if (PopupLayer* l = TopFocusablePopup(); l && l->node) return *l->node;
        return *m_ContentRoot;
    }

    void Owner::ForgetFocusTarget(const FocusTargetNode* t) {
        for (PopupLayer* l : m_Popups) if (l->parkedFocus == t) l->parkedFocus = nullptr;
    }

    // A popup's anchor moved without the window root being laid out (a scroll, say): place the
    // popups again (PopupLayout.updateParentBounds from onGloballyPositioned).
    void Owner::PlacePopups() {
        for (PopupLayer* l : m_Popups) {
            if (!l->node) continue;
            const auto anchor = l->anchorBounds ? l->anchorBounds() : std::nullopt;
            bool stale = anchor.has_value() != l->bounds.has_value();
            if (!stale && anchor && l->positionProvider) {
                const IntRect d = PopupDisplayBounds(l->properties.clippingEnabled);
                const IntSize size = l->node->Size();
                IntOffset pos = l->positionProvider->CalculatePosition(*anchor, { d.right - d.left, d.bottom - d.top }, l->direction, size);
                if (l->properties.clippingEnabled) {
                    pos.x = std::max(d.left, std::min(pos.x, d.right - size.width));
                    pos.y = std::max(d.top, std::min(pos.y, d.bottom - size.height));
                }
                stale = pos.x != l->bounds->left || pos.y != l->bounds->top;
            }
            if (stale) { m_Root->RequestRelayout(); break; }
        }
    }

    void Owner::OnMeasureRequested(LayoutNode& n) {
        m_MeasureDirty.insert(&n);
        if (m_RequestFrame) m_RequestFrame();
    }

    void Owner::OnLayoutRequested(LayoutNode& n) {
        m_LayoutDirty.insert(&n);
        if (m_RequestFrame) m_RequestFrame();
    }

    void Owner::RequestFullRemeasure() {
        std::vector<LayoutNode*> stack{ m_Root.get() };
        while (!stack.empty()) {
            LayoutNode* n = stack.back();
            stack.pop_back();
            n->RequestRemeasure();
            for (LayoutNode* c : n->m_Children) stack.push_back(c);
        }
    }

    void Owner::MeasureAndLayout(IntSize window) {
        // The focused item's properties are re-read every pass (Compose observes the reads).
        if (!m_FocusInvalid.empty() || m_FocusTargetLost || m_ActiveFocus) InvalidateFocusNodes();
        if (window != m_Window) {
            m_Window = window;
            m_ObservedWindow.Set(window);
            m_Root->RequestRemeasure();
        }
        // Shallowest first. A node whose size changed makes its parent remeasure, which the
        // ordered set then visits next (it is shallower).
        while (!m_MeasureDirty.empty()) {
            m_LaidOut = true;
            LayoutNode* n = *m_MeasureDirty.begin();
            m_MeasureDirty.erase(m_MeasureDirty.begin());
            const bool lookaheadDirty = n->m_Lookahead && n->m_Lookahead->measureDirty;
            if (!n->m_MeasureDirty && !lookaheadDirty) continue;
            if (n == m_Root.get()) {
                n->MeasureNode(Constraints::Fixed(window.width, window.height));
                n->m_Coordinators.front()->PlaceAt({ 0, 0 }, 0);
                continue;
            }
            if (!n->m_Measured || !n->m_Parent) {
                // Never measured: its parent has to measure it.
                if (n->m_Parent) n->m_Parent->RequestRemeasure();
                continue;
            }
            if (lookaheadDirty && !n->IsOutMostLookaheadRoot()) {
                // doLookaheadRemeasure: a lookahead size change goes to the parent's lookahead.
                if (!n->m_Lookahead->measured) { n->m_Parent->RequestLookaheadRemeasure(); continue; }
                const IntSize lookaheadBefore = n->La(0).Size();
                n->LookaheadMeasureNode(n->m_Lookahead->constraints);
                if (n->La(0).Size() != lookaheadBefore) { n->m_Parent->RequestLookaheadRemeasure(); continue; }
            }
            const IntSize before = n->Size();
            n->MeasureNode(n->m_LastConstraints);
            if (n->Size() != before) n->m_Parent->RequestMainRemeasure();
            else n->RequestMainRelayout();
        }
        while (!m_LayoutDirty.empty()) {
            LayoutNode* n = *m_LayoutDirty.begin();
            m_LayoutDirty.erase(m_LayoutDirty.begin());
            if (n->m_Placed || n == m_Root.get()) n->LayoutIfNeeded();
            m_LaidOut = true;
        }
        if (m_LaidOut) {
            m_LaidOut = false;
            DispatchGloballyPositioned();
        }
        if (!m_Popups.empty()) {
            PlacePopups();
            while (!m_LayoutDirty.empty()) {
                LayoutNode* n = *m_LayoutDirty.begin();
                m_LayoutDirty.erase(m_LayoutDirty.begin());
                if (n->m_Placed || n == m_Root.get()) n->LayoutIfNeeded();
                m_LaidOut = true;
            }
            if (m_LaidOut) { m_LaidOut = false; DispatchGloballyPositioned(); }
            for (PopupLayer* l : m_Popups)
                if (l->properties.excludeFromSystemGesture) ReplaceViewRect(0, l, l->bounds);
        }
        // onBarrierPositionChanged: a barrier that now covers the focused target takes focus away.
        if (!m_Barriers.empty() && m_ActiveFocus && IsOccludedByInteractionBarrier(*m_ActiveFocus)) ClearFocus(true);
        if (m_Autofill) m_Autofill->OnEndApplyChanges();
        if (m_ContentConstraints && m_ContentRoot->Size() != m_LastContentSize) {
            m_LastContentSize = m_ContentRoot->Size();
            if (m_OnContentSizeChanged) m_OnContentSizeChanged(m_LastContentSize);
        }
    }

    std::vector<PointerInputModifier*> LayoutNode::PointerModifiers() const {
        std::vector<PointerInputModifier*> out;
        for (auto& n : m_Nodes) if (auto* p = n->AsPointerInput()) out.push_back(p);
        return out;
    }

    LayoutNode* Owner::FindBySemanticsId(int id) {
        std::vector<LayoutNode*> stack{ m_Root.get() };
        while (!stack.empty()) {
            LayoutNode* n = stack.back();
            stack.pop_back();
            if (n->m_SemanticsId == id) return n;
            for (LayoutNode* c : n->m_Children) stack.push_back(c);
        }
        return nullptr;
    }

    void Owner::DispatchOnScrollChanged(LayoutNode& node, Offset delta) {
        if (m_Accessibility) m_Accessibility->OnScrollChanged(node, delta);
    }

    void Owner::InvalidateSemantics() {
        ++m_SemanticsVersion;
        if (m_OnSemanticsChanged) m_OnSemanticsChanged();
        if (m_Accessibility) {
            m_Accessibility->OnSemanticsChange();
            if (m_Accessibility->Listening() && m_OnAccessibilityChange) m_OnAccessibilityChange(false);
        }
    }

    void Owner::SetAutofillHost(AutofillHost host) { m_Autofill = std::make_unique<AutofillManager>(*this, std::move(host)); }
    void Owner::RequestAutofill(LayoutNode& node) { if (m_Autofill) m_Autofill->RequestAutofill(node); }

    AccessibilityDelegate& Owner::Accessibility() {
        if (!m_Accessibility) m_Accessibility = std::make_unique<AccessibilityDelegate>(*this);
        return *m_Accessibility;
    }

    void Owner::NotifyLayoutChange(LayoutNode& node) {
        if (!m_Accessibility) return;
        if (m_Accessibility->OnLayoutChange(node) && m_OnAccessibilityChange) m_OnAccessibilityChange(true);
    }

    void Owner::Forget(LayoutNode& n) {
        m_PositionAware.erase(&n);
        m_FocusInvalid.erase(&n);
        if (m_ActiveFocus && NodeOf(*m_ActiveFocus) == &n) { m_ActiveFocus = nullptr; m_FocusCaptured = false; m_FocusTargetLost = true; }
        for (PopupLayer* l : m_Popups)
            if (l->parkedFocus && NodeOf(*l->parkedFocus) == &n) l->parkedFocus = nullptr;
        m_MeasureDirty.erase(&n);
        m_LayoutDirty.erase(&n);
        m_CoordinateReaders.erase(&n);
        for (auto& [id, t] : m_Pointers)
            std::erase_if(t.path, [&](const HitEntry& e) { return e.node == &n; });
    }

    void Owner::TrackPositionAware(LayoutNode& n) {
        bool any = false;
        for (auto& m : n.m_Nodes) if (m->AsGlobalPositionAware()) any = true;
        if (any) { if (m_PositionAware.insert(&n).second) m_LaidOut = true; }
        else m_PositionAware.erase(&n);
    }

    void Owner::DispatchGloballyPositioned() {
        // Parents first (OnPositionedDispatcher sorts by depth); callbacks may change the tree.
        std::vector<LayoutNode*> nodes(m_PositionAware.begin(), m_PositionAware.end());
        std::stable_sort(nodes.begin(), nodes.end(), [](const LayoutNode* a, const LayoutNode* b) { return a->Depth() < b->Depth(); });
        for (LayoutNode* n : nodes) {
            if (!m_PositionAware.count(n) || !n->IsPlaced()) continue;
            std::vector<GlobalPositionAwareModifier*> mods;
            std::vector<const ModifierNode*> at;
            for (auto& m : n->m_Nodes)
                if (auto* g = m->AsGlobalPositionAware()) { mods.push_back(g); at.push_back(m.get()); }
            for (std::size_t i = 0; i < mods.size(); ++i) {
                ui::Size size;
                const auto toRoot = n->SpaceToRoot(at[i], &size);
                if (toRoot == mods[i]->lastToRoot && size == mods[i]->lastSize) continue;
                mods[i]->lastToRoot = toRoot;
                mods[i]->lastSize = size;
                mods[i]->OnGloballyPositioned();
                if (!m_PositionAware.count(n)) break;
            }
        }
    }

    void Owner::SetClipboardText(const std::string& text) {
        if (m_Clipboard.setText) m_Clipboard.setText(text);
        else { m_LocalClip = text; m_LocalStyledClip.reset(); }
    }
    void Owner::SetClipboardAnnotatedText(const text::AnnotatedString& text) {
        if (m_Clipboard.setAnnotatedText) { m_Clipboard.setAnnotatedText(text); return; }
        if (m_Clipboard.setText) { m_Clipboard.setText(text.Text()); return; }
        // convertToCharSequence keeps the span styles only.
        text::AnnotatedStringBuilder b;
        b.Append(text.Text());
        for (const auto& r : text.SpanStyles()) b.AddStyle(r.item, r.start, r.end);
        m_LocalClip = text.Text();
        m_LocalStyledClip = b.ToAnnotatedString();
    }
    std::optional<text::AnnotatedString> Owner::ClipboardAnnotatedText() const {
        if (m_Clipboard.getAnnotatedText) return m_Clipboard.getAnnotatedText();
        if (m_Clipboard.getText) {
            const auto t = m_Clipboard.getText();
            return t ? std::optional<text::AnnotatedString>(*t) : std::nullopt;
        }
        if (m_LocalStyledClip) return m_LocalStyledClip;
        return m_LocalClip ? std::optional<text::AnnotatedString>(*m_LocalClip) : std::nullopt;
    }
    std::optional<std::string> Owner::ClipboardText() const {
        return m_Clipboard.getText ? m_Clipboard.getText() : m_LocalClip;
    }
    bool Owner::ClipboardHasText() const {
        return m_Clipboard.hasText ? m_Clipboard.hasText() : m_LocalClip.has_value();
    }
    std::optional<std::pair<ClipEntry, ClipMetadata>> Owner::ClipboardClip() const {
        if (m_Clipboard.getClip) return m_Clipboard.getClip();
        const auto text = ClipboardText();
        if (!text) return std::nullopt;
        return std::pair{ ClipEntry{ { ClipItem{ *text, std::nullopt, std::nullopt } } }, ClipMetadata{ "plain text", { "text/plain" } } };
    }
    std::optional<ClipMetadata> Owner::ClipboardMetadata() const {
        if (m_Clipboard.getMetadata) return m_Clipboard.getMetadata();
        if (!ClipboardHasText()) return std::nullopt;
        return ClipMetadata{ "plain text", { "text/plain" } };
    }

    void Owner::ToolbarObserver::OnStateChanged() {
        // onDataChange invalidates the menu; onPositionChange invalidates the content rect.
        if (!owner->m_ToolbarClient) return;
        if (bounds) owner->ReadToolbarBounds();
        else owner->ReadToolbarEntries();
    }
    void Owner::ReadToolbarEntries() {
        std::vector<TextToolbarEntry> entries;
        TextToolbarClient* client = m_ToolbarClient;
        rt::Observation::Track(m_ToolbarDataObserver, [&] { entries = client->Entries(); });
        if (entries == m_ToolbarEntries) return;   // updateMenuItems: the same data changes nothing
        m_ToolbarEntries = std::move(entries);
        if (m_ToolbarHost.update) m_ToolbarHost.update(m_ToolbarEntries);
    }
    void Owner::ReadToolbarBounds() {
        Rect r;
        TextToolbarClient* client = m_ToolbarClient;
        rt::Observation::Track(m_ToolbarBoundsObserver, [&] { r = client->ContentBounds(); });
        if (r.left == m_ToolbarRect.left && r.top == m_ToolbarRect.top && r.right == m_ToolbarRect.right && r.bottom == m_ToolbarRect.bottom) return;
        m_ToolbarRect = r;
        if (m_ToolbarHost.move) m_ToolbarHost.move(r);
    }
    void Owner::ShowTextToolbar(TextToolbarClient* client) {
        if (!client) return;
        if (m_ToolbarClient == client) return;
        if (TextToolbarClient* previous = m_ToolbarClient) {
            // mutate cancels the previous session: its mode finishes and its handler hears.
            HideTextToolbar(previous);
            previous->OnClosed();
        }
        m_ToolbarClient = client;
        m_ToolbarDataObserver.owner = m_ToolbarBoundsObserver.owner = this;
        m_ToolbarBoundsObserver.bounds = true;
        rt::Observation::Track(m_ToolbarDataObserver, [&] { m_ToolbarEntries = client->Entries(); });
        rt::Observation::Track(m_ToolbarBoundsObserver, [&] { m_ToolbarRect = client->ContentBounds(); });
        // onCreateActionMode: no items, no mode; the session ends at once.
        const bool started = m_ToolbarHost.show ? m_ToolbarHost.show(m_ToolbarRect, m_ToolbarEntries) : !m_ToolbarEntries.empty();
        if (!started) {
            m_ToolbarClient = nullptr;
            m_ToolbarEntries.clear();
            m_ToolbarDataObserver.Unsubscribe();
            m_ToolbarBoundsObserver.Unsubscribe();
            client->OnClosed();
        }
    }
    void Owner::HideTextToolbar(TextToolbarClient* client) {
        if (!client || client != m_ToolbarClient) return;
        m_ToolbarClient = nullptr;
        m_ToolbarEntries.clear();
        m_ToolbarDataObserver.Unsubscribe();
        m_ToolbarBoundsObserver.Unsubscribe();
        if (m_ToolbarHost.hide) m_ToolbarHost.hide();
    }
    void Owner::OnTextToolbarItem(std::size_t index) {
        if (m_ToolbarClient && index < m_ToolbarEntries.size()) m_ToolbarClient->OnEntryClicked(index);
    }
    void Owner::OnTextToolbarDestroyed() {
        // onDestroyActionMode: the session closes.
        TextToolbarClient* client = m_ToolbarClient;
        if (!client) return;
        m_ToolbarClient = nullptr;
        m_ToolbarEntries.clear();
        m_ToolbarDataObserver.Unsubscribe();
        m_ToolbarBoundsObserver.Unsubscribe();
        client->OnClosed();
    }

    void Owner::ForgetPointerModifier(PointerInputModifier* m) {
        for (auto& [id, t] : m_Pointers)
            std::erase_if(t.path, [&](const HitEntry& e) { return e.mod == m; });
    }

    gfx::ShadowLight Owner::ShadowLight() const {
        if (m_ShadowLight) return *m_ShadowLight;
        return gfx::ShadowLight::Framework(m_Density.density, float(m_Window.width), float(m_Window.height), 0, 0);
    }

    void Owner::DrawWith(const DrawTarget& target, const std::function<void(DrawScope&)>& draw) {
        DrawTarget t = target;
        t.shadowLight = ShadowLight();
        ContentDrawScope scope(m_Density, m_Direction, t);
        draw(scope);
        scope.Finish();
    }

    void Owner::Draw(const DrawTarget& target) {
        DrawTarget t = target;
        t.shadowLight = ShadowLight();
        ContentDrawScope scope(m_Density, m_Direction, t);
        m_Root->Draw(scope);
        scope.Finish();
    }

}
