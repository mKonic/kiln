#include "kiln/ui/Accessibility.h"

#include "kiln/graphics/Shape.h"
#include "kiln/text/AnnotatedString.h"
#include "kiln/text/Paragraph.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace kiln::ui {

    using namespace a11y;

    namespace {
        std::u16string U16(std::string_view s) {
            std::u16string out;
            for (std::size_t i = 0; i < s.size();) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                char32_t cp;
                int len;
                if (c < 0x80) { cp = c; len = 1; }
                else if ((c >> 5) == 6) { cp = c & 0x1f; len = 2; }
                else if ((c >> 4) == 14) { cp = c & 0x0f; len = 3; }
                else { cp = c & 0x07; len = 4; }
                for (int k = 1; k < len && i + std::size_t(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + std::size_t(k)]) & 0x3f);
                i += std::size_t(len);
                if (cp >= 0x10000) { cp -= 0x10000; out.push_back(char16_t(0xD800 + (cp >> 10))); out.push_back(char16_t(0xDC00 + (cp & 0x3ff))); }
                else out.push_back(char16_t(cp));
            }
            return out;
        }
        std::string U8(std::u16string_view s) {
            std::string out;
            for (std::size_t i = 0; i < s.size(); ++i) {
                char32_t cp = s[i];
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
                if (cp < 0x80) out.push_back(char(cp));
                else if (cp < 0x800) { out.push_back(char(0xC0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 0x3f))); }
                else if (cp < 0x10000) { out.push_back(char(0xE0 | (cp >> 12))); out.push_back(char(0x80 | ((cp >> 6) & 0x3f))); out.push_back(char(0x80 | (cp & 0x3f))); }
                else { out.push_back(char(0xF0 | (cp >> 18))); out.push_back(char(0x80 | ((cp >> 12) & 0x3f))); out.push_back(char(0x80 | ((cp >> 6) & 0x3f))); out.push_back(char(0x80 | (cp & 0x3f))); }
            }
            return out;
        }
        // trimToSize: at most `size` UTF-16 units, not splitting a surrogate pair.
        std::string TrimToSize(const std::string& text, int size) {
            const std::u16string u = U16(text);
            if (int(u.size()) <= size) return text;
            int len = size;
            if (len > 0 && u[std::size_t(len) - 1] >= 0xD800 && u[std::size_t(len) - 1] < 0xDC00 &&
                u[std::size_t(len)] >= 0xDC00 && u[std::size_t(len)] < 0xE000) --len;
            return U8(u.substr(0, std::size_t(len)));
        }
        std::string Join(const std::vector<std::string>& v, const char* sep) {
            std::string out;
            for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += sep; out += v[i]; }
            return out;
        }
        int RoundI(float v) { return int(std::floor(v + 0.5f)); }   // fastRoundToInt
        IntRect RoundRect(const Rect& r) { return { RoundI(r.left), RoundI(r.top), RoundI(r.right), RoundI(r.bottom) }; }

        bool HasSemantics(LayoutNode& n) {
            for (const ModifierNode* m : n.ModifierNodes()) if (const_cast<ModifierNode*>(m)->AsSemantics()) return true;
            return false;
        }
        // zSortedChildrenList: by zIndex, then placement order.
        std::vector<LayoutNode*> ZSorted(LayoutNode& n) {
            std::vector<LayoutNode*> v(n.Children().begin(), n.Children().end());
            std::stable_sort(v.begin(), v.end(), [](const LayoutNode* a, const LayoutNode* b) {
                if (a->ZIndex() != b->ZIndex()) return a->ZIndex() < b->ZIndex();
                return a->PlaceOrder() < b->PlaceOrder();
            });
            return v;
        }
        bool IsPlacedNode(const LayoutNode& n) { return n.IsPlaced() || !n.Parent(); }

        // SemanticsRegion: an integer region as disjoint rectangles.
        struct Region {
            std::vector<IntRect> rects;
            bool Empty() const { return rects.empty(); }
            void Set(const IntRect& r) { rects.clear(); if (!r.Empty()) rects.push_back(r); }
            bool Intersect(const Region& o) {
                std::vector<IntRect> out;
                for (const IntRect& a : rects)
                    for (const IntRect& c : o.rects) {
                        IntRect x{ std::max(a.left, c.left), std::max(a.top, c.top), std::min(a.right, c.right), std::min(a.bottom, c.bottom) };
                        if (!x.Empty()) out.push_back(x);
                    }
                rects = std::move(out);
                return !rects.empty();
            }
            void Difference(const IntRect& c) {
                std::vector<IntRect> out;
                for (const IntRect& a : rects) {
                    const IntRect x{ std::max(a.left, c.left), std::max(a.top, c.top), std::min(a.right, c.right), std::min(a.bottom, c.bottom) };
                    if (x.Empty()) { out.push_back(a); continue; }
                    if (a.top < x.top) out.push_back({ a.left, a.top, a.right, x.top });
                    if (x.bottom < a.bottom) out.push_back({ a.left, x.bottom, a.right, a.bottom });
                    if (a.left < x.left) out.push_back({ a.left, x.top, x.left, x.bottom });
                    if (x.right < a.right) out.push_back({ x.right, x.top, a.right, x.bottom });
                }
                rects = std::move(out);
            }
            IntRect Bounds() const {
                if (rects.empty()) return {};
                IntRect b{ INT_MAX, INT_MAX, INT_MIN, INT_MIN };
                for (const IntRect& a : rects) {
                    b.left = std::min(b.left, a.left); b.top = std::min(b.top, a.top);
                    b.right = std::max(b.right, a.right); b.bottom = std::max(b.bottom, a.bottom);
                }
                return b;
            }
        };
    }

    // --- the whole unmerged tree --------------------------------------------------------------

    namespace {
        // Every semantics node, visible or not (the traversal order walks invisible ones too).
        struct Tree {
            Owner& owner;
            std::unordered_map<int, AccessibilityNode> all;
            std::unordered_map<LayoutNode*, SemanticsConfiguration> configs;
            float minTouch = 48;

            const SemanticsConfiguration& ConfigOf(LayoutNode& n) {
                auto it = configs.find(&n);
                if (it != configs.end()) return it->second;
                SemanticsConfiguration c;
                if (n.Parent()) CalculateSemanticsConfiguration(n, c);   // the root's unmerged config is empty
                return configs.emplace(&n, std::move(c)).first->second;
            }
            bool HasMergingAncestor(LayoutNode& n) {
                for (LayoutNode* p = n.Parent(); p; p = p->Parent())
                    if (HasSemantics(*p) && ConfigOf(*p).mergeDescendants) return true;
                return false;
            }

            // findSemanticsModifierNodeToGetBounds.
            const ModifierNode* BoundsModifier(LayoutNode& n, const SemanticsConfiguration& config) {
                const ModifierNode* pick = nullptr;
                for (const ModifierNode* m : n.ModifierNodes()) {
                    auto* s = const_cast<ModifierNode*>(m)->AsSemantics();
                    if (!s || !s->IsImportantForBounds()) continue;
                    if (!config.mergeDescendants) return m;
                    SemanticsConfiguration own;
                    s->ApplySemantics(own);
                    if (own.mergeDescendants) return m;
                    if (!pick) pick = m;
                }
                return pick;
            }
            Rect Space(LayoutNode& n, const ModifierNode* pick) { return pick ? n.RectInRoot(std::nullopt, pick) : n.InnerRectInRoot(); }
            // The clipping layers above the bounds coordinator, innermost first, in root pixels.
            std::vector<Rect> Clips(LayoutNode& n, const ModifierNode* pick) {
                std::vector<Rect> out;
                auto own = [&](LayoutNode& node, const ModifierNode* before) {
                    const auto mods = node.ModifierNodes();
                    std::size_t end = mods.size();
                    if (before) end = std::size_t(std::find(mods.begin(), mods.end(), before) - mods.begin());
                    for (std::size_t i = end; i > 0; --i)
                        if (mods[i - 1]->ClipsChildren()) out.push_back(node.RectInRoot(std::nullopt, mods[i - 1]));
                };
                own(n, pick);
                for (LayoutNode* p = n.Parent(); p; p = p->Parent()) own(*p, nullptr);
                return out;
            }
            // isTransparent: a layer with alpha 0 above the bounds coordinator.
            bool Transparent(LayoutNode& n, const ModifierNode* pick) {
                auto own = [&](LayoutNode& node, const ModifierNode* before) {
                    for (const ModifierNode* m : node.ModifierNodes()) {
                        if (m == before) break;
                        if (auto* d = const_cast<ModifierNode*>(m)->AsDraw(); d && d->LayerAlpha() <= 0) return true;
                    }
                    return false;
                };
                if (own(n, pick)) return true;
                for (LayoutNode* p = n.Parent(); p; p = p->Parent()) if (own(*p, nullptr)) return true;
                return false;
            }
            // boundsInRoot (clipped) and unclipped.
            Rect Clipped(Rect r, const std::vector<Rect>& clips) {
                for (const Rect& c : clips) {
                    r = r.Intersect(c);
                    if (r.Empty()) return {};
                }
                return r;
            }
            // touchBoundsInRoot: the bounds grown to the minimum touch target, clipped by each
            // clipping layer grown the same way (rectInParent with clipToMinimumTouchTargetSize).
            Rect TouchBounds(const Rect& space, const std::vector<Rect>& clips, bool clipToParentIfLarger) {
                const float padX = std::max(0.0f, (minTouch - space.Width()) / 2), padY = std::max(0.0f, (minTouch - space.Height()) / 2);
                Rect b{ space.left - padX, space.top - padY, space.right + padX, space.bottom + padY };
                for (const Rect& c : clips) {
                    // In the clipping coordinator's space.
                    const float w = c.Width(), h = c.Height();
                    const Rect child = b.Translate({ -c.left, -c.top });
                    float left = 0, top = 0;
                    if (!(child.right < 0 || child.left > w || child.bottom < 0 || child.top > h)) {
                        const float underW = (minTouch - child.Width()) / 2;
                        left = underW > 0 ? child.left - underW : std::max(std::max(child.left, std::min(0.0f, child.right - minTouch)), -minTouch / 2);
                        const float underH = (minTouch - child.Height()) / 2;
                        top = underH > 0 ? child.top - underH : std::max(std::max(child.top, std::min(0.0f, child.bottom - minTouch)), -minTouch / 2);
                    }
                    const float right = std::min(w + minTouch, std::max(w, left + minTouch));
                    const float bottom = std::min(h + minTouch, std::max(h, top + minTouch));
                    const bool wide = clipToParentIfLarger && w >= minTouch, tall = clipToParentIfLarger && h >= minTouch;
                    const Rect clip{ wide ? 0 : left, tall ? 0 : top, wide ? w : right, tall ? h : bottom };
                    b = b.Intersect(clip.Translate({ c.left, c.top }));
                    if (b.Empty()) return {};
                }
                return b;
            }

            // A real (layout-backed) node's geometry and flags; children filled by Add.
            AccessibilityNode Make(LayoutNode& n, int id, int parent) {
                AccessibilityNode a;
                a.id = id;
                a.parent = parent;
                a.layout = &n;
                a.config = ConfigOf(n);
                a.zIndex = n.ZIndex();
                a.placeOrder = n.PlaceOrder();
                a.rtl = owner.Direction() == LayoutDirection::Rtl;
                a.hasMergingAncestor = HasMergingAncestor(n);
                if (!n.Parent()) {
                    a.boundsInRoot = a.touchBoundsInRoot = a.unclippedBoundsInRoot = { 0, 0, float(owner.WindowSize().width), float(owner.WindowSize().height) };
                } else {
                    const ModifierNode* pick = BoundsModifier(n, a.config);
                    const Rect space = Space(n, pick);
                    const auto clips = Clips(n, pick);
                    a.unclippedBoundsInRoot = space;
                    a.boundsInRoot = Clipped(space, clips);
                    a.hidden = Transparent(n, pick) || a.config.hideFromAccessibility || a.config.invisibleToUser;
                }
                a.important = !a.hidden && (a.config.mergeDescendants || a.config.ContainsImportantForAccessibility());
                return a;
            }

            // fillOneLayerOfSemanticsWrappers / visitUnmergedAccessibilityChildren.
            void UnmergedChildren(LayoutNode& n, std::vector<LayoutNode*>& out) {
                for (LayoutNode* c : ZSorted(n)) {
                    if (HasSemantics(*c)) out.push_back(c);
                    else UnmergedChildren(*c, out);
                }
            }

            void Add(LayoutNode& n, int id, int parent) {
                AccessibilityNode a = Make(n, id, parent);
                std::vector<LayoutNode*> kids;
                UnmergedChildren(n, kids);
                for (LayoutNode* c : kids) a.children.push_back(c->SemanticsId());
                // replacedChildren: none while clearing; else the children with the fake nodes of
                // a merging node (its first content description, first; its role, last).
                if (!a.config.clearAndSet) {
                    a.replacedChildren = a.children;
                    if (a.config.mergeDescendants && !kids.empty()) {
                        if (a.config.role != Role::None) {
                            AccessibilityNode f = FakeOf(a, kRoleFakeNodeIdOffset);
                            f.config.role = a.config.role;
                            a.replacedChildren.push_back(f.id);
                            all.emplace(f.id, std::move(f));
                        }
                        if (!a.config.contentDescription.empty()) {
                            AccessibilityNode f = FakeOf(a, kContentDescriptionFakeNodeIdOffset);
                            f.config.contentDescription = { a.config.contentDescription.front() };
                            a.replacedChildren.insert(a.replacedChildren.begin(), f.id);
                            all.emplace(f.id, std::move(f));
                        }
                    }
                }
                const int self = a.id;
                all.emplace(self, std::move(a));
                for (LayoutNode* c : kids) Add(*c, c->SemanticsId(), self);
            }
            AccessibilityNode FakeOf(const AccessibilityNode& parent, int offset) {
                AccessibilityNode f;
                f.id = parent.id + offset;
                f.parent = parent.id;
                f.layout = parent.layout;
                f.fake = true;
                f.boundsInRoot = parent.boundsInRoot;
                f.unclippedBoundsInRoot = parent.unclippedBoundsInRoot;
                f.touchBoundsInRoot = parent.touchBoundsInRoot;
                f.hidden = parent.hidden && !parent.config.hideFromAccessibility && !parent.config.invisibleToUser;   // transparency only
                f.important = !f.hidden;
                f.zIndex = parent.zIndex;
                f.placeOrder = parent.placeOrder;
                f.rtl = parent.rtl;
                f.hasMergingAncestor = true;
                return f;
            }
        };
    }

    AccessibilityDelegate::AccessibilityDelegate(Owner& owner)
        : m_Owner(owner), m_ContentCapture(std::make_unique<ContentCaptureManager>(*this)) {}

    bool AccessibilityDelegate::Listening() const { return m_Enabled || m_ContentCapture->Enabled(); }
    AccessibilityDelegate::~AccessibilityDelegate() = default;

    void AccessibilityDelegate::SetEnabled(bool enabled) {
        if (m_Enabled == enabled) return;
        m_Enabled = enabled;
        m_Invalid = true;
        if (!enabled) {
            m_SubtreeChangedLayoutNodes.clear();
            m_SubtreeChangedSet.clear();
            m_SubtreeChangedAlive.clear();
            m_PendingHorizontalScroll.clear();
            m_PendingVerticalScroll.clear();
            for (auto& s : m_ScrollScopes) s.pixelDelta = {};
            m_Events.clear();
        }
    }

    bool AccessibilityDelegate::IsRequestFromAccessibilityTool() const {
        return m_Host.requestFromAccessibilityTool ? m_Host.requestFromAccessibilityTool() : true;
    }
    bool AccessibilityDelegate::TouchExplorationEnabled() const {
        return m_Enabled && (m_Host.touchExplorationEnabled ? m_Host.touchExplorationEnabled() : true);
    }
    std::int64_t AccessibilityDelegate::Now() const { return m_Host.uptimeMillis ? m_Host.uptimeMillis() : 0; }

    const std::unordered_map<int, AccessibilityNode>& AccessibilityDelegate::Nodes() {
        if (m_Invalid) Rebuild();
        return m_Nodes;
    }

    void AccessibilityDelegate::Rebuild() {
        m_Invalid = false;
        Tree tree{ m_Owner, {}, {} };
        tree.minTouch = m_Owner.GetDensity().ToPx(m_Owner.Config().minimumTouchTargetSize);
        m_Nodes.clear();
        tree.Add(m_Owner.Root(), kHostViewId, kInvalidId);
        auto& all = tree.all;
        // Touch bounds (onClick nodes grow to the minimum touch target).
        for (auto& [id, a] : all) {
            if (a.fake || !a.layout || !a.layout->Parent()) continue;
            if (!a.config.onClick) { a.touchBoundsInRoot = a.boundsInRoot; continue; }
            const ModifierNode* pick = tree.BoundsModifier(*a.layout, a.config);
            a.touchBoundsInRoot = tree.TouchBounds(tree.Space(*a.layout, pick), tree.Clips(*a.layout, pick), false);
        }
        for (auto& [id, a] : all)
            if (a.fake) if (auto it = all.find(a.parent); it != all.end()) a.touchBoundsInRoot = it->second.touchBoundsInRoot;

        // UncoveredSemanticsNodeTraversal: topmost children first; a node is kept while part of
        // its touch bounds is still unaccounted for, reporting the bounding box of that part.
        auto clippedTouch = [&](const AccessibilityNode& a) {
            if (!a.layout || !a.layout->Parent() || a.fake) return a.touchBoundsInRoot;
            const ModifierNode* pick = tree.BoundsModifier(*a.layout, a.config);
            if (!a.config.onClick) return a.boundsInRoot;
            return tree.TouchBounds(tree.Space(*a.layout, pick), tree.Clips(*a.layout, pick), true);
        };
        auto unclipped = [&](const AccessibilityNode& a) {
            if (!a.layout || !a.layout->Parent()) return a.boundsInRoot;
            const ModifierNode* pick = tree.BoundsModifier(*a.layout, a.config);
            const Rect space = tree.Space(*a.layout, pick);
            if (!a.config.onClick) return space;
            return tree.TouchBounds(space, {}, false);
        };
        auto visit = [&](const AccessibilityNode& a, const IntRect& b, bool hidden) {
            AccessibilityNode copy = a;
            copy.adjustedBounds = b;
            copy.isInMergingHiddenSubtree = hidden;
            m_Nodes[a.id] = std::move(copy);
        };
        auto addFake = [&](const AccessibilityNode& a, bool hidden) {
            auto p = all.find(a.parent);
            const Rect r = p != all.end() && p->second.layout && IsPlacedNode(*p->second.layout) ? p->second.boundsInRoot : Rect{ 0, 0, 10, 10 };
            visit(a, RoundRect(r), hidden);
        };
        auto scrollParentOf = [&](const AccessibilityNode& a) -> const AccessibilityNode* {
            for (auto it = all.find(a.parent); it != all.end(); it = all.find(it->second.parent))
                if (it->second.config.verticalScrollAxisRange || it->second.config.horizontalScrollAxisRange) return &it->second;
            return nullptr;
        };
        auto partiallyOffscreen = [&](const AccessibilityNode& a) {
            const AccessibilityNode* sp = scrollParentOf(a);
            if (!sp || !a.layout || !sp->layout) return false;
            const Rect mine = a.layout->RectInRoot(std::nullopt, tree.BoundsModifier(*a.layout, a.config));
            const Rect parent = sp->layout->Parent() ? sp->layout->RectInRoot(std::nullopt, tree.BoundsModifier(*sp->layout, sp->config)) : sp->boundsInRoot;
            const Rect visible = mine.Intersect(parent);
            return !(visible.left == mine.left && visible.top == mine.top && visible.right == mine.right && visible.bottom == mine.bottom);
        };
        std::function<void(const AccessibilityNode&, Region&, bool)> partial = [&](const AccessibilityNode& a, Region& unaccounted, bool hiddenIn) {
            if (!a.fake && (!a.layout || !IsPlacedNode(*a.layout))) return;
            if (unaccounted.Empty()) { if (a.fake) addFake(a, hiddenIn); return; }
            if (a.fake) { addFake(a, hiddenIn); return; }
            const Rect cur = a.touchBoundsInRoot.Empty() ? unclipped(a) : a.touchBoundsInRoot;
            const IntRect r = RoundRect(cur);
            Region region;
            region.Set(r);
            const bool hidden = hiddenIn || (a.config.mergeDescendants && a.config.hideFromAccessibility);
            if (region.Intersect(unaccounted)) {
                visit(a, region.Bounds(), hidden);
                for (auto it = a.replacedChildren.rbegin(); it != a.replacedChildren.rend(); ++it)
                    if (auto c = all.find(*it); c != all.end()) partial(c->second, unaccounted, hidden);
                if (a.important) unaccounted.Difference(r);
            }
        };
        std::function<void(const AccessibilityNode&, Region&, bool)> rec = [&](const AccessibilityNode& a, Region& unaccounted, bool hiddenIn) {
            const bool isRoot = a.id == kHostViewId;
            if ((unaccounted.Empty() && !isRoot) || (!a.fake && (!a.layout || !IsPlacedNode(*a.layout)))) return;
            if (a.fake) { addFake(a, hiddenIn); return; }
            const IntRect touch = RoundRect(a.touchBoundsInRoot);
            const IntRect clipped = RoundRect(clippedTouch(a));
            const bool hidden = hiddenIn || (a.config.mergeDescendants && a.config.hideFromAccessibility);
            Region region;
            region.Set(clipped);
            const bool uncovered = region.Intersect(unaccounted);
            if (uncovered && !(clipped == touch)) {
                region.Set(touch);
                region.Intersect(unaccounted);
            }
            if (uncovered) {
                visit(a, region.Bounds(), hidden);
                if (a.config.mergeDescendants && partiallyOffscreen(a)) {
                    Region childrenUnaccounted;
                    childrenUnaccounted.Set(RoundRect(unclipped(a)));
                    for (auto it = a.replacedChildren.rbegin(); it != a.replacedChildren.rend(); ++it)
                        if (auto c = all.find(*it); c != all.end()) partial(c->second, childrenUnaccounted, hidden);
                } else {
                    for (auto it = a.replacedChildren.rbegin(); it != a.replacedChildren.rend(); ++it)
                        if (auto c = all.find(*it); c != all.end()) rec(c->second, unaccounted, hidden);
                }
                if (a.important) unaccounted.Difference(clipped);
            } else if (isRoot) {
                visit(a, region.Bounds(), hidden);
            }
        };
        if (auto r = all.find(kHostViewId); r != all.end()) {
            Region unaccounted;
            unaccounted.Set(RoundRect(r->second.boundsInRoot));
            rec(r->second, unaccounted, false);
        }

        m_All = std::move(all);
        if (m_Enabled) SetTraversalValues();
    }

    // --- traversal order (SemanticsSort) ------------------------------------------------------

    namespace {
        struct Sorter {
            const std::unordered_map<int, AccessibilityNode>& all;
            std::function<bool(const AccessibilityNode&)> isVisible, isFocusableContainer;
            bool rtl = false;

            const AccessibilityNode* Get(int id) const { auto it = all.find(id); return it == all.end() ? nullptr : &it->second; }

            static bool ByTraversalIndex(const AccessibilityNode* a, const AccessibilityNode* b) {
                return a->config.traversalIndex.value_or(0) < b->config.traversalIndex.value_or(0);
            }

            std::vector<const AccessibilityNode*> SubtreeSorted(const std::vector<const AccessibilityNode*>& listToSort) {
                std::unordered_map<int, std::vector<const AccessibilityNode*>> containers;
                std::vector<const AccessibilityNode*> geometry;
                for (const auto* n : listToSort) DepthFirst(*n, geometry, containers);
                return SortByGeometryGroupings(geometry, containers);
            }
            void DepthFirst(const AccessibilityNode& n, std::vector<const AccessibilityNode*>& geometry,
                            std::unordered_map<int, std::vector<const AccessibilityNode*>>& containers) {
                const bool group = n.config.isTraversalGroup;
                if ((group || isFocusableContainer(n)) && isVisible(n)) geometry.push_back(&n);
                std::vector<const AccessibilityNode*> kids;
                for (int id : n.children) if (const auto* c = Get(id)) kids.push_back(c);
                if (group) containers[n.id] = SubtreeSorted(kids);
                else for (const auto* c : kids) DepthFirst(*c, geometry, containers);
            }
            std::vector<const AccessibilityNode*> SortByGeometryGroupings(const std::vector<const AccessibilityNode*>& list,
                                                                          std::unordered_map<int, std::vector<const AccessibilityNode*>>& containers) {
                std::vector<std::pair<Rect, std::vector<const AccessibilityNode*>>> rows;
                for (std::size_t i = 0; i < list.size(); ++i) {
                    const auto* e = list[i];
                    bool placed = false;
                    if (i != 0) {
                        // placedEntryRowOverlaps
                        const float t = e->boundsInRoot.top, b = e->boundsInRoot.bottom;
                        const bool entryEmpty = t >= b;
                        for (auto& row : rows) {
                            const Rect& r = row.first;
                            const bool groupEmpty = r.top >= r.bottom;
                            if (!entryEmpty && !groupEmpty && std::max(t, r.top) < std::min(b, r.bottom)) {
                                row.first = Rect{ std::max(r.left, 0.0f), std::max(r.top, t), std::min(r.right, INFINITY), std::min(r.bottom, b) };
                                row.second.push_back(e);
                                placed = true;
                                break;
                            }
                        }
                    }
                    if (!placed) rows.push_back({ e->boundsInRoot, { e } });
                }
                std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
                    if (a.first.top != b.first.top) return a.first.top < b.first.top;
                    return a.first.bottom < b.first.bottom;
                });
                auto zPlace = [](const AccessibilityNode* a, const AccessibilityNode* b) -> int {
                    if (a->zIndex != b->zIndex) return a->zIndex < b->zIndex ? -1 : 1;
                    if (a->placeOrder != b->placeOrder) return a->placeOrder < b->placeOrder ? -1 : 1;
                    return a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
                };
                auto cmp = [&](const AccessibilityNode* a, const AccessibilityNode* b) {
                    const Rect& x = a->boundsInRoot; const Rect& y = b->boundsInRoot;
                    auto c = [](float p, float q) { return p < q ? -1 : p > q ? 1 : 0; };
                    int r = rtl ? c(y.right, x.right) : c(x.left, y.left);
                    if (!r) r = c(x.top, y.top);
                    if (!r) r = c(x.bottom, y.bottom);
                    if (!r) r = rtl ? c(y.left, x.left) : c(x.right, y.right);
                    if (!r) r = zPlace(a, b);
                    return r < 0;
                };
                std::vector<const AccessibilityNode*> out;
                for (auto& row : rows) {
                    std::stable_sort(row.second.begin(), row.second.end(), cmp);
                    out.insert(out.end(), row.second.begin(), row.second.end());
                }
                std::stable_sort(out.begin(), out.end(), ByTraversalIndex);
                std::size_t i = 0;
                while (i < out.size()) {
                    auto it = containers.find(out[i]->id);
                    if (it != containers.end()) {
                        const auto kids = it->second;
                        if (!isFocusableContainer(*out[i])) out.erase(out.begin() + std::ptrdiff_t(i));
                        else ++i;
                        out.insert(out.begin() + std::ptrdiff_t(i), kids.begin(), kids.end());
                        i += kids.size();
                    } else {
                        ++i;
                    }
                }
                return out;
            }
        };
    }

    void AccessibilityDelegate::SetTraversalValues() {
        m_IdToBefore.clear();
        m_IdToAfter.clear();
        const AccessibilityNode* host = FindAny(kHostViewId);
        if (!host) return;
        Sorter s{ m_All,
                  [this](const AccessibilityNode& n) { return m_Nodes.count(n.id) != 0; },
                  [this](const AccessibilityNode& n) { return ScreenReaderFocusable(n); },
                  host->rtl };
        const auto order = s.SubtreeSorted({ host });
        for (std::size_t i = 1; i < order.size(); ++i) {
            m_IdToBefore[order[i - 1]->id] = order[i]->id;
            m_IdToAfter[order[i]->id] = order[i - 1]->id;
        }
    }

    // --- node info ----------------------------------------------------------------------------

    namespace {
        // getTextForTranslationOrFirstText / getInfoText.
        std::optional<std::string> TranslationOrFirstText(const SemanticsConfiguration& c) {
            if (c.isShowingTextSubstitution.value_or(false) && c.textSubstitution) return c.textSubstitution;
            if (!c.text.empty()) return c.text.front();
            return std::nullopt;
        }
        std::optional<std::string> InfoText(const SemanticsConfiguration& c) {
            if (c.editableText) return c.editableText;
            return TranslationOrFirstText(c);
        }
        bool InfoIsCheckable(const SemanticsConfiguration& c) {
            return c.toggleableState.has_value() || (c.selected.has_value() && c.role != Role::Tab);
        }
        // Role.toLegacyClassName.
        const char* LegacyClassName(Role r) {
            switch (r) {
                case Role::Button: return "android.widget.Button";
                case Role::Checkbox: return "android.widget.CheckBox";
                case Role::RadioButton: return "android.widget.RadioButton";
                case Role::Image: return "android.widget.ImageView";
                case Role::DropdownList: return "android.widget.Spinner";
                case Role::ValuePicker: return "android.widget.NumberPicker";
                default: return nullptr;
            }
        }
        // getIterableTextForAccessibility.
        std::optional<std::string> IterableText(const SemanticsConfiguration& c) {
            if (!c.contentDescription.empty()) return Join(c.contentDescription, ",");
            if (c.editableText) return c.editableText;
            return TranslationOrFirstText(c);
        }
        bool CanScrollForward(const ScrollAxisRange& r) {
            const float v = r.value ? r.value() : 0, max = r.maxValue ? r.maxValue() : 0;
            return (v < max && !r.reverseScrolling) || (v > 0 && r.reverseScrolling);
        }
        bool CanScrollBackward(const ScrollAxisRange& r) {
            const float v = r.value ? r.value() : 0, max = r.maxValue ? r.maxValue() : 0;
            return (v > 0 && !r.reverseScrolling) || (v < max && r.reverseScrolling);
        }
        bool HasCollectionInfo(const SemanticsConfiguration& c) { return c.collectionInfo.has_value() || c.selectableGroup; }
        // calculateIfHorizontallyStacked.
        bool HorizontallyStacked(const std::vector<const AccessibilityNode*>& items) {
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

    bool AccessibilityDelegate::Enabled(const AccessibilityNode& node) const { return !node.config.disabled; }

    SemanticsConfiguration AccessibilityDelegate::MergedConfig(const AccessibilityNode& node) const {
        // copyWithMergingEnabled().config: a merging node's config with its descendants'
        // (stopping at nodes that merge their own and below clearing ones).
        SemanticsConfiguration out = node.config;
        if (!node.config.mergeDescendants || node.fake) return out;
        std::function<void(const AccessibilityNode&)> mergeInto = [&](const AccessibilityNode& n) {
            if (n.config.clearAndSet) return;
            for (int id : n.children) {
                const AccessibilityNode* c = FindAny(id);
                if (!c || c->config.mergeDescendants) continue;
                out.Merge(c->config);
                mergeInto(*c);
            }
        };
        mergeInto(node);
        return out;
    }

    bool AccessibilityDelegate::UnmergedLeaf(const AccessibilityNode& node) const {
        return !node.fake && node.replacedChildren.empty() && !node.hasMergingAncestor;
    }

    std::optional<std::string> AccessibilityDelegate::InfoStateDescription(const AccessibilityNode& node) {
        const auto& c = node.config;
        std::optional<std::string> state = c.stateDescription;
        if (c.toggleableState) {
            if (*c.toggleableState == 1 && c.role == Role::Switch && !state) state = m_Strings.stateOn;
            else if (*c.toggleableState == 0 && c.role == Role::Switch && !state) state = m_Strings.stateOff;
            else if (*c.toggleableState == 2 && !state) state = m_Strings.indeterminate;
        }
        if (c.selected && c.role != Role::Tab && !state) state = *c.selected ? m_Strings.selected : m_Strings.notSelected;
        if (const auto& r = c.progressBarRangeInfo) {
            if (!r->indeterminate) {
                if (!state) {
                    const float span = r->end - r->start;
                    const float progress = std::clamp(span == 0 ? 0.0f : (r->current - r->start) / span, 0.0f, 1.0f);
                    const int percent = progress == 0 ? 0 : progress == 1 ? 100 : std::clamp(RoundI(progress * 100), 1, 99);
                    std::string t = m_Strings.templatePercent;
                    if (auto at = t.find("%1$d"); at != std::string::npos) t.replace(at, 4, std::to_string(percent));
                    state = t;
                }
            } else if (!state) {
                state = m_Strings.inProgress;
            }
        }
        if (c.editableText) {
            // createStateDescriptionForTextField: "Empty" when the merged node says nothing.
            const SemanticsConfiguration merged = MergedConfig(node);
            auto empty = [](const std::vector<std::string>& v) { return v.empty() || std::all_of(v.begin(), v.end(), [](const std::string& s) { return s.empty(); }); };
            const bool unspeakable = empty(merged.contentDescription) && empty(merged.text) &&
                                     (!merged.editableText || merged.editableText->empty()) && (!merged.hintText || merged.hintText->empty());
            state = unspeakable ? std::optional(m_Strings.stateEmpty) : std::nullopt;
        }
        return state;
    }

    bool AccessibilityDelegate::ScreenReaderFocusable(const AccessibilityNode& node) {
        auto it = m_Nodes.find(node.id);
        const bool hiddenSubtree = it != m_Nodes.end() && it->second.isInMergingHiddenSubtree;
        if (node.hidden || hiddenSubtree) return false;
        if (node.config.mergeDescendants) return true;
        const bool speaking = !node.config.contentDescription.empty() || InfoText(node.config).has_value() ||
                              InfoStateDescription(node).has_value() || InfoIsCheckable(node.config);
        return speaking && UnmergedLeaf(node);
    }

    std::optional<AccessibilityNodeInfo> AccessibilityDelegate::CreateNodeInfo(int id) {
        const AccessibilityNode* adjusted = Find(id);
        if (!adjusted) return std::nullopt;
        const AccessibilityNode& node = *adjusted;
        const auto& c = node.config;
        const bool sensitive = c.isSensitiveData.value_or(false);
        if (sensitive && !IsRequestFromAccessibilityTool()) return std::nullopt;
        AccessibilityNodeInfo info;
        info.id = id;
        info.dataSensitive = sensitive;
        info.parent = id == kHostViewId ? kInvalidId : node.parent;
        info.boundsInRoot = node.adjustedBounds;

        // populateAccessibilityNodeInfoProperties.
        const bool hiddenSubtree = node.isInMergingHiddenSubtree;
        info.className = "android.view.View";
        if (c.editableText) info.className = "android.widget.EditText";
        if (!c.text.empty()) info.className = "android.widget.TextView";
        if (c.role != Role::None && (node.fake || node.replacedChildren.empty())) {
            if (c.role == Role::Tab) info.roleDescription = m_Strings.tab;
            else if (c.role == Role::Switch) info.roleDescription = m_Strings.switchRole;
            else if (const char* cls = LegacyClassName(c.role);
                     cls && (c.role != Role::Image || UnmergedLeaf(node) || c.mergeDescendants)) info.className = cls;
        }
        info.importantForAccessibility = node.important;

        const bool requestFromTool = IsRequestFromAccessibilityTool();
        int drawingOrder = 0;
        std::vector<const AccessibilityNode*> kids;
        for (int k : node.replacedChildren) if (const auto* a = FindAny(k)) kids.push_back(a);
        if (c.isTraversalGroup && c.mergeDescendants && kids.size() > 1)
            std::stable_sort(kids.begin(), kids.end(), [](const AccessibilityNode* a, const AccessibilityNode* b) {
                return a->config.traversalIndex.value_or(0) < b->config.traversalIndex.value_or(0);
            });
        for (const auto* k : kids) {
            if (!m_Nodes.count(k->id) || k->id == kHostViewId) continue;
            if (requestFromTool || !k->config.isSensitiveData.value_or(false)) info.children.push_back(k->id);
            m_DrawingOrder[k->id] = drawingOrder++;
        }

        if (id == m_AccessibilityFocused) {
            info.accessibilityFocused = true;
            info.actions.push_back({ a11y::ClearAccessibilityFocus, {} });
        } else {
            info.actions.push_back({ AccessibilityFocus, {} });
        }

        if (auto t = InfoText(c)) {
            info.text = TrimToSize(*t, kParcelSafeTextLength);
            if (!c.editableText && !(c.isShowingTextSubstitution.value_or(false) && c.textSubstitution))
                for (const auto& span : c.textSpans) if (span.textIndex == 0) info.textSpans.push_back(span);
        }
        if (c.error) { info.contentInvalid = true; info.error = c.error; }
        info.stateDescription = InfoStateDescription(node);
        info.checkable = InfoIsCheckable(c);
        if (c.toggleableState) {
            if (*c.toggleableState == 1) info.checked = true;
            else if (*c.toggleableState == 0) info.checked = false;
        }
        if (c.selected) {
            if (c.role == Role::Tab) info.selected = *c.selected;
            else info.checked = *c.selected;
        }
        if (!c.mergeDescendants || node.replacedChildren.empty())
            if (!c.contentDescription.empty()) info.contentDescription = c.contentDescription.front();

        if (c.testTag) {
            std::optional<bool> asId = c.testTagsAsResourceId;
            if (!asId && node.layout)
                for (LayoutNode* p = node.layout->Parent(); p && !asId; p = p->Parent()) {
                    if (!HasSemantics(*p)) continue;
                    SemanticsConfiguration pc;
                    CalculateSemanticsConfiguration(*p, pc);
                    asId = pc.testTagsAsResourceId;
                }
            if (asId.value_or(false)) info.viewIdResourceName = c.testTag;
        }
        if (c.hintText) info.hintText = c.hintText;
        info.heading = c.heading;
        info.textEntryKey = c.textEntryKey;
        if (id != kHostViewId) {
            if (auto it = m_DrawingOrder.find(node.id); it != m_DrawingOrder.end()) info.drawingOrder = it->second;
        }
        info.password = c.password && c.isPasswordObfuscated.value_or(true);
        info.editable = c.isEditable.value_or(false);
        info.maxTextLength = c.maxTextLength.value_or(-1);
        info.enabled = Enabled(node);
        info.focusable = c.focused.has_value();
        if (info.focusable) {
            info.focused = *c.focused;
            if (info.focused) {
                info.actions.push_back({ ClearFocus, {} });
                m_Focused = id;
            } else {
                info.actions.push_back({ Focus, {} });
            }
        }
        info.visibleToUser = !(node.hidden || hiddenSubtree);
        {
            // setInvisibleIfEmptyBounds (a fake node's parent's bounds).
            const AccessibilityNode* b = node.fake ? FindAny(node.parent) : &node;
            if (b && b->touchBoundsInRoot.Empty()) info.visibleToUser = false;
        }
        if (c.liveRegion) info.liveRegion = *c.liveRegion == LiveRegionMode::Assertive ? LiveRegionAssertive : LiveRegionPolite;
        info.clickable = false;
        if (c.onClick) {
            const bool sel = c.selected.value_or(false);
            const bool radioOrTab = c.role == Role::Tab || c.role == Role::RadioButton;
            info.clickable = !radioOrTab || !sel;
            if (Enabled(node) && info.clickable) info.actions.push_back({ Click, c.onClickLabel });
        }
        info.longClickable = false;
        if (c.onLongClick) {
            info.longClickable = true;
            if (Enabled(node)) info.actions.push_back({ LongClick, c.onLongClickLabel });
        }
        if (c.copyText) info.actions.push_back({ Copy, {} });
        if (Enabled(node)) {
            if (c.setText) info.actions.push_back({ SetText, {} });
            if (c.onImeAction) info.actions.push_back({ ImeEnter, c.onImeActionLabel });
            if (c.cutText) info.actions.push_back({ Cut, {} });
            if (c.pasteText && info.focused && m_Owner.ClipboardHasText()) info.actions.push_back({ Paste, {} });
        }

        const auto iterable = IterableText(c);
        if (iterable && !iterable->empty()) {
            info.textSelection = std::pair{ SelectionStart(node), SelectionEnd(node) };
            info.actions.push_back({ SetSelection, {} });
            info.actions.push_back({ NextAtMovementGranularity, {} });
            info.actions.push_back({ PreviousAtMovementGranularity, {} });
            info.movementGranularities = Character | Word | Paragraph;
            // excludeLineAndPageGranularities: an unfocused text field (or one inside a merging,
            // unfocused one).
            bool exclude = c.editableText && c.focused != std::optional(true);
            if (!exclude && node.layout)
                for (LayoutNode* p = node.layout->Parent(); p; p = p->Parent()) {
                    if (!HasSemantics(*p)) continue;
                    SemanticsConfiguration pc;
                    CalculateSemanticsConfiguration(*p, pc);
                    if (pc.mergeDescendants && pc.editableText) { exclude = pc.focused != std::optional(true); break; }
                }
            if (c.contentDescription.empty() && c.getTextLayoutResult && !exclude)
                info.movementGranularities |= Line | Page;
        }
        info.extraDataKeys.push_back(kExtraDataIdKey);
        if (info.text && !info.text->empty() && c.getTextLayoutResult) info.extraDataKeys.push_back(kExtraDataTextCharacterLocationKey);
        if (c.testTag) info.extraDataKeys.push_back(kExtraDataTestTagKey);
        if (c.shape) {
            info.extraDataKeys.push_back(kExtraDataShapeTypeKey);
            info.extraDataKeys.push_back(kExtraDataShapeRectKey);
            info.extraDataKeys.push_back(kExtraDataShapeRectCornersKey);
            info.extraDataKeys.push_back(kExtraDataShapeRegionKey);
        }

        if (const auto& r = c.progressBarRangeInfo) {
            info.className = c.setProgress ? "android.widget.SeekBar" : "android.widget.ProgressBar";
            if (!r->indeterminate) info.rangeInfo = AccessibilityNodeInfo::Range{ r->start, r->end, r->current };
            if (c.setProgress && Enabled(node)) {
                if (r->current < std::max(r->end, r->start)) info.actions.push_back({ ScrollForward, {} });
                if (r->current > std::min(r->start, r->end)) info.actions.push_back({ ScrollBackward, {} });
            }
        }
        if (Enabled(node) && c.setProgress) info.actions.push_back({ SetProgress, {} });

        // setCollectionInfo / setCollectionItemInfo.
        auto selectedChildren = [&](const AccessibilityNode& group) {
            std::vector<const AccessibilityNode*> out;
            for (int k : group.replacedChildren) if (const auto* a = FindAny(k); a && a->config.selected) out.push_back(a);
            return out;
        };
        if (c.collectionInfo) {
            info.collectionInfo = c.collectionInfo;
        } else if (c.selectableGroup) {
            const auto items = selectedChildren(node);
            if (!items.empty()) {
                const int n = int(items.size());
                info.collectionInfo = HorizontallyStacked(items) ? std::pair{ 1, n } : std::pair{ n, 1 };
            }
        }
        if (c.collectionItemInfo)
            info.collectionItemInfo = AccessibilityNodeInfo::Item{ c.collectionItemInfo->rowIndex, c.collectionItemInfo->rowSpan,
                                                                   c.collectionItemInfo->columnIndex, c.collectionItemInfo->columnSpan,
                                                                   c.selected.value_or(false) };
        if (const AccessibilityNode* parent = FindAny(node.parent); parent && parent->config.selectableGroup) {
            const auto& pci = parent->config.collectionInfo;
            const bool lazy = pci && (pci->first < 0 || pci->second < 0);
            if (!lazy && c.selected) {
                const auto items = selectedChildren(*parent);
                int index = 0;
                for (const auto* a : items) if (a->placeOrder < node.placeOrder) ++index;
                if (!items.empty()) {
                    const bool horizontal = HorizontallyStacked(items);
                    info.collectionItemInfo = AccessibilityNodeInfo::Item{ horizontal ? 0 : index, 1, horizontal ? index : 0, 1, c.selected.value_or(false) };
                }
            }
        }

        if (c.horizontalScrollAxisRange && c.scrollBy) {
            const auto& x = *c.horizontalScrollAxisRange;
            if (!HasCollectionInfo(c)) info.className = "android.widget.HorizontalScrollView";
            if ((x.maxValue ? x.maxValue() : 0) > 0) info.scrollable = true;
            if (Enabled(node)) {
                if (CanScrollForward(x)) {
                    info.actions.push_back({ ScrollForward, {} });
                    info.actions.push_back({ node.rtl ? ScrollLeft : ScrollRight, {} });
                }
                if (CanScrollBackward(x)) {
                    info.actions.push_back({ ScrollBackward, {} });
                    info.actions.push_back({ node.rtl ? ScrollRight : ScrollLeft, {} });
                }
            }
        }
        if (c.verticalScrollAxisRange && c.scrollBy) {
            const auto& y = *c.verticalScrollAxisRange;
            if (!HasCollectionInfo(c)) info.className = "android.widget.ScrollView";
            if ((y.maxValue ? y.maxValue() : 0) > 0) info.scrollable = true;
            if (Enabled(node)) {
                if (CanScrollForward(y)) {
                    info.actions.push_back({ ScrollForward, {} });
                    info.actions.push_back({ ScrollDown, {} });
                }
                if (CanScrollBackward(y)) {
                    info.actions.push_back({ ScrollBackward, {} });
                    info.actions.push_back({ ScrollUp, {} });
                }
            }
        }
        // addPageActions (not on carousels).
        if (Enabled(node) && c.role != Role::Carousel) {
            if (c.pageUp) info.actions.push_back({ PageUp, {} });
            if (c.pageDown) info.actions.push_back({ PageDown, {} });
            if (c.pageLeft) info.actions.push_back({ PageLeft, {} });
            if (c.pageRight) info.actions.push_back({ PageRight, {} });
        }
        info.paneTitle = c.paneTitle;
        if (Enabled(node)) {
            if (c.expand) info.actions.push_back({ Expand, c.expandLabel });
            if (c.collapse) info.actions.push_back({ Collapse, c.collapseLabel });
            if (c.dismiss) info.actions.push_back({ Dismiss, c.dismissLabel });
            if (!c.customActions.empty()) {
                // Stable ids: a label keeps the id it had; new labels take the free ones.
                std::unordered_map<int, std::string> idToLabel;
                std::unordered_map<std::string, int> labelToId;
                const std::size_t count = std::min<std::size_t>(c.customActions.size(), kCustomActionCount - 1);
                auto old = m_LabelToActionId.find(id);
                if (old != m_LabelToActionId.end()) {
                    std::vector<int> available;
                    for (int i = 0; i < kCustomActionCount; ++i) available.push_back(kCustomActionIdBase + i);
                    std::vector<const CustomAccessibilityAction*> unassigned;
                    for (std::size_t i = 0; i < count; ++i) {
                        const auto& a = c.customActions[i];
                        if (auto f = old->second.find(a.label); f != old->second.end()) {
                            idToLabel[f->second] = a.label;
                            labelToId[a.label] = f->second;
                            available.erase(std::remove(available.begin(), available.end(), f->second), available.end());
                            info.actions.push_back({ f->second, a.label });
                        } else {
                            unassigned.push_back(&a);
                        }
                    }
                    for (std::size_t i = 0; i < unassigned.size() && i < available.size(); ++i) {
                        idToLabel[available[i]] = unassigned[i]->label;
                        labelToId[unassigned[i]->label] = available[i];
                        info.actions.push_back({ available[i], unassigned[i]->label });
                    }
                } else {
                    for (std::size_t i = 0; i < count; ++i) {
                        const int actionId = kCustomActionIdBase + int(i);
                        idToLabel[actionId] = c.customActions[i].label;
                        labelToId[c.customActions[i].label] = actionId;
                        info.actions.push_back({ actionId, c.customActions[i].label });
                    }
                }
                m_ActionIdToLabel[id] = std::move(idToLabel);
                m_LabelToActionId[id] = std::move(labelToId);
            }
        }
        info.screenReaderFocusable = ScreenReaderFocusable(node);
        if (auto it = m_IdToBefore.find(id); it != m_IdToBefore.end()) {
            info.traversalBefore = it->second;
            info.extraDataKeys.push_back(kExtraDataTraversalBeforeKey);
        }
        if (auto it = m_IdToAfter.find(id); it != m_IdToAfter.end()) info.traversalAfter = it->second;
        if (c.accessibilityClassName) info.className = *c.accessibilityClassName;
        return info;
    }

    // --- actions ------------------------------------------------------------------------------

    bool AccessibilityDelegate::RequestAccessibilityFocus(int id) {
        if (!TouchExplorationEnabled()) return false;
        if (m_AccessibilityFocused == id) return false;
        if (m_AccessibilityFocused != kInvalidId) SendEventForVirtualView(m_AccessibilityFocused, ViewAccessibilityFocusCleared);
        m_AccessibilityFocused = id;
        if (m_Host.invalidate) m_Host.invalidate();
        SendEventForVirtualView(id, ViewAccessibilityFocused);
        return true;
    }

    bool AccessibilityDelegate::ClearA11yFocus(int id) {
        if (m_AccessibilityFocused != id) return false;
        m_AccessibilityFocused = kInvalidId;
        if (m_Host.invalidate) m_Host.invalidate();
        SendEventForVirtualView(id, ViewAccessibilityFocusCleared);
        return true;
    }

    int AccessibilityDelegate::SelectionStart(const AccessibilityNode& node) const {
        if (node.config.contentDescription.empty() && node.config.textSelection) return std::min(node.config.textSelection->first, node.config.textSelection->second);
        return m_CursorPosition;
    }
    int AccessibilityDelegate::SelectionEnd(const AccessibilityNode& node) const {
        if (node.config.contentDescription.empty() && node.config.textSelection) return std::max(node.config.textSelection->first, node.config.textSelection->second);
        return m_CursorPosition;
    }

    bool AccessibilityDelegate::SetAccessibilitySelection(const AccessibilityNode& node, int start, int end, bool traversalMode) {
        if (node.config.setSelection && Enabled(node)) return node.config.setSelection(start, end, traversalMode);
        if (start == end && end == m_CursorPosition) return false;
        const auto text = IterableText(node.config);
        if (!text) return false;
        const int length = int(U16(*text).size());
        m_CursorPosition = start >= 0 && start == end && end <= length ? start : -1;
        const bool nonEmpty = length > 0;
        AccessibilityEvent e = CreateEvent(node.id, ViewTextSelectionChanged);
        if (nonEmpty) { e.fromIndex = m_CursorPosition; e.toIndex = m_CursorPosition; e.itemCount = length; }
        e.text.push_back(*text);
        Send(std::move(e));
        SendPendingTextTraversedEvent(node.id);
        return true;
    }

    void AccessibilityDelegate::SendPendingTextTraversedEvent(int id) {
        if (m_PendingTraversal && m_PendingTraversal->id == id) {
            if (Now() - m_PendingTraversal->time <= kTextTraversedEventTimeoutMillis) {
                AccessibilityEvent e = CreateEvent(id, ViewTextTraversedAtMovementGranularity);
                e.fromIndex = m_PendingTraversal->from;
                e.toIndex = m_PendingTraversal->to;
                e.action = m_PendingTraversal->action;
                e.movementGranularity = m_PendingTraversal->granularity;
                if (const auto* n = Find(id)) if (auto t = IterableText(n->config)) e.text.push_back(*t);
                Send(std::move(e));
            }
        } else if (m_PendingTraversal) {
            return;
        }
        m_PendingTraversal.reset();
    }

    bool AccessibilityDelegate::TraverseAtGranularity(const AccessibilityNode& node, int granularity, bool forward, bool extend) {
        if (!m_PreviousTraversedNode || *m_PreviousTraversedNode != node.id) {
            m_CursorPosition = -1;
            m_PreviousTraversedNode = node.id;
        }
        const auto text8 = IterableText(node.config);
        if (!text8 || text8->empty()) return false;
        const std::u16string text = U16(*text8);
        TextBackend* backend = m_Owner.Text();
        auto brk = [&](int offset, bool following) {
            return backend ? backend->CharacterBreak(text, offset, following) : TextBackend::SurrogateCharacterBreak(text, offset, following);
        };
        auto wordBrk = [&](int offset, bool following) {
            return backend ? backend->WordBreak(text, offset, following) : TextBackend::SimpleWordBreak(text, offset, following);
        };
        std::shared_ptr<const text::TextLayoutResult> layout;
        if (granularity == Line || granularity == Page) {
            if (!node.config.getTextLayoutResult) return false;
            layout = node.config.getTextLayoutResult();
            if (!layout) return false;
        }
        int current = SelectionEnd(node);
        if (current == -1) current = forward ? 0 : int(text.size());
        Segment range;
        switch (granularity) {
            case Character: range = forward ? CharacterFollowing(text, current, brk) : CharacterPreceding(text, current, brk); break;
            case Word: range = forward ? WordFollowing(text, current, wordBrk) : WordPreceding(text, current, wordBrk); break;
            case Paragraph: range = forward ? ParagraphFollowing(text, current) : ParagraphPreceding(text, current); break;
            case Line: range = forward ? LineFollowing(text, *layout, current) : LinePreceding(text, *layout, current); break;
            case Page: {
                const int pageHeight = RoundI(node.boundsInRoot.Height());
                range = forward ? PageFollowing(text, *layout, pageHeight, current) : PagePreceding(text, *layout, pageHeight, current);
                break;
            }
            default: return false;
        }
        if (!range) return false;
        const auto [segStart, segEnd] = *range;
        int selStart, selEnd;
        if (extend && node.config.contentDescription.empty() && node.config.editableText) {
            selStart = SelectionStart(node);
            if (selStart == -1) selStart = forward ? segStart : segEnd;
            selEnd = forward ? segEnd : segStart;
        } else {
            selStart = forward ? segEnd : segStart;
            selEnd = selStart;
        }
        m_PendingTraversal = PendingTraversal{ node.id, forward ? NextAtMovementGranularity : PreviousAtMovementGranularity, granularity, segStart, segEnd, Now() };
        SetAccessibilitySelection(node, selStart, selEnd, true);
        return true;
    }

    bool AccessibilityDelegate::PerformAction(int id, int action, const AccessibilityActionArgs& args) {
        const AccessibilityNode* found = Find(id);
        if (!found) return false;
        const AccessibilityNode node = *found;   // actions can rebuild the map
        const auto& c = node.config;
        if (c.isSensitiveData.value_or(false) && !IsRequestFromAccessibilityTool()) return false;
        switch (action) {
            case AccessibilityFocus: return RequestAccessibilityFocus(id);
            case a11y::ClearAccessibilityFocus: return ClearA11yFocus(id);
            case NextAtMovementGranularity:
            case PreviousAtMovementGranularity:
                if (!args.granularity) return false;
                return TraverseAtGranularity(node, *args.granularity, action == NextAtMovementGranularity, args.extendSelection);
            case SetSelection: {
                const bool ok = SetAccessibilitySelection(node, args.selectionStart.value_or(-1), args.selectionEnd.value_or(-1), false);
                // Compose sends this with CONTENT_CHANGE_TYPE_UNDEFINED as the event type (0).
                if (ok) SendEventForVirtualView(id, Undefined);
                return ok;
            }
            case Copy: return c.copyText && c.copyText();
            default: break;
        }
        if (!Enabled(node)) return false;
        switch (action) {
            case Click: {
                const bool r = c.onClick && c.onClick();
                SendEventForVirtualView(id, ViewClicked);
                return r;
            }
            case LongClick: return c.onLongClick && c.onLongClick();
            case ScrollForward: case ScrollBackward: case ScrollDown: case ScrollUp: case ScrollRight: case ScrollLeft: {
                const bool fwd = action == ScrollForward, back = action == ScrollBackward;
                const bool left = action == ScrollLeft, right = action == ScrollRight, up = action == ScrollUp, down = action == ScrollDown;
                const bool horizontal = left || right || fwd || back, vertical = up || down || fwd || back;
                if (fwd || back) {
                    if (c.progressBarRangeInfo && c.setProgress) {
                        const auto& r = *c.progressBarRangeInfo;
                        const float max = std::max(r.end, r.start), min = std::min(r.start, r.end);
                        float inc = r.steps > 0 ? (max - min) / float(r.steps + 1) : (max - min) / float(kSliderStepsCount);
                        if (back) inc = -inc;
                        return c.setProgress(r.current + inc);
                    }
                }
                auto canScroll = [](const ScrollAxisRange& r, float amount) {
                    const float v = r.value ? r.value() : 0, mx = r.maxValue ? r.maxValue() : 0;
                    return (amount < 0 && v > 0) || (amount > 0 && v < mx);
                };
                // fallbackViewport: the size of the layout (boundsInParent).
                const Size fallback = node.layout ? Size{ float(node.layout->Size().width), float(node.layout->Size().height) } : Size{};
                const std::optional<float> viewport = c.scrollViewportLength ? std::optional(c.scrollViewportLength()) : std::nullopt;
                if (!c.scrollBy) return false;
                if (const auto& x = c.horizontalScrollAxisRange; x && horizontal) {
                    float amount = viewport.value_or(fallback.width);
                    if (left || back) amount = -amount;
                    if (x->reverseScrolling) amount = -amount;
                    if (node.rtl && (left || right)) amount = -amount;
                    if (canScroll(*x, amount)) {
                        if (c.pageLeft || c.pageRight) {
                            const auto& page = amount > 0 ? c.pageRight : c.pageLeft;
                            return page && page();
                        }
                        return c.scrollBy(amount, 0);
                    }
                }
                if (const auto& y = c.verticalScrollAxisRange; y && vertical) {
                    float amount = viewport.value_or(fallback.height);
                    if (up || back) amount = -amount;
                    if (y->reverseScrolling) amount = -amount;
                    if (canScroll(*y, amount)) {
                        if (c.pageUp || c.pageDown) {
                            const auto& page = amount > 0 ? c.pageDown : c.pageUp;
                            return page && page();
                        }
                        return c.scrollBy(0, amount);
                    }
                }
                return false;
            }
            case PageUp: return c.pageUp && c.pageUp();
            case PageDown: return c.pageDown && c.pageDown();
            case PageLeft: return c.pageLeft && c.pageLeft();
            case PageRight: return c.pageRight && c.pageRight();
            case SetProgress: return args.progress && c.setProgress && c.setProgress(*args.progress);
            case Focus:
                if (m_Host.requestFocusFromTouch) m_Host.requestFocusFromTouch();
                return c.requestFocus && c.requestFocus();
            case ClearFocus:
                if (c.focused == std::optional(true)) { m_Owner.ClearFocus(false); return true; }
                return false;
            case SetText: return c.setText && c.setText(args.text.value_or(""));
            case ImeEnter: return c.onImeAction && c.onImeAction();
            case Paste: return c.pasteText && c.pasteText();
            case Cut: return c.cutText && c.cutText();
            case Expand: return c.expand && c.expand();
            case Collapse: return c.collapse && c.collapse();
            case Dismiss: return c.dismiss && c.dismiss();
            case ShowOnScreen: return ScrollOntoScreen(node);
            default: {
                auto labels = m_ActionIdToLabel.find(id);
                if (labels == m_ActionIdToLabel.end()) return false;
                auto label = labels->second.find(action);
                if (label == labels->second.end()) return false;
                for (const auto& a : c.customActions) if (a.label == label->second) return a.action && a.action();
                return false;
            }
        }
    }

    bool AccessibilityDelegate::ScrollOntoScreen(const AccessibilityNode& node) {
        auto parentOf = [&](const AccessibilityNode* n) { return n ? FindAny(n->parent) : nullptr; };
        const AccessibilityNode* scrollable = parentOf(&node);
        while (scrollable && !scrollable->config.scrollBy) scrollable = parentOf(scrollable);
        if (!scrollable) {
            const Rect& b = node.boundsInRoot;
            const IntRect r{ int(std::floor(b.left)), int(std::floor(b.top)), int(std::ceil(b.right)), int(std::ceil(b.bottom)) };
            return m_Host.requestRectangleOnScreen && m_Host.requestRectangleOnScreen(r);
        }
        // The node's own coordinator bounds (unclipped position and size).
        const Rect nodeRect = node.unclippedBoundsInRoot;
        bool result = false;
        Offset accumulated;
        for (const AccessibilityNode* a = scrollable; a; a = parentOf(a)) {
            if (!a->config.scrollBy || !a->layout) continue;
            // scrollDxDyForNodeVisible: the viewport (the ancestor in its parent, inset by its
            // content padding) against the node moved by what earlier ancestors scrolled.
            Rect viewport = a->layout->BoundsInRoot();
            if (a->config.scrollViewportPadding) {
                const auto [before, after] = a->config.scrollViewportPadding();
                float l = 0, r = 0, t = 0, b = 0;
                if (a->config.horizontalScrollAxisRange) {
                    const bool reverse = a->config.horizontalScrollAxisRange->reverseScrolling != a->rtl;
                    l = reverse ? after : before;
                    r = reverse ? before : after;
                }
                if (a->config.verticalScrollAxisRange) {
                    t = a->config.verticalScrollAxisRange->reverseScrolling ? after : before;
                    b = a->config.verticalScrollAxisRange->reverseScrolling ? before : after;
                }
                const bool roomX = viewport.Width() > l + r, roomY = viewport.Height() > t + b;
                viewport = { roomX ? viewport.left + l : viewport.left, roomY ? viewport.top + t : viewport.top,
                             roomX ? viewport.right - r : viewport.right, roomY ? viewport.bottom - b : viewport.bottom };
            }
            const Rect target = nodeRect.Translate(accumulated);
            auto delta = [](float p, float q) { return (p < 0) == (q < 0) ? (std::abs(p) < std::abs(q) ? p : q) : 0.0f; };
            const Offset d{ delta(target.left - viewport.left, target.right - viewport.right),
                            delta(target.top - viewport.top, target.bottom - viewport.bottom) };
            Offset scroll = d;
            if (!(d.x == 0 && d.y == 0)) {
                if (a->config.horizontalScrollAxisRange && a->config.horizontalScrollAxisRange->reverseScrolling) scroll.x = -scroll.x;
                if (node.rtl) scroll.x = -scroll.x;
                if (a->config.verticalScrollAxisRange && a->config.verticalScrollAxisRange->reverseScrolling) scroll.y = -scroll.y;
            }
            result = a->config.scrollBy(scroll.x, scroll.y) || result;
            accumulated = accumulated - d;
        }
        return result;
    }

    AccessibilityExtraData AccessibilityDelegate::ExtraData(int id, const std::string& key, const AccessibilityActionArgs& args) {
        AccessibilityExtraData out;
        const AccessibilityNode* found = Find(id);
        if (!found) return out;
        const AccessibilityNode& node = *found;
        const auto& c = node.config;
        if (key == kExtraDataTraversalBeforeKey) {
            if (auto it = m_IdToBefore.find(id); it != m_IdToBefore.end()) out.intValue = it->second;
        } else if (key == kExtraDataTraversalAfterKey) {
            if (auto it = m_IdToAfter.find(id); it != m_IdToAfter.end()) out.intValue = it->second;
        } else if (key == kExtraDataTextCharacterLocationKey && c.getTextLayoutResult) {
            const auto text = IterableText(c);
            const int start = args.characterStart.value_or(-1), length = args.characterLength.value_or(-1);
            const int textLength = text ? int(U16(*text).size()) : INT_MAX;
            if (length <= 0 || start < 0 || start >= textLength) return out;
            const auto layout = c.getTextLayoutResult();
            if (!layout || !node.layout) return out;
            // Relative to the text layout: the node's inner coordinator.
            const Rect inner = node.layout->InnerRectInRoot();
            const int total = text::Utf16Length(layout->LayoutInput().text.Text());
            for (int i = 0; i < length; ++i) {
                if (start + i >= total) { out.rects.push_back(std::nullopt); continue; }
                const auto box = layout->BoundingBox(start + i);
                const Rect r = Rect{ box.x, box.y, box.x + box.w, box.y + box.h }.Translate({ inner.left, inner.top });
                const Rect v = r.Intersect(node.boundsInRoot);
                const bool overlaps = r.left < node.boundsInRoot.right && node.boundsInRoot.left < r.right &&
                                      r.top < node.boundsInRoot.bottom && node.boundsInRoot.top < r.bottom;
                out.rects.push_back(overlaps ? std::optional(v) : std::nullopt);
            }
        } else if (key == kExtraDataTestTagKey && c.testTag) {
            out.stringValue = c.testTag;
        } else if (key == kExtraDataIdKey) {
            out.intValue = node.fake ? node.id : (node.layout ? node.layout->SemanticsId() : node.id);
        } else if ((key == kExtraDataShapeTypeKey || key == kExtraDataShapeRectKey || key == kExtraDataShapeRectCornersKey ||
                    key == kExtraDataShapeRegionKey) && c.shape) {
            // getShapeBounds: the node's bounds; the outline in screen space relative to them.
            const Rect b = node.boundsInRoot;
            const gfx::Outline o = c.shape->CreateOutline({ b.Width(), b.Height() }, m_Owner.Direction(), m_Owner.GetDensity());
            if (const auto* r = std::get_if<Rect>(&o.value)) {
                out.shapeType = 0;
                out.shapeRect = r->Translate({ b.left, b.top });
            } else if (const auto* rr = std::get_if<gfx::RoundRect>(&o.value)) {
                out.shapeType = 1;
                out.shapeRect = rr->rect.Translate({ b.left, b.top });
                out.shapeCorners = { rr->topLeft.x, rr->topLeft.y, rr->topRight.x, rr->topRight.y,
                                     rr->bottomRight.x, rr->bottomRight.y, rr->bottomLeft.x, rr->bottomLeft.y };
            } else {
                out.shapeType = 2;
                const Rect pb = o.Bounds().Translate({ b.left, b.top });
                out.shapeRegion.push_back(pb);
            }
            if (key == kExtraDataShapeRectKey || key == kExtraDataShapeRectCornersKey || key == kExtraDataShapeRegionKey) out.shapeType.reset();
            if (key != kExtraDataShapeRectCornersKey) out.shapeCorners.clear();
            if (key == kExtraDataShapeRectCornersKey || key == kExtraDataShapeRegionKey) out.shapeRect.reset();
            if (key != kExtraDataShapeRegionKey && key != kExtraDataShapeTypeKey) out.shapeRegion.clear();
        }
        return out;
    }

    // --- hover --------------------------------------------------------------------------------

    int AccessibilityDelegate::HitTest(float x, float y) {
        Nodes();
        // hitTestSemantics: the semantics nodes under the point along the topmost path, deepest
        // first; the first that is shown and important.
        std::vector<const AccessibilityNode*> path;
        const AccessibilityNode* at = FindAny(kHostViewId);
        while (at) {
            path.push_back(at);
            const AccessibilityNode* next = nullptr;
            for (auto it = at->children.rbegin(); it != at->children.rend(); ++it) {
                const AccessibilityNode* c = FindAny(*it);
                if (!c || !c->layout || !c->layout->IsPlaced()) continue;
                if (c->touchBoundsInRoot.Contains({ x, y })) { next = c; break; }
            }
            at = next;
        }
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const AccessibilityNode* n = *it;
            if (n->id == kHostViewId) continue;
            auto cur = m_Nodes.find(n->id);
            const bool hiddenSubtree = cur != m_Nodes.end() && cur->second.isInMergingHiddenSubtree;
            if (n->hidden || hiddenSubtree) continue;
            if (!n->important) continue;
            return n->id;
        }
        return kInvalidId;
    }

    bool AccessibilityDelegate::DispatchHover(int action, float x, float y) {
        constexpr int kHoverMove = 7, kHoverEnter = 9, kHoverExit = 10;
        if (!TouchExplorationEnabled()) return false;
        auto update = [&](int id) {
            if (m_Hovered == id) return;
            const int previous = m_Hovered;
            m_Hovered = id;
            SendEventForVirtualView(id, ViewHoverEnter);
            SendEventForVirtualView(previous, ViewHoverExit);
        };
        if (action == kHoverMove || action == kHoverEnter) {
            const int id = HitTest(x, y);
            update(id);
            return id != kInvalidId;
        }
        if (action == kHoverExit) {
            if (m_Hovered != kInvalidId) { update(kInvalidId); return true; }
            return false;
        }
        return false;
    }

    bool AccessibilityDelegate::CanScroll(bool vertical, int direction, Offset position) {
        if (std::isnan(position.x) || std::isnan(position.y)) return false;
        for (const auto& [id, n] : Nodes()) {
            const IntRect& b = n.adjustedBounds;
            if (!(position.x >= float(b.left) && position.x < float(b.right) && position.y >= float(b.top) && position.y < float(b.bottom))) continue;
            const auto& range = vertical ? n.config.verticalScrollAxisRange : n.config.horizontalScrollAxisRange;
            if (!range) continue;
            int actual = range->reverseScrolling ? -direction : direction;
            if (direction == 0 && range->reverseScrolling) actual = -1;
            const float v = range->value ? range->value() : 0, max = range->maxValue ? range->maxValue() : 0;
            if (actual < 0 ? v > 0 : v < max) return true;
        }
        return false;
    }

    // --- events -------------------------------------------------------------------------------

    AccessibilityEvent AccessibilityDelegate::CreateEvent(int id, int type) const {
        AccessibilityEvent e;
        e.type = type;
        e.id = id;
        if (m_Enabled) {
            auto it = m_Nodes.find(id);
            if (it != m_Nodes.end()) {
                const auto& c = it->second.config;
                e.password = c.password && c.isPasswordObfuscated.value_or(true);
                e.dataSensitive = c.isSensitiveData.value_or(false);
            }
        }
        return e;
    }

    void AccessibilityDelegate::Send(AccessibilityEvent e) {
        if (!m_Enabled) return;
        m_Events.push_back(std::move(e));
    }

    void AccessibilityDelegate::SendEventForVirtualView(int id, int type, std::optional<int> contentChange,
                                                        const std::vector<std::string>* contentDescription) {
        if (id == kInvalidId || !m_Enabled) return;
        AccessibilityEvent e = CreateEvent(id, type);
        if (contentChange) e.contentChangeTypes = contentChange;
        if (contentDescription) e.contentDescription = Join(*contentDescription, ",");
        Send(std::move(e));
    }

    void AccessibilityDelegate::SendPaneChangeEvents(int id, int contentChangeType, const std::optional<std::string>& title) {
        AccessibilityEvent e = CreateEvent(id, WindowStateChanged);
        e.contentChangeTypes = contentChangeType;
        if (title) e.text.push_back(*title);
        Send(std::move(e));
    }

    namespace {
        int SemanticsIdOf(Owner& owner, LayoutNode& n) { return &n == &owner.Root() ? kHostViewId : n.SemanticsId(); }
    }

    void AccessibilityDelegate::OnSemanticsChange() { m_Invalid = true; }

    bool AccessibilityDelegate::OnLayoutChange(LayoutNode& node) {
        m_Invalid = true;
        // Content capture flushes after layout changes too (its boundsUpdatesEventLoop).
        if (!m_Enabled) return m_ContentCapture->Enabled();
        const bool first = m_SubtreeChangedLayoutNodes.empty();
        NotifySubtreeChanged(&node);
        return first && !m_SubtreeChangedLayoutNodes.empty();
    }

    void AccessibilityDelegate::NotifySubtreeChanged(LayoutNode* layout) {
        if (!layout) return;
        if (!m_SubtreeChangedSet.insert(layout).second) return;
        m_SubtreeChangedLayoutNodes.push_back(layout);
        m_SubtreeChangedAlive.push_back(layout->AliveToken());
    }

    void AccessibilityDelegate::OnScrollChanged(LayoutNode& node, Offset delta) {
        if (!m_Enabled || (delta.x == 0 && delta.y == 0)) return;
        const int id = SemanticsIdOf(m_Owner, node);
        for (auto& s : m_ScrollScopes)
            if (s.id == id) {
                const Offset forward = m_Owner.Direction() == LayoutDirection::Rtl ? Offset{ delta.x, -delta.y } : Offset{ -delta.x, -delta.y };
                s.pixelDelta = s.pixelDelta + forward;
                return;
            }
    }

    void AccessibilityDelegate::FlushBoundsUpdates() {
        if (m_Enabled) {
            // The scroll positions read since the last pass (the snapshot observation), through
            // the current ranges of the nodes still there.
            const auto& nodes = Nodes();
            std::erase_if(m_ScrollScopes, [&](const ScrollScope& s) { return !nodes.count(s.id); });
            for (auto& s : m_ScrollScopes) {
                const auto& c = nodes.at(s.id).config;
                s.x = c.horizontalScrollAxisRange;
                s.y = c.verticalScrollAxisRange;
                ScheduleScrollEventIfNeeded(s);
            }
            std::unordered_set<int> sent;
            for (std::size_t i = 0; i < m_SubtreeChangedLayoutNodes.size(); ++i) {
                if (m_SubtreeChangedAlive[i].expired()) continue;
                LayoutNode& n = *m_SubtreeChangedLayoutNodes[i];
                SendSubtreeChangeEvents(n, sent);
                SendScrolledEvent(n);
            }
        } else {
            for (auto& s : m_ScrollScopes) s.pixelDelta = {};
        }
        m_SubtreeChangedLayoutNodes.clear();
        m_SubtreeChangedSet.clear();
        m_SubtreeChangedAlive.clear();
        m_PendingHorizontalScroll.clear();
        m_PendingVerticalScroll.clear();
    }

    void AccessibilityDelegate::SendSubtreeChangeEvents(LayoutNode& layout, std::unordered_set<int>& sent) {
        if (!layout.GetOwner() && &layout != &m_Owner.Root()) return;
        // The node's semantics node (or its closest), then its closest merging ancestor.
        LayoutNode* sem = &layout;
        while (sem && sem->Parent() && !HasSemantics(*sem)) sem = sem->Parent();
        if (!sem) return;
        SemanticsConfiguration config;
        if (sem->Parent()) CalculateSemanticsConfiguration(*sem, config);
        if (!config.mergeDescendants)
            for (LayoutNode* p = sem->Parent(); p; p = p->Parent()) {
                if (!p->Parent() || !HasSemantics(*p)) continue;
                SemanticsConfiguration pc;
                CalculateSemanticsConfiguration(*p, pc);
                if (pc.mergeDescendants) { sem = p; break; }
            }
        const int id = SemanticsIdOf(m_Owner, *sem);
        if (!sent.insert(id).second) return;
        SendEventForVirtualView(id, WindowContentChanged, Subtree);
    }

    void AccessibilityDelegate::SendScrolledEvent(LayoutNode& layout) {
        const int id = SemanticsIdOf(m_Owner, layout);
        auto px = m_PendingHorizontalScroll.find(id);
        auto py = m_PendingVerticalScroll.find(id);
        if (px == m_PendingHorizontalScroll.end() && py == m_PendingVerticalScroll.end()) return;
        AccessibilityEvent e = CreateEvent(id, ViewScrolled);
        if (px != m_PendingHorizontalScroll.end()) {
            e.scrollX = int(px->second.value ? px->second.value() : 0);
            e.maxScrollX = int(px->second.maxValue ? px->second.maxValue() : 0);
        }
        if (py != m_PendingVerticalScroll.end()) {
            e.scrollY = int(py->second.value ? py->second.value() : 0);
            e.maxScrollY = int(py->second.maxValue ? py->second.maxValue() : 0);
        }
        for (auto& s : m_ScrollScopes)
            if (s.id == id) {
                e.scrollDeltaX = RoundI(s.pixelDelta.x);
                e.scrollDeltaY = RoundI(s.pixelDelta.y);
                s.pixelDelta = {};
            }
        Send(std::move(e));
    }

    void AccessibilityDelegate::ScheduleScrollEventIfNeeded(ScrollScope& scope) {
        const float dx = scope.x && scope.oldX ? (scope.x->value ? scope.x->value() : 0) - *scope.oldX : 0;
        const float dy = scope.y && scope.oldY ? (scope.y->value ? scope.y->value() : 0) - *scope.oldY : 0;
        if (dx != 0 || dy != 0) {
            if (m_Host.invalidate) m_Host.invalidate();
            auto it = m_Nodes.find(scope.id);
            if (it != m_Nodes.end() && it->second.layout) {
                if (scope.x) m_PendingHorizontalScroll.insert_or_assign(scope.id, *scope.x);
                if (scope.y) m_PendingVerticalScroll.insert_or_assign(scope.id, *scope.y);
                NotifySubtreeChanged(it->second.layout);
            }
        }
        if (scope.x) scope.oldX = scope.x->value ? scope.x->value() : 0;
        if (scope.y) scope.oldY = scope.y->value ? scope.y->value() : 0;
    }

    void AccessibilityDelegate::CheckForSemanticsChanges() {
        Nodes();
        if (m_Enabled) SendStructureChangeEvents(kHostViewId);
        SendSemanticsPropertyChangeEvents();
        UpdateSemanticsNodesCopyAndPanes();
    }

    void AccessibilityDelegate::SendStructureChangeEvents(int id) {
        const AccessibilityNode* node = FindAny(id);
        if (!node) return;
        const NodeCopy* old = id == kHostViewId ? &m_PreviousRoot : nullptr;
        if (!old) { auto it = m_Previous.find(id); if (it == m_Previous.end()) return; old = &it->second; }
        std::unordered_set<int> now;
        for (int k : node->replacedChildren) {
            if (!m_Nodes.count(k)) continue;
            if (!old->children.count(k)) { NotifySubtreeChanged(node->layout); return; }
            now.insert(k);
        }
        for (int k : old->children)
            if (!now.count(k)) { NotifySubtreeChanged(node->layout); return; }
        for (int k : node->replacedChildren)
            if (m_Previous.count(k) && m_Nodes.count(k)) SendStructureChangeEvents(k);
    }

    void AccessibilityDelegate::SendSemanticsPropertyChangeEvents() {
        std::vector<ScrollScope> oldScopes = std::move(m_ScrollScopes);
        m_ScrollScopes.clear();
        for (const auto& [id, cur] : m_Nodes) {
            auto prev = m_Previous.find(id);
            if (prev == m_Previous.end()) continue;
            const SemanticsConfiguration& n = cur.config;
            const SemanticsConfiguration& o = prev->second.config;
            bool propertyChanged = false, textSubstitutionChanged = false;

            // Scroll ranges: observe each scrolling node (a new one counts as changed).
            if (n.horizontalScrollAxisRange || n.verticalScrollAxisRange) {
                bool newly = true;
                ScrollScope scope;
                scope.id = id;
                for (auto& s : oldScopes) if (s.id == id) { scope = s; newly = false; break; }
                scope.x = n.horizontalScrollAxisRange;
                scope.y = n.verticalScrollAxisRange;
                m_ScrollScopes.push_back(scope);
                const bool rangesChanged = bool(n.horizontalScrollAxisRange) != bool(o.horizontalScrollAxisRange) ||
                                           bool(n.verticalScrollAxisRange) != bool(o.verticalScrollAxisRange);
                if (newly || rangesChanged) {
                    NotifySubtreeChanged(cur.layout);
                    ScheduleScrollEventIfNeeded(m_ScrollScopes.back());
                }
            }
            if (n.paneTitle && n.paneTitle != o.paneTitle && o.paneTitle) SendPaneChangeEvents(id, PaneTitle, n.paneTitle);
            if (n.stateDescription && n.stateDescription != o.stateDescription) {
                SendEventForVirtualView(id, WindowContentChanged, a11y::StateDescription);
                SendEventForVirtualView(id, WindowContentChanged, Undefined);
            }
            if (n.toggleableState && n.toggleableState != o.toggleableState) {
                SendEventForVirtualView(id, WindowContentChanged, Checked);
                SendEventForVirtualView(id, WindowContentChanged, Undefined);
            }
            if (n.error && n.error != o.error) SendEventForVirtualView(id, WindowContentChanged, Error | ContentInvalid);
            if (n.progressBarRangeInfo && n.progressBarRangeInfo != o.progressBarRangeInfo) {
                SendEventForVirtualView(id, WindowContentChanged, a11y::StateDescription);
                SendEventForVirtualView(id, WindowContentChanged, Undefined);
            }
            if (n.selected && n.selected != o.selected) {
                if (n.role == Role::Tab) {
                    if (*n.selected) {
                        AccessibilityEvent e = CreateEvent(id, ViewSelected);
                        const SemanticsConfiguration merged = MergedConfig(cur);
                        if (!merged.contentDescription.empty()) e.contentDescription = Join(merged.contentDescription, ",");
                        if (!merged.text.empty()) e.text.push_back(Join(merged.text, ","));
                        Send(std::move(e));
                    } else {
                        SendEventForVirtualView(id, WindowContentChanged, Undefined);
                    }
                } else {
                    SendEventForVirtualView(id, WindowContentChanged, a11y::StateDescription);
                    SendEventForVirtualView(id, WindowContentChanged, Undefined);
                }
            }
            if (!n.contentDescription.empty() && n.contentDescription != o.contentDescription)
                SendEventForVirtualView(id, WindowContentChanged, ContentDescription, &n.contentDescription);
            if ((n.textSubstitution && n.textSubstitution != o.textSubstitution) ||
                (n.isShowingTextSubstitution && n.isShowingTextSubstitution != o.isShowingTextSubstitution))
                textSubstitutionChanged = true;
            if (n.editableText && n.editableText != o.editableText) {
                if (n.setText) {
                    const std::u16string oldText = U16(o.editableText.value_or("")), newText = U16(*n.editableText);
                    const std::string trimmed = TrimToSize(*n.editableText, kParcelSafeTextLength);
                    int start = 0, end = 0;
                    const int oldLen = int(oldText.size()), newLen = int(newText.size()), minLen = std::min(oldLen, newLen);
                    while (start < minLen && oldText[std::size_t(start)] == newText[std::size_t(start)]) ++start;
                    while (end < minLen - start && oldText[std::size_t(oldLen - 1 - end)] == newText[std::size_t(newLen - 1 - end)]) ++end;
                    const bool oldPassword = o.password && o.isPasswordObfuscated.value_or(true);
                    const bool newPassword = n.password && n.isPasswordObfuscated.value_or(true);
                    const bool oldField = o.editableText.has_value();
                    const bool became = oldField && oldPassword != newPassword;
                    AccessibilityEvent e;
                    if (became) {
                        e = CreateEvent(id, ViewTextSelectionChanged);
                        e.fromIndex = 0; e.toIndex = 0; e.itemCount = newLen;
                        e.text.push_back(trimmed);
                    } else {
                        e = CreateEvent(id, ViewTextChanged);
                        e.fromIndex = start;
                        e.removedCount = oldLen - end - start;
                        e.addedCount = newLen - end - start;
                        e.beforeText = o.editableText.value_or("");
                        e.text.push_back(trimmed);
                    }
                    e.className = "android.widget.EditText";
                    Send(e);
                    if (became && n.textSelection) {
                        e.fromIndex = std::min(n.textSelection->first, n.textSelection->second);
                        e.toIndex = std::max(n.textSelection->first, n.textSelection->second);
                        Send(e);
                    }
                } else {
                    SendEventForVirtualView(id, WindowContentChanged, Text);
                }
            }
            if (n.textSelection && n.textSelection != o.textSelection) {
                const std::string text = n.editableText.value_or("");
                AccessibilityEvent e = CreateEvent(id, ViewTextSelectionChanged);
                e.fromIndex = std::min(n.textSelection->first, n.textSelection->second);
                e.toIndex = std::max(n.textSelection->first, n.textSelection->second);
                e.itemCount = int(U16(text).size());
                e.text.push_back(TrimToSize(text, kParcelSafeTextLength));
                Send(std::move(e));
                SendPendingTextTraversedEvent(id);
            }
            if (n.focused && n.focused != o.focused) {
                if (*n.focused) {
                    m_Focused = id;
                    if (!cur.touchBoundsInRoot.Empty()) m_ReportedFocused = id;
                    Send(CreateEvent(id, ViewFocused));
                } else if (m_Focused == id) {
                    m_Focused = kInvalidId;
                }
                SendEventForVirtualView(id, WindowContentChanged, Undefined);
            }
            if (!n.customActions.empty()) {
                std::unordered_set<std::string> labels, oldLabels;
                for (const auto& a : n.customActions) labels.insert(a.label);
                for (const auto& a : o.customActions) oldLabels.insert(a.label);
                if (!o.customActions.empty() ? labels != oldLabels : true) propertyChanged = true;
            }
            // Every other key: a new or different value (actions by label and presence).
            auto other = [](const SemanticsConfiguration& x, const SemanticsConfiguration& y) {
                SemanticsConfiguration a = x, b = y;
                for (SemanticsConfiguration* c : { &a, &b }) {
                    c->paneTitle.reset(); c->stateDescription.reset(); c->toggleableState.reset(); c->error.reset();
                    c->progressBarRangeInfo.reset(); c->selected.reset(); c->contentDescription.clear();
                    c->textSubstitution.reset(); c->isShowingTextSubstitution.reset(); c->editableText.reset();
                    c->textSelection.reset(); c->focused.reset(); c->customActions.clear();
                    c->horizontalScrollAxisRange.reset(); c->verticalScrollAxisRange.reset();
                    c->mergeDescendants = c->clearAndSet = false;
                }
                return !a.Equivalent(b);
            };
            if (other(n, o)) propertyChanged = true;
            if (textSubstitutionChanged || (o.isShowingTextSubstitution && !n.isShowingTextSubstitution))
                if (InfoText(o) != InfoText(n)) SendEventForVirtualView(id, WindowContentChanged, Text);
            if (propertyChanged) SendEventForVirtualView(id, WindowContentChanged, Undefined);
        }
        // sendTypeViewFocusedEventIfNeeded: input focus that moved without a Focused change.
        int focusedId = kInvalidId;
        if (FocusTargetNode* t = m_Owner.ActiveFocus())
            if (LayoutNode* l = m_Owner.NodeOf(*t)) focusedId = SemanticsIdOf(m_Owner, *l);
        if (focusedId != m_ReportedFocused) {
            m_ReportedFocused = kInvalidId;
            auto it = m_Nodes.find(focusedId);
            if (it != m_Nodes.end() && it->second.config.focused == std::optional(true) && !it->second.touchBoundsInRoot.Empty()) {
                m_Focused = focusedId;
                m_ReportedFocused = focusedId;
                Send(CreateEvent(focusedId, ViewFocused));
            }
        }
    }

    void AccessibilityDelegate::UpdateSemanticsNodesCopyAndPanes() {
        std::vector<int> gone;
        for (int id : m_PaneDisplayed) {
            auto it = m_Nodes.find(id);
            if (it == m_Nodes.end() || !it->second.config.paneTitle) {
                gone.push_back(id);
                auto prev = m_Previous.find(id);
                SendPaneChangeEvents(id, PaneDisappeared, prev != m_Previous.end() ? prev->second.config.paneTitle : std::nullopt);
            }
        }
        for (int id : gone) m_PaneDisplayed.erase(id);
        m_Previous.clear();
        auto copy = [&](const AccessibilityNode& n) {
            NodeCopy c;
            c.config = n.config;
            for (int k : n.replacedChildren) if (m_Nodes.count(k)) c.children.insert(k);
            return c;
        };
        for (const auto& [id, n] : m_Nodes) {
            if (n.config.paneTitle && m_PaneDisplayed.insert(id).second) SendPaneChangeEvents(id, PaneAppeared, n.config.paneTitle);
            m_Previous[id] = copy(n);
        }
        if (const AccessibilityNode* root = FindAny(kHostViewId)) m_PreviousRoot = copy(*root);
    }

    // --- text segment iterators (AccessibilityIterators) --------------------------------------

    namespace a11y {
        namespace {
            Segment Range(int start, int end) {
                if (start < 0 || end < 0 || start == end) return std::nullopt;
                return std::pair{ start, end };
            }
            // BreakIterator.isBoundary through preceding / following.
            bool IsBoundary(const std::u16string& text, int i, const std::function<int(int, bool)>& brk) {
                if (i <= 0 || i >= int(text.size())) return true;
                const int p = brk(i, false);
                return brk(p < 0 ? 0 : p, true) == i;
            }
            bool LetterOrDigit(const std::u16string& text, int index) {
                if (index < 0 || index >= int(text.size())) return false;
                char32_t cp = text[std::size_t(index)];
                if (cp >= 0xD800 && cp < 0xDC00 && index + 1 < int(text.size())) cp = 0x10000 + ((cp - 0xD800) << 10) + (text[std::size_t(index) + 1] - 0xDC00);
                if (cp < 0x80) return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
                // Character.isLetterOrDigit outside ASCII: everything but spaces, punctuation and
                // symbols of the common blocks.
                if (cp < 0xC0) return cp == 0xAA || cp == 0xB2 || cp == 0xB3 || cp == 0xB5 || cp == 0xB9 || cp == 0xBA || (cp >= 0xBC && cp <= 0xBE);
                if (cp == 0xD7 || cp == 0xF7) return false;
                if ((cp >= 0x2000 && cp <= 0x2BFF) || (cp >= 0x3000 && cp <= 0x3003) || (cp >= 0x3008 && cp <= 0x3020) ||
                    (cp >= 0xFE10 && cp <= 0xFE6F && !(cp >= 0xFE20 && cp <= 0xFE2F)) || (cp >= 0xFF01 && cp <= 0xFF0F) ||
                    (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65) ||
                    (cp >= 0x1F000 && cp <= 0x1FAFF) || cp == 0x00A0 || cp == 0xFEFF)
                    return false;
                return true;
            }
            int LineEdge(const text::TextLayoutResult& layout, int line, bool start) {
                // getLineEdgeIndex: DirectionStart is Rtl, DirectionEnd Ltr.
                const int lineStart = layout.LineStart(line);
                const bool paragraphRtl = layout.ParagraphDirection(lineStart) == text::ResolvedTextDirection::Rtl;
                const bool directionRtl = start;
                return directionRtl != paragraphRtl ? layout.LineStart(line) : layout.LineEnd(line) - 1;
            }
        }

        Segment CharacterFollowing(const std::u16string& text, int current, const std::function<int(int, bool)>& brk) {
            const int n = int(text.size());
            if (n <= 0 || current >= n) return std::nullopt;
            int start = std::max(current, 0);
            while (!IsBoundary(text, start, brk)) {
                start = brk(start, true);
                if (start < 0) return std::nullopt;
            }
            const int end = brk(start, true);
            if (end < 0) return std::nullopt;
            return Range(start, end);
        }
        Segment CharacterPreceding(const std::u16string& text, int current, const std::function<int(int, bool)>& brk) {
            const int n = int(text.size());
            if (n <= 0 || current <= 0) return std::nullopt;
            int end = std::min(current, n);
            while (!IsBoundary(text, end, brk)) {
                end = brk(end, false);
                if (end < 0) return std::nullopt;
            }
            const int start = brk(end, false);
            if (start < 0) return std::nullopt;
            return Range(start, end);
        }
        Segment WordFollowing(const std::u16string& text, int current, const std::function<int(int, bool)>& brk) {
            const int n = int(text.size());
            if (n <= 0 || current >= n) return std::nullopt;
            auto isStart = [&](int i) { return LetterOrDigit(text, i) && (i == 0 || !LetterOrDigit(text, i - 1)); };
            auto isEnd = [&](int i) { return (i > 0 && LetterOrDigit(text, i - 1)) && (i == n || !LetterOrDigit(text, i)); };
            int start = std::max(current, 0);
            while (!LetterOrDigit(text, start) && !isStart(start)) {
                start = brk(start, true);
                if (start < 0) return std::nullopt;
            }
            const int end = brk(start, true);
            if (end < 0 || !isEnd(end)) return std::nullopt;
            return Range(start, end);
        }
        Segment WordPreceding(const std::u16string& text, int current, const std::function<int(int, bool)>& brk) {
            const int n = int(text.size());
            if (n <= 0 || current <= 0) return std::nullopt;
            auto isStart = [&](int i) { return LetterOrDigit(text, i) && (i == 0 || !LetterOrDigit(text, i - 1)); };
            auto isEnd = [&](int i) { return (i > 0 && LetterOrDigit(text, i - 1)) && (i == n || !LetterOrDigit(text, i)); };
            int end = std::min(current, n);
            while (end > 0 && !LetterOrDigit(text, end - 1) && !isEnd(end)) {
                end = brk(end, false);
                if (end < 0) return std::nullopt;
            }
            const int start = brk(end, false);
            if (start < 0 || !isStart(start)) return std::nullopt;
            return Range(start, end);
        }
        Segment ParagraphFollowing(const std::u16string& text, int current) {
            const int n = int(text.size());
            if (n <= 0 || current >= n) return std::nullopt;
            auto isStart = [&](int i) { return text[std::size_t(i)] != u'\n' && (i == 0 || text[std::size_t(i) - 1] == u'\n'); };
            auto isEnd = [&](int i) { return i > 0 && text[std::size_t(i) - 1] != u'\n' && (i == n || text[std::size_t(i)] == u'\n'); };
            int start = std::max(current, 0);
            while (start < n && text[std::size_t(start)] == u'\n' && !isStart(start)) ++start;
            if (start >= n) return std::nullopt;
            int end = start + 1;
            while (end < n && !isEnd(end)) ++end;
            return Range(start, end);
        }
        Segment ParagraphPreceding(const std::u16string& text, int current) {
            const int n = int(text.size());
            if (n <= 0 || current <= 0) return std::nullopt;
            auto isStart = [&](int i) { return text[std::size_t(i)] != u'\n' && (i == 0 || text[std::size_t(i) - 1] == u'\n'); };
            auto isEnd = [&](int i) { return i > 0 && text[std::size_t(i) - 1] != u'\n' && (i == n || text[std::size_t(i)] == u'\n'); };
            int end = std::min(current, n);
            while (end > 0 && text[std::size_t(end) - 1] == u'\n' && !isEnd(end)) --end;
            if (end <= 0) return std::nullopt;
            int start = end - 1;
            while (start > 0 && !isStart(start)) --start;
            return Range(start, end);
        }
        Segment LineFollowing(const std::u16string& text, const text::TextLayoutResult& layout, int current) {
            const int n = int(text.size());
            if (n <= 0 || current >= n) return std::nullopt;
            int next;
            if (current < 0) next = layout.LineForOffset(0);
            else {
                const int line = layout.LineForOffset(current);
                next = LineEdge(layout, line, true) == current ? line : line + 1;
            }
            if (next >= layout.LineCount()) return std::nullopt;
            return Range(LineEdge(layout, next, true), LineEdge(layout, next, false) + 1);
        }
        Segment LinePreceding(const std::u16string& text, const text::TextLayoutResult& layout, int current) {
            const int n = int(text.size());
            if (n <= 0 || current <= 0) return std::nullopt;
            int previous;
            if (current > n) previous = layout.LineForOffset(n);
            else {
                const int line = layout.LineForOffset(current);
                previous = LineEdge(layout, line, false) + 1 == current ? line : line - 1;
            }
            if (previous < 0) return std::nullopt;
            return Range(LineEdge(layout, previous, true), LineEdge(layout, previous, false) + 1);
        }
        Segment PageFollowing(const std::u16string& text, const text::TextLayoutResult& layout, int pageHeight, int current) {
            const int n = int(text.size());
            if (n <= 0 || current >= n) return std::nullopt;
            const int start = std::max(0, current);
            const int line = layout.LineForOffset(start);
            const float nextPageStartY = layout.LineTop(line) + float(pageHeight);
            const float lastLineTop = layout.LineTop(layout.LineCount() - 1);
            const int endLine = nextPageStartY < lastLineTop ? layout.LineForVerticalPosition(nextPageStartY) - 1 : layout.LineCount() - 1;
            return Range(start, LineEdge(layout, endLine, false) + 1);
        }
        Segment PagePreceding(const std::u16string& text, const text::TextLayoutResult& layout, int pageHeight, int current) {
            const int n = int(text.size());
            if (n <= 0 || current <= 0) return std::nullopt;
            const int end = std::min(n, current);
            const int line = layout.LineForOffset(end);
            const float previousPageEndY = layout.LineTop(line) - float(pageHeight);
            int startLine = previousPageEndY > 0 ? layout.LineForVerticalPosition(previousPageEndY) : 0;
            if (end == n && startLine < line) startLine += 1;
            return Range(LineEdge(layout, startLine, true), end);
        }
    }


    // --- content capture and translation ------------------------------------------------------

    std::optional<ContentCaptureEvent> ContentCaptureManager::ToViewStructure(const AccessibilityNode& node, int index) {
        const auto& c = node.config;
        if (c.password) return std::nullopt;
        ContentCaptureEvent e;
        e.kind = ContentCaptureEvent::Kind::Appear;
        e.id = node.id;
        e.parent = node.parent;   // the root: kInvalidId (the view)
        e.index = index;
        e.timestamp = Now();
        if (c.testTag) e.testTag = c.testTag;
        if (c.isTraversalGroup) e.className = "android.widget.ViewGroup";
        if (!c.text.empty()) { e.className = "android.widget.TextView"; e.text = Join(c.text, "\n"); }
        if (c.editableText) { e.className = "android.widget.EditText"; e.text = c.editableText; }
        if (!c.contentDescription.empty()) e.contentDescription = Join(c.contentDescription, "\n");
        if (const char* cls = LegacyClassName(c.role)) e.className = cls;
        if (c.getTextLayoutResult)
            if (const auto layout = c.getTextLayoutResult()) {
                const auto& in = layout->LayoutInput();
                const auto& size = in.style.span.fontSize;
                if (size.IsSp()) e.textSizePx = size.value * in.density.density * in.density.fontScale;
            }
        // boundsInParent: against the parent's bounds coordinator.
        Rect b = node.boundsInRoot;
        if (const AccessibilityNode* p = m_Delegate.FindAnyNode(node.parent); p && p->id != kHostViewId)
            b = b.Translate({ -p->unclippedBoundsInRoot.left, -p->unclippedBoundsInRoot.top });
        e.left = int(b.left);
        e.top = int(b.top);
        e.width = int(b.Width());
        e.height = int(b.Height());
        return e;
    }

    void ContentCaptureManager::UpdateTranslationOnAppeared(const AccessibilityNode& node) {
        const auto& c = node.config;
        if (!m_ShowTranslated && c.isShowingTextSubstitution == std::optional(true)) {
            if (c.showTextSubstitution) c.showTextSubstitution(false);
        } else if (m_ShowTranslated && c.isShowingTextSubstitution == std::optional(false)) {
            if (c.showTextSubstitution) c.showTextSubstitution(true);
        }
    }

    void ContentCaptureManager::Appeared(int index, const AccessibilityNode& node) {
        if (!m_Enabled) return;
        UpdateTranslationOnAppeared(node);
        if (auto e = ToViewStructure(node, index)) m_Buffered.push_back(std::move(*e));
        const auto& nodes = m_Delegate.Nodes();
        int i = 0;
        for (int k : node.replacedChildren) {
            auto it = nodes.find(k);
            if (it == nodes.end()) continue;
            Appeared(i++, it->second);
        }
    }

    void ContentCaptureManager::Disappeared(const AccessibilityNode& node) {
        if (!m_Enabled) return;
        ContentCaptureEvent e;
        e.kind = ContentCaptureEvent::Kind::Disappear;
        e.id = node.id;
        e.timestamp = Now();
        m_Buffered.push_back(e);
        for (int k : node.replacedChildren)
            if (const AccessibilityNode* c = m_Delegate.FindAnyNode(k)) Disappeared(*c);
    }

    void ContentCaptureManager::OnStart() {
        if (const AccessibilityNode* root = m_Delegate.FindAnyNode(kHostViewId)) Appeared(-1, *root);
        FlushBuffered();
    }

    void ContentCaptureManager::OnStop() {
        if (const AccessibilityNode* root = m_Delegate.FindAnyNode(kHostViewId)) Disappeared(*root);
        FlushBuffered();
        m_Enabled = false;
    }

    void ContentCaptureManager::FlushBuffered() {
        if (m_Buffered.empty()) return;
        for (auto& e : m_Buffered) m_Events.push_back(std::move(e));
        m_Buffered.clear();
        ContentCaptureEvent flush;
        flush.kind = ContentCaptureEvent::Kind::Flush;
        m_Events.push_back(flush);
    }

    void ContentCaptureManager::SendAppearEvents(const AccessibilityNode& node, const Copy& old) {
        const auto& nodes = m_Delegate.Nodes();
        int i = 0;
        for (int k : node.replacedChildren) {
            auto it = nodes.find(k);
            if (it == nodes.end()) continue;
            if (!old.children.count(k)) Appeared(i, it->second);
            ++i;
        }
        for (int k : node.replacedChildren) {
            auto cur = nodes.find(k);
            auto prev = m_Previous.find(k);
            if (cur != nodes.end() && prev != m_Previous.end()) SendAppearEvents(cur->second, prev->second);
        }
    }

    void ContentCaptureManager::UpdateCopy() {
        const auto& nodes = m_Delegate.Nodes();
        auto copy = [&](const AccessibilityNode& n) {
            Copy c;
            c.config = n.config;
            for (int k : n.replacedChildren) if (nodes.count(k)) c.children.insert(k);
            return c;
        };
        m_Previous.clear();
        for (const auto& [id, n] : nodes) m_Previous[id] = copy(n);
        if (const AccessibilityNode* root = m_Delegate.FindAnyNode(kHostViewId)) m_PreviousRoot = copy(*root);
    }

    void ContentCaptureManager::CheckForChanges() {
        if (!m_Enabled) return;
        const auto& nodes = m_Delegate.Nodes();
        // sendContentCaptureDisappearEvents.
        for (const auto& [id, copy] : m_Previous)
            if (!nodes.count(id)) {
                ContentCaptureEvent e;
                e.kind = ContentCaptureEvent::Kind::Disappear;
                e.id = id;
                e.timestamp = Now();
                m_Buffered.push_back(e);
            }
        if (const AccessibilityNode* root = m_Delegate.FindAnyNode(kHostViewId)) SendAppearEvents(*root, m_PreviousRoot);
        // checkForContentCapturePropertyChanges: the first text, sent right away.
        for (const auto& [id, n] : nodes) {
            if (n.config.text.empty()) continue;
            auto prev = m_Previous.find(id);
            const std::string now = n.config.text.front();
            if (prev != m_Previous.end()) {
                const auto& old = prev->second.config.text;
                if (!old.empty() && old.front() == now) continue;
            }
            ContentCaptureEvent e;
            e.kind = ContentCaptureEvent::Kind::TextChanged;
            e.id = id;
            e.text = now;
            m_Events.push_back(std::move(e));
        }
        UpdateCopy();
    }

    void ContentCaptureManager::OnShowTranslation() {
        m_ShowTranslated = true;
        for (const auto& [id, n] : m_Delegate.Nodes())
            if (n.config.isShowingTextSubstitution == std::optional(false) && n.config.showTextSubstitution) n.config.showTextSubstitution(true);
    }

    void ContentCaptureManager::OnHideTranslation() {
        m_ShowTranslated = false;
        for (const auto& [id, n] : m_Delegate.Nodes())
            if (n.config.isShowingTextSubstitution == std::optional(true) && n.config.showTextSubstitution) n.config.showTextSubstitution(false);
    }

    void ContentCaptureManager::OnClearTranslation() {
        m_ShowTranslated = false;
        for (const auto& [id, n] : m_Delegate.Nodes())
            if (n.config.isShowingTextSubstitution && n.config.clearTextSubstitution) n.config.clearTextSubstitution();
    }

    std::vector<std::pair<int, std::string>> ContentCaptureManager::TranslationRequests(const std::vector<int>& ids) {
        std::vector<std::pair<int, std::string>> out;
        const auto& nodes = m_Delegate.Nodes();
        for (int id : ids) {
            auto it = nodes.find(id);
            if (it == nodes.end() || it->second.config.text.empty()) continue;
            out.emplace_back(id, Join(it->second.config.text, "\n"));
        }
        return out;
    }

    void ContentCaptureManager::TranslationResponses(const std::vector<std::pair<int, std::string>>& responses) {
        for (const auto& [id, text] : responses) {
            const auto& nodes = m_Delegate.Nodes();
            auto it = nodes.find(id);
            if (it != nodes.end() && it->second.config.setTextSubstitution) it->second.config.setTextSubstitution(text);
        }
    }

}
