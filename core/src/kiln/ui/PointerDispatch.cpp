#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace kiln::ui {

    // Hit test (NodeCoordinator.hitTest): each pointer modifier is tested against the coordinator
    // it sits in (the bounds of the layout modifier after it, through the layers before it); a
    // clipping layer that misses ends the node's chain and its children. Children go topmost
    // first and the first child hit wins. A modifier the point is outside of joins the path only
    // when something inside it is hit (speculativeHit). For touch, a modifier smaller than the
    // minimum touch target also counts within the expanded area, and the nearest such one is
    // used when nothing was hit directly.
    namespace {
        using Mat3 = LayoutNode::Mat3;
        constexpr Mat3 kIdentity3{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        // Root -> local through a (projective) local -> root matrix; false behind the camera.
        bool ToLocal(const Mat3& m, Offset p, Offset& out) {
            const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
            const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
            const double det = a * A + b * B + c * C;
            if (std::abs(det) < 1e-12) return false;
            const double inv[9] = { A / det, -(b * i - c * h) / det, (b * f - c * e) / det,
                                    B / det, (a * i - c * g) / det, -(a * f - c * d) / det,
                                    C / det, -(a * h - b * g) / det, (a * e - b * d) / det };
            const double w = inv[6] * p.x + inv[7] * p.y + inv[8];
            if (w <= 1e-9) return false;
            out = { float((inv[0] * p.x + inv[1] * p.y + inv[2]) / w), float((inv[3] * p.x + inv[4] * p.y + inv[5]) / w) };
            return true;
        }
        struct LocalMap {
            Mat3 m = kIdentity3;
            Offset operator()(Offset p) const { Offset o; return ToLocal(m, p, o) ? o : Offset{ INFINITY, INFINITY }; }
        };
    }

    void Owner::HitTest(LayoutNode& n, const Mat3& parentInner, Offset p, bool touch, std::vector<HitEntry>& path,
                        std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors) {
        if (!n.IsPlaced() && &n != m_Root.get()) return;
        std::vector<LayoutNode::HitStep> steps;
        Mat3 inner;
        n.HitSteps(parentInner, steps, inner);
        const std::size_t ancestorsBefore = ancestors.size();
        for (const LayoutNode::HitStep& step : steps) {
            Offset local;
            const bool mapped = ToLocal(step.toRoot, p, local);
            const Rect bounds{ 0, 0, step.size.width, step.size.height };
            const bool inBounds = mapped && bounds.Contains(local);
            if (!step.mod) {
                // A clipping layer hides the rest of the chain and the children outside it.
                const bool inClip = inBounds && (!step.clip || step.clip->LayerClipContains(step.size, local, m_Direction, m_Density));
                if (!inClip) { ancestors.resize(ancestorsBefore); return; }
                continue;
            }
            ancestors.push_back({ &n, step.mod, inBounds });
            if (touch && !inBounds && mapped) {
                const float minPx = m_Density.ToPx(m_Config.minimumTouchTargetSize);
                const float ex = std::max(0.0f, (minPx - bounds.Width()) / 2), ey = std::max(0.0f, (minPx - bounds.Height()) / 2);
                const Rect expanded{ bounds.left - ex, bounds.top - ey, bounds.right + ex, bounds.bottom + ey };
                if ((ex > 0 || ey > 0) && expanded.Contains(local)) {
                    const float dx = std::max({ bounds.left - local.x, 0.0f, local.x - bounds.right });
                    const float dy = std::max({ bounds.top - local.y, 0.0f, local.y - bounds.bottom });
                    const float d = dx * dx + dy * dy;
                    // Later modifiers of the node at the same distance extend its path.
                    if (d < nearDistance || (d == nearDistance && !near.empty() && near.back().node == &n)) {
                        nearDistance = d;
                        near = ancestors;
                    }
                }
            }
        }

        // Children in reverse draw order (zIndex, then placement order).
        std::vector<LayoutNode*> order(n.Children().begin(), n.Children().end());
        std::stable_sort(order.begin(), order.end(), [](const LayoutNode* a, const LayoutNode* b) {
            return a->ZIndex() < b->ZIndex();
        });
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const std::size_t before = path.size();
            HitTest(**it, inner, p, touch, path, near, nearDistance, ancestors);
            if (path.size() > before) break;
        }
        // No child hit: the path ends at this node's last modifier the point is inside.
        if (path.empty())
            for (std::size_t i = ancestors.size(); i > ancestorsBefore; --i)
                if (ancestors[i - 1].inBounds) { path.assign(ancestors.begin(), ancestors.begin() + std::ptrdiff_t(i)); break; }
        ancestors.resize(ancestorsBefore);
    }

    namespace {
        // A modifier's event space: its coordinator's local coordinates and size.
        LocalMap SpaceOf(const LayoutNode& node, const PointerInputModifier* mod, IntSize& size) {
            LocalMap map;
            Size s;
            if (node.PointerSpace(mod, map.m, s)) { size = { int(s.width), int(s.height) }; return map; }
            size = node.Size();
            const gfx::Matrix l = node.LocalToRoot();
            map.m = { l.a, l.c, l.e, l.b, l.d, l.f, 0, 0, 1 };
            return map;
        }
    }

    void Owner::ProcessPointer(const RawPointerEvent& raw) {
        if (raw.cancel) {
            std::unordered_set<PointerInputModifier*> seen;
            for (auto& [id, t] : m_Pointers)
                for (auto& e : t.path)
                    if (seen.insert(e.mod).second) e.mod->OnCancelPointerInput();
            m_Pointers.clear();
            return;
        }

        // Hover (a mouse or stylus that is not pressed) is hit-tested on every event: handlers that
        // left the path get Exit, new ones Enter (HitPathTracker / PointerInputEventProcessor).
        std::vector<std::pair<HitEntry, std::int64_t>> exited;
        std::unordered_set<PointerInputModifier*> entered;
        for (const RawPointer& rp : raw.pointers) {
            auto it = m_Pointers.find(rp.id);
            const bool wasDown = it != m_Pointers.end() && it->second.pressed;
            if (rp.type == PointerType::Touch || rp.down || wasDown || it == m_Pointers.end()) continue;
            std::vector<HitEntry> path, near, ancestors;
            float nearDistance = INFINITY;
            if (raw.type != PointerEventType::Exit) HitTest(*m_Root, kIdentity3, rp.position, false, path, near, nearDistance, ancestors);
            auto has = [](const std::vector<HitEntry>& v, const HitEntry& e) {
                return std::any_of(v.begin(), v.end(), [&](const HitEntry& x) { return x.mod == e.mod; });
            };
            for (const HitEntry& e : it->second.path) if (!has(path, e)) exited.push_back({ e, rp.id });
            for (const HitEntry& e : path) if (!has(it->second.path, e)) entered.insert(e.mod);
            it->second.path = std::move(path);
        }

        // New presses (and hovering mice without a path) are hit-tested.
        for (const RawPointer& rp : raw.pointers) {
            auto it = m_Pointers.find(rp.id);
            const bool wasDown = it != m_Pointers.end() && it->second.pressed;
            if ((rp.down && !wasDown) || it == m_Pointers.end()) {
                const bool fresh = it == m_Pointers.end();
                std::vector<HitEntry> path, near, ancestors;
                float nearDistance = INFINITY;
                HitTest(*m_Root, kIdentity3, rp.position, rp.type == PointerType::Touch, path, near, nearDistance, ancestors);
                if (path.empty()) path = near;
                Tracked& t = m_Pointers[rp.id];
                // A new pointer's previous position is its current one (no movement on the down).
                if (it == m_Pointers.end() || (rp.down && !wasDown)) { t.position = rp.position; t.uptime = raw.uptimeMillis; }
                t.path = std::move(path);
                t.type = rp.type;
                if (fresh && !rp.down && rp.type != PointerType::Touch)
                    for (const HitEntry& e : t.path) entered.insert(e.mod);
            }
        }

        // Exit to the handlers the hover left (all three passes, that handler alone).
        for (const auto& [entry, id] : exited) {
            if (!entry.node->GetOwner()) continue;   // detached meanwhile
            PointerEvent ev;
            ev.type = PointerEventType::Exit;
            ev.buttons = raw.buttons;
            ev.keyboardModifiers = { raw.metaState };
            const Tracked& t = m_Pointers[id];
            IntSize size;
            const LocalMap toLocal = SpaceOf(*entry.node, entry.mod, size);
            PointerInputChange c;
            c.id = id;
            c.uptimeMillis = raw.uptimeMillis;
            for (const auto& r : raw.pointers) if (r.id == id) c.position = toLocal(r.position);
            c.previousPosition = toLocal(t.position);
            c.type = t.type;
            c.consumed = std::make_shared<bool>(false);
            ev.changes.push_back(c);
            for (PointerEventPass pass : { PointerEventPass::Initial, PointerEventPass::Main, PointerEventPass::Final })
                entry.mod->OnPointerEvent(ev, pass, size);
        }

        // Merge the paths into a tree: each distinct modifier gets the pointers whose path has it.
        struct TreeNode { HitEntry entry; std::vector<std::int64_t> pointers; std::vector<std::size_t> children; };
        std::vector<TreeNode> tree;
        std::vector<std::size_t> roots;
        std::unordered_map<std::int64_t, std::shared_ptr<bool>> consumed;
        for (const RawPointer& rp : raw.pointers) {
            consumed[rp.id] = std::make_shared<bool>(false);
            auto& path = m_Pointers[rp.id].path;
            // The parent is held by index: push_back below reallocates `tree`.
            std::size_t parent = SIZE_MAX;
            for (const HitEntry& e : path) {
                auto& level = parent == SIZE_MAX ? roots : tree[parent].children;
                std::size_t found = SIZE_MAX;
                for (std::size_t i : level) if (tree[i].entry.mod == e.mod) { found = i; break; }
                if (found == SIZE_MAX) {
                    tree.push_back({ e, {}, {} });
                    found = tree.size() - 1;
                    (parent == SIZE_MAX ? roots : tree[parent].children).push_back(found);
                }
                tree[found].pointers.push_back(rp.id);
                parent = found;
            }
        }

        auto eventFor = [&](const TreeNode& tn, IntSize& size) {
            PointerEvent ev;
            ev.type = raw.type;
            // A handler the hover just reached sees Enter.
            if (raw.type == PointerEventType::Move && entered.count(tn.entry.mod)) ev.type = PointerEventType::Enter;
            ev.buttons = raw.buttons;
            ev.keyboardModifiers = { raw.metaState };
            ev.classification = raw.classification;
            const LocalMap toLocal = SpaceOf(*tn.entry.node, tn.entry.mod, size);
            for (std::int64_t id : tn.pointers) {
                const Tracked& t = m_Pointers[id];
                const RawPointer* rp = nullptr;
                for (const auto& r : raw.pointers) if (r.id == id) rp = &r;
                PointerInputChange c;
                c.id = id;
                c.uptimeMillis = raw.uptimeMillis;
                c.position = toLocal(rp->position);
                c.pressed = rp->down;
                c.previousUptimeMillis = t.uptime;
                c.previousPosition = toLocal(t.position);
                c.previousPressed = t.pressed;
                c.type = rp->type;
                c.scrollDelta = rp->scrollDelta;
                c.panOffset = rp->panOffset;
                c.scaleFactor = rp->scaleFactor;
                for (const HistoricalChange& h : rp->historical) c.historical.push_back({ h.uptimeMillis, toLocal(h.position), h.panOffset, h.scaleFactor });
                c.consumed = consumed[id];
                ev.changes.push_back(c);
            }
            return ev;
        };

        // A node may be removed by an earlier handler; the entry stays valid only while the
        // modifier is still tracked by some pointer path.
        auto alive = [&](const HitEntry& e) {
            for (auto& [id, t] : m_Pointers)
                for (auto& x : t.path) if (x.mod == e.mod) return true;
            return false;
        };
        std::function<void(std::size_t, PointerEventPass)> down = [&](std::size_t i, PointerEventPass pass) {
            if (alive(tree[i].entry)) {
                IntSize size;
                PointerEvent ev = eventFor(tree[i], size);
                tree[i].entry.mod->OnPointerEvent(ev, pass, size);
            }
            for (std::size_t c : tree[i].children) down(c, pass);
        };
        std::function<void(std::size_t)> up = [&](std::size_t i) {
            for (std::size_t c : tree[i].children) up(c);
            if (alive(tree[i].entry)) {
                IntSize size;
                PointerEvent ev = eventFor(tree[i], size);
                tree[i].entry.mod->OnPointerEvent(ev, PointerEventPass::Main, size);
            }
        };
        for (std::size_t r : roots) down(r, PointerEventPass::Initial);
        for (std::size_t r : roots) up(r);
        for (std::size_t r : roots) down(r, PointerEventPass::Final);

        // Commit this event as the previous state; drop pointers that lifted (touch) or left.
        for (const RawPointer& rp : raw.pointers) {
            Tracked& t = m_Pointers[rp.id];
            t.position = rp.position;
            t.pressed = rp.down;
            t.uptime = raw.uptimeMillis;
        }
        for (auto it = m_Pointers.begin(); it != m_Pointers.end();) {
            bool present = false;
            for (const auto& rp : raw.pointers) if (rp.id == it->first) present = true;
            const bool release = present && !it->second.pressed && it->second.type == PointerType::Touch;
            if (!present || release) it = m_Pointers.erase(it); else ++it;
        }
    }

}
