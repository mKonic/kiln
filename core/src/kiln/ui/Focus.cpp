#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <climits>

namespace kiln::ui {

    using OnFound = std::function<bool(FocusTargetNode&)>;

    // --- FocusTargetNode / FocusRequester --------------------------------------------------------

    FocusState FocusTargetNode::State() const {
        if (!Owner() || !Owner()->GetOwner()) return FocusState::Inactive;
        return Owner()->GetOwner()->StateOf(*this);
    }
    bool FocusTargetNode::CanFocus() const {
        if (!Owner() || !Owner()->GetOwner()) return false;
        return Owner()->GetOwner()->CanFocus(*this);
    }
    bool FocusTargetNode::RequestFocus(FocusDirection d) {
        if (!Owner() || !Owner()->GetOwner()) return false;
        return Owner()->GetOwner()->RequestFocus(*this, d);
    }
    void FocusTargetNode::OnDetach() {
        // A focused node going away clears focus (FocusTargetNode.onDetach); the owner forgets it.
        if (auto* n = Owner(); n && n->GetOwner() && n->GetOwner()->ActiveFocus() == this) {
            n->GetOwner()->ClearFocus(true);
        }
    }
    bool FocusRequester::RequestFocus(FocusDirection d) {
        for (FocusTargetNode* t : m_Targets) if (t->RequestFocus(d)) return true;
        return false;
    }

    // --- tree relations -----------------------------------------------------------------------

    LayoutNode* Owner::NodeOf(const FocusTargetNode& t) const { return t.Owner(); }

    void Owner::FirstTargets(LayoutNode& n, std::vector<FocusTargetNode*>& out) const {
        for (auto& m : n.m_Nodes) if (auto* t = m->AsFocusTarget()) { out.push_back(t); return; }
        for (LayoutNode* c : n.m_Children) FirstTargets(*c, out);
    }

    std::vector<FocusTargetNode*> Owner::FocusChildren(const FocusTargetNode& t) const {
        std::vector<FocusTargetNode*> out;
        if (t.m_IsRoot) { FirstTargets(*m_Root, out); return out; }
        LayoutNode* n = NodeOf(t);
        if (!n) return out;
        // A later target in the same chain is the only child; else the first targets below.
        bool after = false;
        for (auto& m : n->m_Nodes) {
            if (m.get() == &t) { after = true; continue; }
            if (after) if (auto* c = m->AsFocusTarget()) { out.push_back(c); return out; }
        }
        for (LayoutNode* c : n->m_Children) FirstTargets(*c, out);
        return out;
    }

    FocusTargetNode* Owner::FocusParent(const FocusTargetNode& t) const {
        if (t.m_IsRoot) return nullptr;
        LayoutNode* n = NodeOf(t);
        if (!n) return nullptr;
        FocusTargetNode* before = nullptr;
        for (auto& m : n->m_Nodes) {
            if (m.get() == &t) break;
            if (auto* p = m->AsFocusTarget()) before = p;
        }
        if (before) return before;
        for (LayoutNode* p = n->m_Parent; p; p = p->m_Parent)
            for (auto it = p->m_Nodes.rbegin(); it != p->m_Nodes.rend(); ++it)
                if (auto* ft = (*it)->AsFocusTarget()) return ft;
        return const_cast<FocusTargetNode*>(&m_RootFocus);
    }

    FocusState Owner::StateOf(const FocusTargetNode& t) const {
        if (!m_ActiveFocus) return FocusState::Inactive;
        if (&t == m_ActiveFocus) return m_FocusCaptured ? FocusState::Captured : FocusState::Active;
        for (FocusTargetNode* p = FocusParent(*m_ActiveFocus); p; p = FocusParent(*p))
            if (p == &t) return FocusState::ActiveParent;
        return FocusState::Inactive;
    }

    bool Owner::CanFocus(const FocusTargetNode& t) const {
        if (t.m_IsRoot) return false;
        switch (t.focusability) {
            case Focusability::Always: return true;
            case Focusability::Never: return false;
            case Focusability::SystemDefined: return m_InputMode.Peek() != InputMode::Touch;
        }
        return false;
    }

    bool Owner::Eligible(const FocusTargetNode& t) const {
        LayoutNode* n = NodeOf(t);
        return n && n->m_Owner == this && (n->IsPlaced() || n == m_Root.get());
    }

    Rect Owner::FocusRect(const FocusTargetNode& t) const {
        if (t.m_IsRoot) return { 0, 0, float(m_Window.width), float(m_Window.height) };
        LayoutNode* n = NodeOf(t);
        if (!n) return {};
        const Offset o = n->PositionInRoot();
        const IntSize s = n->Size();
        return { o.x, o.y, o.x + s.width, o.y + s.height };
    }

    FocusTargetNode* Owner::ActiveChild(const FocusTargetNode& t) const {
        for (FocusTargetNode* c : FocusChildren(t)) if (HasFocus(StateOf(*c))) return c;
        return nullptr;
    }

    // --- transactions -------------------------------------------------------------------------

    void Owner::SetActive(FocusTargetNode* t) {
        m_ActiveFocus = t;
        m_FocusCaptured = false;
        DispatchFocusEvents();
    }

    void Owner::DispatchFocusEvents() {
        // Every target and focus-event node re-derives its state and reports changes. A focus
        // change is rare enough that a walk of the tree is fine.
        std::vector<LayoutNode*> stack{ m_Root.get() };
        while (!stack.empty()) {
            LayoutNode* n = stack.back();
            stack.pop_back();
            for (std::size_t i = 0; i < n->m_Nodes.size(); ++i) {
                ModifierNode* m = n->m_Nodes[i].get();
                if (auto* t = m->AsFocusTarget()) {
                    const FocusState now = StateOf(*t);
                    if (now != t->m_LastReported) {
                        const FocusState prev = t->m_LastReported;
                        t->m_LastReported = now;
                        if (t->onFocusChange) t->onFocusChange(prev, now);
                    }
                }
                if (auto* e = m->AsFocusEvent()) {
                    // The first target at or after this node in the chain, else the children's.
                    FocusTargetNode* below = nullptr;
                    for (std::size_t j = i + 1; j < n->m_Nodes.size() && !below; ++j) below = n->m_Nodes[j]->AsFocusTarget();
                    FocusState st = FocusState::Inactive;
                    if (below) st = StateOf(*below);
                    else {
                        std::vector<FocusTargetNode*> firsts;
                        for (LayoutNode* c : n->m_Children) FirstTargets(*c, firsts);
                        for (auto* f : firsts) if (HasFocus(StateOf(*f))) { st = StateOf(*f); break; }
                    }
                    e->OnFocusEvent(st);
                }
            }
            for (LayoutNode* c : n->m_Children) stack.push_back(c);
        }
    }

    bool Owner::RequestFocus(FocusTargetNode& t, FocusDirection dir) {
        if (!CanFocus(t)) return FindChildForEnter(t, dir, [&](FocusTargetNode& c) { return RequestFocus(c, dir); });
        if (m_ActiveFocus == &t) { DispatchFocusEvents(); return true; }
        if (m_FocusCaptured) return false;      // Captured only yields to a forced clear
        // The host view must hold platform focus for keys and IME to arrive.
        if (m_RequestHostFocus && !m_ActiveFocus && !m_RequestHostFocus()) return false;
        SetActive(&t);
        return true;
    }

    bool Owner::ClearFocus(bool force) {
        if (!m_ActiveFocus) return true;
        if (m_FocusCaptured && !force) return false;
        SetActive(nullptr);
        return true;
    }

    bool Owner::CaptureFocus() {
        if (!m_ActiveFocus) return false;
        m_FocusCaptured = true;
        DispatchFocusEvents();
        return true;
    }
    bool Owner::FreeFocus() {
        if (!m_FocusCaptured) return false;
        m_FocusCaptured = false;
        DispatchFocusEvents();
        return true;
    }

    void Owner::OnHostFocusChanged(bool focused) {
        if (!focused) ClearFocus(true);
    }

    // --- 2D search (TwoDimensionalFocusSearch.kt) -----------------------------------------------

    namespace {
        bool IsCandidate(const Rect& r, const Rect& f, FocusDirection d) {
            switch (d) {
                case FocusDirection::Left: return (f.right > r.right || f.left >= r.right) && f.left > r.left;
                case FocusDirection::Right: return (f.left < r.left || f.right <= r.left) && f.right < r.right;
                case FocusDirection::Up: return (f.bottom > r.bottom || f.top >= r.bottom) && f.top > r.top;
                case FocusDirection::Down: return (f.top < r.top || f.bottom <= r.top) && f.bottom < r.bottom;
                default: return false;
            }
        }
        float Major(const Rect& r, const Rect& s, FocusDirection d) {
            float v = 0;
            switch (d) {
                case FocusDirection::Left: v = s.left - r.right; break;
                case FocusDirection::Right: v = r.left - s.right; break;
                case FocusDirection::Up: v = s.top - r.bottom; break;
                case FocusDirection::Down: v = r.top - s.bottom; break;
                default: break;
            }
            return std::max(v, 0.0f);
        }
        float MajorFar(const Rect& r, const Rect& s, FocusDirection d) {
            float v = 0;
            switch (d) {
                case FocusDirection::Left: v = s.left - r.left; break;
                case FocusDirection::Right: v = r.right - s.right; break;
                case FocusDirection::Up: v = s.top - r.top; break;
                case FocusDirection::Down: v = r.bottom - s.bottom; break;
                default: break;
            }
            return std::max(v, 1.0f);
        }
        float Minor(const Rect& r, const Rect& f, FocusDirection d) {
            if (d == FocusDirection::Left || d == FocusDirection::Right)
                return (f.top + f.Height() / 2) - (r.top + r.Height() / 2);
            return (f.left + f.Width() / 2) - (r.left + r.Width() / 2);
        }
        bool InBeam(const Rect& r, const Rect& s, FocusDirection d) {
            if (d == FocusDirection::Left || d == FocusDirection::Right) return r.bottom > s.top && r.top < s.bottom;
            return r.right > s.left && r.left < s.right;
        }
        bool InDirection(const Rect& r, const Rect& s, FocusDirection d) {
            switch (d) {
                case FocusDirection::Left: return s.left >= r.right;
                case FocusDirection::Right: return s.right <= r.left;
                case FocusDirection::Up: return s.top >= r.bottom;
                case FocusDirection::Down: return s.bottom <= r.top;
                default: return false;
            }
        }
        bool BeamBeats(const Rect& s, const Rect& r1, const Rect& r2, FocusDirection d) {
            if (InBeam(r2, s, d) || !InBeam(r1, s, d)) return false;
            if (!InDirection(r2, s, d)) return true;
            if (d == FocusDirection::Left || d == FocusDirection::Right) return true;
            return Major(r1, s, d) < MajorFar(r2, s, d);
        }
        long long Weighted(const Rect& c, const Rect& f, FocusDirection d) {
            const long long major = (long long)Major(c, f, d), minor = (long long)Minor(c, f, d);
            return 13 * major * major + minor * minor;
        }
        bool IsBetterCandidate(const Rect& proposed, const Rect& current, const Rect& focused, FocusDirection d) {
            if (!IsCandidate(proposed, focused, d)) return false;
            if (!IsCandidate(current, focused, d)) return true;
            if (BeamBeats(focused, proposed, current, d)) return true;
            if (BeamBeats(focused, current, proposed, d)) return false;
            return Weighted(proposed, focused, d) < Weighted(current, focused, d);
        }
    }

    bool Owner::FindChildForEnter(FocusTargetNode& t, FocusDirection dir, const OnFound& onFound) {
        // collectAccessibleChildren: focusable descendants, flattening non-focusable groups.
        std::vector<FocusTargetNode*> acc;
        std::function<void(FocusTargetNode&)> collect = [&](FocusTargetNode& n) {
            for (FocusTargetNode* c : FocusChildren(n)) {
                if (!Eligible(*c)) continue;
                if (CanFocus(*c)) acc.push_back(c); else collect(*c);
            }
        };
        collect(t);
        if (acc.size() <= 1) return !acc.empty() && onFound(*acc.front());
        const FocusDirection d = dir == FocusDirection::Enter ? FocusDirection::Right : dir;   // b/244528858
        const Rect fr = FocusRect(t);
        const Rect start = (d == FocusDirection::Right || d == FocusDirection::Down) ? Rect{ fr.left, fr.top, fr.left, fr.top }
                                                                                     : Rect{ fr.right, fr.bottom, fr.right, fr.bottom };
        Rect best = start;
        switch (d) {
            case FocusDirection::Left: best = start.Translate({ start.Width() + 1, 0 }); break;
            case FocusDirection::Right: best = start.Translate({ -(start.Width() + 1), 0 }); break;
            case FocusDirection::Up: best = start.Translate({ 0, start.Height() + 1 }); break;
            case FocusDirection::Down: best = start.Translate({ 0, -(start.Height() + 1) }); break;
            default: break;
        }
        FocusTargetNode* result = nullptr;
        for (auto* c : acc) {
            const Rect r = FocusRect(*c);
            if (IsBetterCandidate(r, best, start, d)) { best = r; result = c; }
        }
        return result && onFound(*result);
    }

    bool Owner::SearchChildren2D(FocusTargetNode& t, Rect focused, FocusDirection d, const OnFound& onFound) {
        auto children = FocusChildren(t);
        while (!children.empty()) {
            Rect best = focused;
            switch (d) {
                case FocusDirection::Left: best = focused.Translate({ focused.Width() + 1, 0 }); break;
                case FocusDirection::Right: best = focused.Translate({ -(focused.Width() + 1), 0 }); break;
                case FocusDirection::Up: best = focused.Translate({ 0, focused.Height() + 1 }); break;
                case FocusDirection::Down: best = focused.Translate({ 0, -(focused.Height() + 1) }); break;
                default: return false;
            }
            FocusTargetNode* next = nullptr;
            for (auto* c : children) {
                if (!Eligible(*c)) continue;
                const Rect r = FocusRect(*c);
                if (IsBetterCandidate(r, best, focused, d)) { best = r; next = c; }
            }
            if (!next) return false;
            if (CanFocus(*next)) return onFound(*next);
            if (SearchChildren2D(*next, focused, d, onFound)) return true;
            std::erase(children, next);
        }
        return false;
    }

    bool Owner::Search2D(FocusTargetNode& t, FocusDirection d, const Rect* previous, const OnFound& onFound) {
        switch (StateOf(t)) {
            case FocusState::Inactive:
                if (CanFocus(t)) return onFound(t);
                if (!previous) return FindChildForEnter(t, d, onFound);
                return SearchChildren2D(t, *previous, d, onFound);
            case FocusState::ActiveParent: {
                FocusTargetNode* child = ActiveChild(t);
                if (!child) return false;
                if (StateOf(*child) == FocusState::ActiveParent) {
                    if (Search2D(*child, d, previous, onFound)) return true;
                    return SearchChildren2D(t, previous ? *previous : FocusRect(*m_ActiveFocus), d, onFound);
                }
                return SearchChildren2D(t, previous ? *previous : FocusRect(*child), d, onFound);
            }
            default:
                return FindChildForEnter(t, d, onFound);
        }
    }

    // --- 1D search (OneDimensionalFocusSearch.kt) ----------------------------------------------

    std::vector<FocusTargetNode*> Owner::SortedChildren(const FocusTargetNode& t) const {
        auto children = FocusChildren(t);
        auto path = [](LayoutNode* n) { std::vector<LayoutNode*> p; for (; n; n = n->m_Parent) p.insert(p.begin(), n); return p; };
        std::stable_sort(children.begin(), children.end(), [&](FocusTargetNode* a, FocusTargetNode* b) {
            const bool ea = Eligible(*a), eb = Eligible(*b);
            if (!ea || !eb) return ea && !eb;
            LayoutNode *na = NodeOf(*a), *nb = NodeOf(*b);
            if (na == nb) return false;
            auto pa = path(na), pb = path(nb);
            for (std::size_t i = 0; i < std::min(pa.size(), pb.size()); ++i)
                if (pa[i] != pb[i]) return pa[i]->m_PlaceOrder < pb[i]->m_PlaceOrder;
            return false;
        });
        return children;
    }

    bool Owner::Forward(FocusTargetNode& t, const OnFound& onFound) {
        auto pick = [&] {
            for (auto* c : SortedChildren(t)) if (Eligible(*c) && Forward(*c, onFound)) return true;
            return false;
        };
        switch (StateOf(t)) {
            case FocusState::ActiveParent: {
                FocusTargetNode* child = ActiveChild(t);
                return (child && Forward(*child, onFound)) || (child && SearchChildren1D(t, *child, FocusDirection::Next, onFound));
            }
            case FocusState::Active: case FocusState::Captured: return pick();
            case FocusState::Inactive: return CanFocus(t) ? onFound(t) : pick();
        }
        return false;
    }

    bool Owner::Backward(FocusTargetNode& t, const OnFound& onFound) {
        auto pick = [&] {
            auto c = SortedChildren(t);
            for (auto it = c.rbegin(); it != c.rend(); ++it) if (Eligible(**it) && Backward(**it, onFound)) return true;
            return false;
        };
        switch (StateOf(t)) {
            case FocusState::ActiveParent: {
                FocusTargetNode* child = ActiveChild(t);
                if (!child) return false;
                if (StateOf(*child) == FocusState::ActiveParent)
                    return Backward(*child, onFound) || SearchChildren1D(t, *child, FocusDirection::Previous, onFound) ||
                           (CanFocus(*child) && onFound(*child));
                return SearchChildren1D(t, *child, FocusDirection::Previous, onFound);
            }
            case FocusState::Active: case FocusState::Captured: return pick();
            case FocusState::Inactive: return pick() || (CanFocus(t) && onFound(t));
        }
        return false;
    }

    bool Owner::SearchChildren1D(FocusTargetNode& t, FocusTargetNode& focused, FocusDirection d, const OnFound& onFound) {
        auto children = SortedChildren(t);
        auto it = std::find(children.begin(), children.end(), &focused);
        if (d == FocusDirection::Next) {
            for (auto i = it == children.end() ? it : it + 1; i != children.end(); ++i)
                if (Eligible(**i) && Forward(**i, onFound)) return true;
        } else if (it != children.end()) {
            for (auto i = std::make_reverse_iterator(it); i != children.rend(); ++i)
                if (Eligible(**i) && Backward(**i, onFound)) return true;
        }
        if (d == FocusDirection::Next || !CanFocus(t) || t.m_IsRoot) return false;
        return onFound(t);
    }

    bool Owner::MoveFocus(FocusDirection d) {
        FocusTargetNode* before = m_ActiveFocus;
        auto request = [&](FocusTargetNode& n) { return RequestFocus(n, d); };
        bool found = false;
        switch (d) {
            case FocusDirection::Next: found = Forward(m_RootFocus, request); break;
            case FocusDirection::Previous: found = Backward(m_RootFocus, request); break;
            case FocusDirection::Left: case FocusDirection::Right: case FocusDirection::Up: case FocusDirection::Down:
                found = Search2D(m_RootFocus, d, nullptr, request); break;
            case FocusDirection::Enter:
                if (m_ActiveFocus) found = FindChildForEnter(*m_ActiveFocus, d, request);
                break;
            case FocusDirection::Exit:
                if (m_ActiveFocus)
                    for (FocusTargetNode* p = FocusParent(*m_ActiveFocus); p && !p->m_IsRoot; p = FocusParent(*p))
                        if (CanFocus(*p)) { found = RequestFocus(*p, d); break; }
                break;
        }
        if (found && m_ActiveFocus != before) return true;
        // 1D wraps around like the Android framework: clear, then take focus from the start.
        if ((d == FocusDirection::Next || d == FocusDirection::Previous) && before && ClearFocus(false)) {
            const bool wrapped = d == FocusDirection::Next ? Forward(m_RootFocus, request) : Backward(m_RootFocus, request);
            return wrapped;
        }
        return false;
    }

    // --- keys -----------------------------------------------------------------------------------

    bool Owner::DispatchKeyEvent(const KeyEvent& e) {
        // A key going up that was never seen going down is dropped (FocusOwnerImpl.validateKeyEvent).
        if (e.type == KeyEventType::KeyDown) {
            if (std::find(m_KeysDown.begin(), m_KeysDown.end(), e.keyCode) == m_KeysDown.end()) m_KeysDown.push_back(e.keyCode);
        } else if (e.type == KeyEventType::KeyUp) {
            auto it = std::find(m_KeysDown.begin(), m_KeysDown.end(), e.keyCode);
            if (it == m_KeysDown.end()) return false;
            m_KeysDown.erase(it);
        }
        if (m_InputMode.Peek() != InputMode::Keyboard && e.keyCode != keys::Back) m_InputMode.Set(InputMode::Keyboard);

        // Path of key nodes root -> focused: ancestors' chains, then the focused node's chain up
        // to the next focus target after the active one.
        std::vector<KeyInputModifier*> path;
        if (m_ActiveFocus) {
            LayoutNode* n = NodeOf(*m_ActiveFocus);
            std::vector<LayoutNode*> line;
            for (LayoutNode* p = n ? n->m_Parent : nullptr; p; p = p->m_Parent) line.insert(line.begin(), p);
            for (LayoutNode* p : line)
                for (auto& m : p->m_Nodes) if (auto* k = m->AsKeyInput()) path.push_back(k);
            if (n) {
                bool passedActive = false;
                for (auto& m : n->m_Nodes) {
                    if (passedActive && m->AsFocusTarget()) break;
                    if (m.get() == m_ActiveFocus) passedActive = true;
                    if (auto* k = m->AsKeyInput()) path.push_back(k);
                }
            }
        }
        for (KeyInputModifier* k : path) if (k->OnPreKeyEvent(e)) return true;
        for (auto it = path.rbegin(); it != path.rend(); ++it) if ((*it)->OnKeyEvent(e)) return true;

        // Root handler (AndroidComposeView.onKeyEvent + KeyEvent.toFocusDirection).
        if (e.type != KeyEventType::KeyDown) return false;
        std::optional<FocusDirection> d;
        switch (e.keyCode) {
            case keys::NavigatePrevious: d = FocusDirection::Previous; break;
            case keys::NavigateNext: d = FocusDirection::Next; break;
            case keys::Tab: d = e.Shift() ? FocusDirection::Previous : FocusDirection::Next; break;
            case keys::DpadRight: d = FocusDirection::Right; break;
            case keys::DpadLeft: d = FocusDirection::Left; break;
            case keys::DpadUp: case keys::PageUp: d = FocusDirection::Up; break;
            case keys::DpadDown: case keys::PageDown: d = FocusDirection::Down; break;
            case keys::DpadCenter: case keys::Enter: case keys::NumpadEnter: d = FocusDirection::Enter; break;
            case keys::Back: case keys::Escape: d = FocusDirection::Exit; break;
            default: break;
        }
        if (!d) return false;
        return MoveFocus(*d);
    }

}
