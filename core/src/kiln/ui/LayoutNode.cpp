#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Accessibility.h"
#include "kiln/ui/InsetsRulers.h"

#include <cmath>
#include "kiln/ui/NestedScroll.h"

#include <algorithm>

namespace kiln::ui {

    // --- ModifierNode ---------------------------------------------------------------------

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
            return MeasureHere(c);
        }

        Placeable& MeasureHere(const Constraints& c) {
            MeasureScope scope = node.Scope();
            if (mod) {
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
            node.LayoutIfNeeded();
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
            if (index == 0) {
                LayoutNode* parent = node.m_Parent;
                const bool moved = !node.m_Placed || node.m_Position != p || zIndex != z;
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
                if (position != p)
                    if (Owner* o = node.GetOwner()) o->NotifyLayoutChange(node);
                position = p;
                // A layout modifier's zIndex adds to the node's (Modifier.zIndex).
                if (zIndex != z) { zIndex = z; node.UpdateZIndex(); }
            }
        }

        int MinIntrinsicWidth(int h) override {
            MeasureScope s = node.Scope();
            if (mod) return mod->MinIntrinsicWidth(s, *Next(), h);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MinIntrinsicWidth(s, ch, h) : 0;
        }
        int MaxIntrinsicWidth(int h) override {
            MeasureScope s = node.Scope();
            if (mod) return mod->MaxIntrinsicWidth(s, *Next(), h);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MaxIntrinsicWidth(s, ch, h) : 0;
        }
        int MinIntrinsicHeight(int w) override {
            MeasureScope s = node.Scope();
            if (mod) return mod->MinIntrinsicHeight(s, *Next(), w);
            auto ch = ChildMeasurables();
            return node.m_Policy ? node.m_Policy->MinIntrinsicHeight(s, ch, w) : 0;
        }
        int MaxIntrinsicHeight(int w) override {
            MeasureScope s = node.Scope();
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
        if (m_Owner) m_Owner->Forget(*this);
        for (auto& n : m_Nodes) if (n->m_Owner) { n->OnDetach(); n->m_Owner = nullptr; }
    }

    void LayoutNode::Watcher::OnStateChanged() {
        if (phase == 0) node.RequestRemeasure();
        else if (phase == 1) node.RequestRelayout();
        else node.InvalidateDraw();
    }

    MeasureScope LayoutNode::Scope() const {
        return MeasureScope(m_Owner ? m_Owner->GetDensity() : Density{}, m_Owner ? m_Owner->Direction() : LayoutDirection::Ltr);
    }

    Measurable& LayoutNode::AsMeasurable() { return *m_Coordinators.front(); }

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

    void LayoutNode::HitSteps(const Mat3& parentInner, std::vector<HitStep>& out, Mat3& inner) const {
        Mat3 m = Mul3(parentInner, Translate3(float(m_Position.x), float(m_Position.y)));
        // Nodes up to and including a layout modifier sit in that modifier's coordinator.
        std::size_t ci = 0;
        auto sizeOf = [&](std::size_t i) { const auto& c = *m_Coordinators[i]; return ui::Size{ float(c.Width()), float(c.Height()) }; };
        for (const auto& node : m_Nodes) {
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
        for (std::size_t i = 0; i < next.size(); ++i) {
            const ModifierElement& e = *next[i];
            if (i < m_Elements.size() && m_Nodes[i] && m_Elements[i]->Type() == e.Type()) {
                if (!m_Elements[i]->Equals(e)) {
                    e.Update(*m_Nodes[i]);
                    updated = true;
                    if (focusKind(*m_Nodes[i])) focusChanged = true;
                }
                nodes.push_back(std::move(m_Nodes[i]));
            } else {
                structural = true;
                auto n = e.Create();
                if (focusKind(*n)) focusChanged = true;
                n->m_Owner = this;
                nodes.push_back(std::move(n));
                if (m_Owner) nodes.back()->OnAttach();
            }
        }
        for (auto& old : m_Nodes)
            if (old) {
                if (focusKind(*old)) focusChanged = true;
                if (m_Owner) {
                    if (auto* p = old->AsPointerInput()) m_Owner->ForgetPointerModifier(p);
                    old->OnDetach();
                }
                old->m_Owner = nullptr;
            }
        m_Nodes = std::move(nodes);
        m_Elements = next;
        if (m_Owner && focusChanged) m_Owner->ScheduleFocusInvalidation(*this);
        if (m_Owner && (structural || updated)) m_Owner->TrackPositionAware(*this);
        if (structural) RebuildChain();
        if (m_Owner && (structural || updated)) m_Owner->InvalidateSemantics();
        // Parent data may have changed: the parent measures with it.
        ParentData pd;
        for (auto& n : m_Nodes) if (auto* p = n->AsParentData()) p->ModifyParentData(pd);
        m_ParentData = std::move(pd);
        if (m_Parent) m_Parent->RequestRemeasure();
        RequestRemeasure();
        InvalidateDraw();
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
    }

    void LayoutNode::SetChildren(std::vector<rt::TreeNode*> children) {
        if (m_MeasureManaged) return;
        std::vector<LayoutNode*> next;
        next.reserve(children.size());
        for (auto* c : children) next.push_back(static_cast<LayoutNode*>(c));
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
        m_MeasureDirty = true;
        const IntSize before = Size();
        MeasureNode(m_LastConstraints);
        if (before != Size() && m_Parent) m_Parent->RequestRemeasure();
        LayoutChain();
    }

    void LayoutNode::Attach(Owner& owner, int depth) {
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
        for (LayoutNode* c : m_Children) c->Attach(owner, depth + 1);
        m_MeasureDirty = true;
        owner.OnMeasureRequested(*this);
    }

    void LayoutNode::Detach() {
        for (LayoutNode* c : m_Children) c->Detach();
        if (m_Owner) {
            m_Owner->Forget(*this);
            for (auto& n : m_Nodes) n->OnDetach();
        }
        m_MeasureWatch.Unsubscribe();
        m_LayoutWatch.Unsubscribe();
        m_DrawWatch.Unsubscribe();
        m_Owner = nullptr;
        m_Placed = false;
    }

    void LayoutNode::RequestRemeasure() {
        m_MeasureDirty = true;
        m_LayoutDirty = true;
        if (m_Owner) m_Owner->OnMeasureRequested(*this);
    }

    void LayoutNode::RequestRelayout() {
        m_LayoutDirty = true;
        if (m_Owner) m_Owner->OnLayoutRequested(*this);
    }

    void LayoutNode::InvalidateDraw() {
        if (m_Owner) m_Owner->OnDrawRequested();
    }

    Placeable& LayoutNode::MeasureNode(const Constraints& c) {
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
        m_LayoutDirty = false;
        if (m_Owner) m_Owner->m_LayoutDirty.erase(this);
        m_NextPlaceOrder = 0;
        const LayoutDirection dir = m_Owner ? m_Owner->Direction() : LayoutDirection::Ltr;
        for (LayoutNode* c : m_Children) c->m_PlacedThisPass = false;
        m_InLayout = true;
        rt::Observation::Track(m_LayoutWatch, [&] {
            for (std::size_t i = 0; i < m_Coordinators.size(); ++i) {
                auto& c = m_Coordinators[i];
                CaptureRulers(i);
                PlacementScope scope(dir, c->Width());
                scope.SetRulerLookup([this, i](const Ruler& r, float d) { return FindRulerValue(i, r, d); });
                if (m_Owner) scope.SetInsets(&m_Owner->Insets());
                if (c->result.place) c->result.place(scope);
            }
        });
        m_InLayout = false;
        // Children this pass did not place stop drawing and hit testing (markNodeAndSubtreeAsNotPlaced).
        for (LayoutNode* c : m_Children)
            if (!c->m_PlacedThisPass && c->m_Placed) { c->m_Placed = false; InvalidateDraw(); }
        InvalidateDraw();
        CheckLinesReadByParent();
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
            const Density d = m_Owner ? m_Owner->GetDensity() : Density{};
            RulerScope scope(d, m_Owner ? m_Owner->Direction() : LayoutDirection::Ltr, c.Width(),
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
        DrawCoordinator(0, scope);
    }

    void LayoutNode::DrawCoordinator(std::size_t index, ContentDrawScope& scope) {
        Coordinator& c = *m_Coordinators[index];
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
            return a->m_ZIndex != b->m_ZIndex ? a->m_ZIndex < b->m_ZIndex : a->m_PlaceOrder < b->m_PlaceOrder;
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
        // RootMeasurePolicy (ui/layout/RootMeasurePolicy.kt): children get the window's
        // constraints as they are and sit at the origin.
        struct RootMeasurePolicy final : MeasurePolicy {
            const WindowInsetsHolder* insets = nullptr;
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) override {
                MeasureResult r = Children(s, ch, c);
                // The window insets as rulers for every layout (WindowInsetsRulersProvider).
                if (insets)
                    r.rulers = [h = insets, w = r.width, hh = r.height](RulerScope& scope) { ProvideWindowInsetsRulers(scope, *h, w, hh); };
                return r;
            }
            MeasureResult Children(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) {
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
        };
    }

    Owner::Owner() : m_Root(std::make_unique<LayoutNode>()) {
        m_RootFocus.m_IsRoot = true;
        m_RootFocus.focusability = Focusability::Never;
        auto root = std::make_shared<RootMeasurePolicy>();
        root->insets = &m_Insets;
        m_Root->SetMeasurePolicy(std::move(root));
        m_Root->Attach(*this, 0);
    }

    Owner::~Owner() {
        m_Root->Detach();
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
            m_Root->RequestRemeasure();
        }
        // Shallowest first. A node whose size changed makes its parent remeasure, which the
        // ordered set then visits next (it is shallower).
        while (!m_MeasureDirty.empty()) {
            m_LaidOut = true;
            LayoutNode* n = *m_MeasureDirty.begin();
            m_MeasureDirty.erase(m_MeasureDirty.begin());
            if (!n->m_MeasureDirty) continue;
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
            const IntSize before = n->Size();
            n->MeasureNode(n->m_LastConstraints);
            if (n->Size() != before) n->m_Parent->RequestRemeasure();
            else n->RequestRelayout();
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
        // onBarrierPositionChanged: a barrier that now covers the focused target takes focus away.
        if (!m_Barriers.empty() && m_ActiveFocus && IsOccludedByInteractionBarrier(*m_ActiveFocus)) ClearFocus(true);
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
        m_MeasureDirty.erase(&n);
        m_LayoutDirty.erase(&n);
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

    void Owner::Draw(const DrawTarget& target) {
        ContentDrawScope scope(m_Density, m_Direction, target);
        m_Root->Draw(scope);
        scope.Finish();
    }

}
