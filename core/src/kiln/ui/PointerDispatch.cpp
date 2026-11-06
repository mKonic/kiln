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
    void Owner::HitTest(LayoutNode& n, Offset p, bool touch, std::vector<HitEntry>& path,
                        std::vector<HitEntry>& near, float& nearDistance, std::vector<HitEntry>& ancestors) {
        if (!n.IsPlaced() && &n != m_Root.get()) return;
        const Offset origin = n.PositionInRoot();
        const IntSize size = n.Size();
        const Rect bounds{ origin.x, origin.y, origin.x + size.width, origin.y + size.height };
        const bool inside = bounds.Contains(p);
        const auto mods = n.PointerModifiers();

        std::vector<HitEntry> own;
        for (auto* m : mods) own.push_back({ &n, m });
        const std::size_t ancestorsBefore = ancestors.size();
        if (inside) ancestors.insert(ancestors.end(), own.begin(), own.end());

        if (touch && !inside && !mods.empty()) {
            const float minPx = m_Density.ToPx(m_Config.minimumTouchTargetSize);
            const float ex = std::max(0.0f, (minPx - size.width) / 2), ey = std::max(0.0f, (minPx - size.height) / 2);
            const Rect expanded{ bounds.left - ex, bounds.top - ey, bounds.right + ex, bounds.bottom + ey };
            if ((ex > 0 || ey > 0) && expanded.Contains(p)) {
                const float dx = std::max({ bounds.left - p.x, 0.0f, p.x - bounds.right });
                const float dy = std::max({ bounds.top - p.y, 0.0f, p.y - bounds.bottom });
                const float d = dx * dx + dy * dy;
                if (d < nearDistance) {
                    nearDistance = d;
                    near = ancestors;
                    near.insert(near.end(), own.begin(), own.end());
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

        // New presses (and hovering mice without a path) are hit-tested.
        for (const RawPointer& rp : raw.pointers) {
            auto it = m_Pointers.find(rp.id);
            const bool wasDown = it != m_Pointers.end() && it->second.pressed;
            if ((rp.down && !wasDown) || it == m_Pointers.end()) {
                std::vector<HitEntry> path, near, ancestors;
                float nearDistance = INFINITY;
                HitTest(*m_Root, rp.position, rp.type == PointerType::Touch, path, near, nearDistance, ancestors);
                if (path.empty()) path = near;
                Tracked& t = m_Pointers[rp.id];
                if (rp.down && !wasDown) { t.path = std::move(path); }
                else if (it == m_Pointers.end()) { t.path = std::move(path); t.position = rp.position; t.uptime = raw.uptimeMillis; }
                t.type = rp.type;
            }
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
            ev.buttons = raw.buttons;
            ev.classification = raw.classification;
            const Offset origin = tn.entry.node->PositionInRoot();
            for (std::int64_t id : tn.pointers) {
                const Tracked& t = m_Pointers[id];
                const RawPointer* rp = nullptr;
                for (const auto& r : raw.pointers) if (r.id == id) rp = &r;
                PointerInputChange c;
                c.id = id;
                c.uptimeMillis = raw.uptimeMillis;
                c.position = rp->position - origin;
                c.pressed = rp->down;
                c.previousUptimeMillis = t.uptime;
                c.previousPosition = t.position - origin;
                c.previousPressed = t.pressed;
                c.type = rp->type;
                c.scrollDelta = rp->scrollDelta;
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
