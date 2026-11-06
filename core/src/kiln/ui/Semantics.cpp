#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Semantics.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace kiln::ui {

    void SemanticsConfiguration::Merge(const SemanticsConfiguration& c) {
        contentDescription.insert(contentDescription.end(), c.contentDescription.begin(), c.contentDescription.end());
        text.insert(text.end(), c.text.begin(), c.text.end());
        if (!editableText) editableText = c.editableText;
        if (!stateDescription) stateDescription = c.stateDescription;
        if (!selected) selected = c.selected;
        if (!collectionItemInfo) collectionItemInfo = c.collectionItemInfo;
        if (!toggleableState) toggleableState = c.toggleableState;
        if (role == Role::None) role = c.role;
        heading = heading || c.heading;
        disabled = disabled || c.disabled;
    }

    namespace {
        // A node's own configuration from its modifiers (leftmost wins for single values).
        bool OwnConfig(LayoutNode& n, SemanticsConfiguration& out) {
            bool any = false;
            for (const ModifierNode* m : n.ModifierNodes())
                if (auto* s = const_cast<ModifierNode*>(m)->AsSemantics()) { s->ApplySemantics(out); any = true; }
            return any && (out.HasContent() || out.mergeDescendants || out.isTraversalGroup);
        }
        Rect Bounds(LayoutNode& n) {
            const Offset o = n.PositionInRoot();
            const IntSize s = n.Size();
            return { o.x, o.y, o.x + s.width, o.y + s.height };
        }
        // Collects descendants' content into `into` (stopping at nodes that merge their own).
        void MergeDescendants(LayoutNode& n, SemanticsConfiguration& into) {
            for (LayoutNode* c : n.Children()) {
                if (!c->IsPlaced()) continue;
                SemanticsConfiguration cc;
                const bool has = OwnConfig(*c, cc);
                if (has && cc.mergeDescendants) continue;   // a separate merged node
                if (has) into.Merge(cc);
                if (!cc.clearAndSet) MergeDescendants(*c, into);
            }
        }
        Rect Intersect(const Rect& a, const Rect& b) {
            Rect r{ std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom) };
            if (r.right < r.left) r.right = r.left;
            if (r.bottom < r.top) r.bottom = r.top;
            return r;
        }
        bool Clips(LayoutNode& n) {
            for (const ModifierNode* m : n.ModifierNodes()) if (m->ClipsChildren()) return true;
            return false;
        }
        // `clip`: the ancestors' clip in root coordinates; bounds are clipped by it
        // (SemanticsNode.boundsInRoot uses the clipped coordinator bounds).
        void Build(LayoutNode& n, const Rect& clip, std::vector<SemanticsNode>& out) {
            if (!n.IsPlaced() && n.Parent()) return;
            const Rect bounds = Bounds(n);
            const Rect inner = Clips(n) ? Intersect(clip, bounds) : clip;
            SemanticsConfiguration cfg;
            if (OwnConfig(n, cfg)) {
                SemanticsNode node;
                node.id = n.SemanticsId();
                node.layout = &n;
                node.boundsInRoot = Intersect(bounds, clip);
                if (cfg.mergeDescendants && !cfg.clearAndSet) MergeDescendants(n, cfg);
                node.config = cfg;
                if (!cfg.mergeDescendants && !cfg.clearAndSet)
                    for (LayoutNode* c : n.Children()) Build(*c, inner, node.children);
                out.push_back(std::move(node));
                return;
            }
            for (LayoutNode* c : n.Children()) Build(*c, inner, out);
        }

        // Integer region as disjoint rectangles (SemanticsRegion, enough for intersect/difference).
        struct IRect { int l, t, r, b; bool Empty() const { return r <= l || b <= t; } };
        struct Region {
            std::vector<IRect> rects;
            bool Empty() const { return rects.empty(); }
            void Intersect(const IRect& c) {
                std::vector<IRect> out;
                for (const IRect& a : rects) {
                    IRect x{ std::max(a.l, c.l), std::max(a.t, c.t), std::min(a.r, c.r), std::min(a.b, c.b) };
                    if (!x.Empty()) out.push_back(x);
                }
                rects = std::move(out);
            }
            void Subtract(const IRect& c) {
                std::vector<IRect> out;
                for (const IRect& a : rects) {
                    const IRect x{ std::max(a.l, c.l), std::max(a.t, c.t), std::min(a.r, c.r), std::min(a.b, c.b) };
                    if (x.Empty()) { out.push_back(a); continue; }
                    if (a.t < x.t) out.push_back({ a.l, a.t, a.r, x.t });
                    if (x.b < a.b) out.push_back({ a.l, x.b, a.r, a.b });
                    if (a.l < x.l) out.push_back({ a.l, x.t, x.l, x.b });
                    if (x.r < a.r) out.push_back({ x.r, x.t, a.r, x.b });
                }
                rects = std::move(out);
            }
            IRect Bounds() const {
                IRect b{ INT_MAX, INT_MAX, INT_MIN, INT_MIN };
                for (const IRect& a : rects) { b.l = std::min(b.l, a.l); b.t = std::min(b.t, a.t); b.r = std::max(b.r, a.r); b.b = std::max(b.b, a.b); }
                return b;
            }
        };
        IRect Round(const Rect& r) { return { int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom)) }; }

        // UncoveredSemanticsNodeTraversal: children are visited topmost first; a node is kept when
        // part of its clipped bounds is still unaccounted for, reports the bounding box of that
        // part, and then covers its bounds for the nodes below it.
        void KeepUncovered(SemanticsNode& node, Region& unaccounted) {
            std::vector<SemanticsNode> kept;
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
                if (unaccounted.Empty()) break;
                const IRect clipped = Round(it->boundsInRoot);
                Region visible = unaccounted;
                visible.Intersect(clipped);
                if (visible.Empty()) continue;
                const IRect b = visible.Bounds();
                it->boundsInRoot = { float(b.l), float(b.t), float(b.r), float(b.b) };
                KeepUncovered(*it, unaccounted);
                unaccounted.Subtract(clipped);
                kept.push_back(std::move(*it));
            }
            std::reverse(kept.begin(), kept.end());
            node.children = std::move(kept);
        }
    }

    SemanticsNode Owner::BuildSemanticsTree() {
        SemanticsNode root;
        root.id = -1;
        root.layout = m_Root.get();
        root.boundsInRoot = { 0, 0, float(m_Window.width), float(m_Window.height) };
        for (LayoutNode* c : m_Root->Children()) Build(*c, root.boundsInRoot, root.children);
        Region unaccounted{ { Round(root.boundsInRoot) } };
        KeepUncovered(root, unaccounted);
        return root;
    }


    namespace {
        std::vector<const SemanticsNode*> SelectableChildren(const SemanticsNode& group) {
            std::vector<const SemanticsNode*> out;
            for (const auto& c : group.children) if (c.config.selected) out.push_back(&c);
            return out;
        }
        // calculateIfHorizontallyStacked: summed center distances, x against y.
        bool HorizontallyStacked(const std::vector<const SemanticsNode*>& items) {
            if (items.size() < 2) return true;
            float dx = 0, dy = 0;
            for (std::size_t i = 1; i < items.size(); ++i) {
                const Rect& a = items[i - 1]->boundsInRoot;
                const Rect& b = items[i]->boundsInRoot;
                dx += std::abs((a.left + a.right) / 2 - (b.left + b.right) / 2);
                dy += std::abs((a.top + a.bottom) / 2 - (b.top + b.bottom) / 2);
            }
            return dy < dx;
        }
    }

    std::optional<std::pair<int, int>> ResolveCollectionInfo(const SemanticsNode& node) {
        if (node.config.collectionInfo) return node.config.collectionInfo;
        if (!node.config.selectableGroup) return std::nullopt;
        const auto items = SelectableChildren(node);
        if (items.empty()) return std::nullopt;
        const int n = int(items.size());
        return HorizontallyStacked(items) ? std::pair{ 1, n } : std::pair{ n, 1 };
    }

    std::optional<CollectionItemInfo> ResolveCollectionItemInfo(const SemanticsNode& node, const SemanticsNode* parent) {
        if (node.config.collectionItemInfo) return node.config.collectionItemInfo;
        if (!parent || !parent->config.selectableGroup || !node.config.selected) return std::nullopt;
        if (const auto& ci = parent->config.collectionInfo; ci && (ci->first < 0 || ci->second < 0)) return std::nullopt;
        const auto items = SelectableChildren(*parent);
        int index = 0;
        for (const auto* c : items) { if (c == &node) break; ++index; }
        CollectionItemInfo info;
        if (HorizontallyStacked(items)) info.columnIndex = index; else info.rowIndex = index;
        return info;
    }

    std::vector<ScrollEvent> ScrollEventTracker::Collect(const SemanticsNode& root, Owner& owner) {
        std::vector<ScrollEvent> out;
        std::unordered_map<int, Old> now;
        std::function<void(const SemanticsNode&)> visit = [&](const SemanticsNode& n) {
            const auto& c = n.config;
            if (c.horizontalScrollAxisRange || c.verticalScrollAxisRange) {
                Old cur;
                if (const auto& r = c.horizontalScrollAxisRange; r && r->value) cur.x = r->value();
                if (const auto& r = c.verticalScrollAxisRange; r && r->value) cur.y = r->value();
                const auto it = m_Old.find(n.id);
                const Offset delta = owner.TakeScrollDelta(n.id);
                if (it != m_Old.end()) {
                    const bool moved = (cur.x && it->second.x && *cur.x != *it->second.x) ||
                                       (cur.y && it->second.y && *cur.y != *it->second.y);
                    if (moved) {
                        ScrollEvent e;
                        e.id = n.id;
                        auto range = [](const ScrollAxisRange& r) {
                            return std::pair{ int(r.value()), int(r.maxValue ? r.maxValue() : 0) };
                        };
                        if (cur.x) e.x = range(*c.horizontalScrollAxisRange);
                        if (cur.y) e.y = range(*c.verticalScrollAxisRange);
                        e.deltaX = int(std::lround(delta.x));
                        e.deltaY = int(std::lround(delta.y));
                        out.push_back(e);
                    }
                }
                now[n.id] = cur;
            }
            for (const auto& ch : n.children) visit(ch);
        };
        visit(root);
        m_Old = std::move(now);
        return out;
    }

}
