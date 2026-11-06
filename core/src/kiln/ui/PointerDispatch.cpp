#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace kiln::ui {

    // Hit test (NodeCoordinator.hitTest): children topmost first, the first child hit wins; a
    // node's own pointer modifiers join the path when the point is inside its bounds. For touch,
    // a node with pointer input smaller than the minimum touch target also counts within the
    // expanded area, and the nearest such node is used when nothing was hit directly.
    namespace {
        gfx::Matrix Invert(const gfx::Matrix& m) {
            const float det = m.a * m.d - m.b * m.c;
            if (std::abs(det) < 1e-12f) return {};
            const float i = 1 / det;
            return { m.d * i, -m.b * i, -m.c * i, m.a * i, (m.c * m.f - m.d * m.e) * i, (m.b * m.e - m.a * m.f) * i };
        }
    }

    void Owner::HitTest(LayoutNode& n, Offset p, bool touch, std::vector<HitEntry>& path,
                        std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors) {
        if (!n.IsPlaced() && &n != m_Root.get()) return;
        // Test in the node's own space, through any graphicsLayer transforms above it.
        const Offset local = Invert(n.LocalToRoot()).Map(p);
        const IntSize size = n.Size();
        const Rect bounds{ 0, 0, float(size.width), float(size.height) };
        const bool inside = bounds.Contains(local);
        const auto mods = n.PointerModifiers();

        std::vector<HitEntry> own;
        for (auto* m : mods) own.push_back({ &n, m });
        const std::size_t ancestorsBefore = ancestors.size();
        if (inside) ancestors.insert(ancestors.end(), own.begin(), own.end());

        if (touch && !inside && !mods.empty()) {
            const float minPx = m_Density.ToPx(m_Config.minimumTouchTargetSize);
            const float ex = std::max(0.0f, (minPx - size.width) / 2), ey = std::max(0.0f, (minPx - size.height) / 2);
            const Rect expanded{ bounds.left - ex, bounds.top - ey, bounds.right + ex, bounds.bottom + ey };
            if ((ex > 0 || ey > 0) && expanded.Contains(local)) {
                const float dx = std::max({ bounds.left - local.x, 0.0f, local.x - bounds.right });
                const float dy = std::max({ bounds.top - local.y, 0.0f, local.y - bounds.bottom });
                const float d = dx * dx + dy * dy;
                if (d < nearDistance) {
                    nearDistance = d;
                    near = ancestors;
                    near.insert(near.end(), own.begin(), own.end());
                }
            }
        }

        // A clipping layer hides its children outside its bounds from hit testing too.
        bool clips = false;
        for (const ModifierNode* m : n.ModifierNodes()) if (m->ClipsChildren()) { clips = true; break; }
        if (clips && !inside) { ancestors.resize(ancestorsBefore); return; }

        // Children in reverse draw order (zIndex, then placement order).
        std::vector<LayoutNode*> order(n.Children().begin(), n.Children().end());
        std::stable_sort(order.begin(), order.end(), [](const LayoutNode* a, const LayoutNode* b) {
            return a->ZIndex() < b->ZIndex();
        });
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const std::size_t before = path.size();
            HitTest(**it, p, touch, path, near, nearDistance, ancestors);
            if (path.size() > before) break;
        }
        if (inside && path.empty() && !ancestors.empty()) path = ancestors;
        ancestors.resize(ancestorsBefore);
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
            if (raw.type != PointerEventType::Exit) HitTest(*m_Root, rp.position, false, path, near, nearDistance, ancestors);
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
                HitTest(*m_Root, rp.position, rp.type == PointerType::Touch, path, near, nearDistance, ancestors);
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
            const Tracked& t = m_Pointers[id];
            const gfx::Matrix toLocal = Invert(entry.node->LocalToRoot());
            PointerInputChange c;
            c.id = id;
            c.uptimeMillis = raw.uptimeMillis;
            for (const auto& r : raw.pointers) if (r.id == id) c.position = toLocal.Map(r.position);
            c.previousPosition = toLocal.Map(t.position);
            c.type = t.type;
            c.consumed = std::make_shared<bool>(false);
            ev.changes.push_back(c);
            for (PointerEventPass pass : { PointerEventPass::Initial, PointerEventPass::Main, PointerEventPass::Final })
                entry.mod->OnPointerEvent(ev, pass, entry.node->Size());
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

        auto eventFor = [&](const TreeNode& tn) {
            PointerEvent ev;
            ev.type = raw.type;
            // A handler the hover just reached sees Enter.
            if (raw.type == PointerEventType::Move && entered.count(tn.entry.mod)) ev.type = PointerEventType::Enter;
            ev.buttons = raw.buttons;
            ev.classification = raw.classification;
            const gfx::Matrix toLocal = Invert(tn.entry.node->LocalToRoot());
            for (std::int64_t id : tn.pointers) {
                const Tracked& t = m_Pointers[id];
                const RawPointer* rp = nullptr;
                for (const auto& r : raw.pointers) if (r.id == id) rp = &r;
                PointerInputChange c;
                c.id = id;
                c.uptimeMillis = raw.uptimeMillis;
                c.position = toLocal.Map(rp->position);
                c.pressed = rp->down;
                c.previousUptimeMillis = t.uptime;
                c.previousPosition = toLocal.Map(t.position);
                c.previousPressed = t.pressed;
                c.type = rp->type;
                c.scrollDelta = rp->scrollDelta;
                for (const HistoricalChange& h : rp->historical) c.historical.push_back({ h.uptimeMillis, toLocal.Map(h.position) });
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
                PointerEvent ev = eventFor(tree[i]);
                tree[i].entry.mod->OnPointerEvent(ev, pass, tree[i].entry.node->Size());
            }
            for (std::size_t c : tree[i].children) down(c, pass);
        };
        std::function<void(std::size_t)> up = [&](std::size_t i) {
            for (std::size_t c : tree[i].children) up(c);
            if (alive(tree[i].entry)) {
                PointerEvent ev = eventFor(tree[i]);
                tree[i].entry.mod->OnPointerEvent(ev, PointerEventPass::Main, tree[i].entry.node->Size());
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
