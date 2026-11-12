#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Semantics.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace kiln::ui {

    bool SemanticsConfiguration::ContainsImportantForAccessibility() const {
        return !contentDescription.empty() || !text.empty() || editableText || stateDescription || paneTitle || hintText ||
               error || role != Role::None || heading || disabled || password || isPasswordObfuscated || textEntryKey ||
               isDialog || isPopup || imeAction || selected || toggleableState || focused || progressBarRangeInfo ||
               liveRegion || selectableGroup || textSelection || horizontalScrollAxisRange || verticalScrollAxisRange ||
               collectionInfo || collectionItemInfo || onClick || onLongClick || setText || insertTextAtCursor ||
               setSelection || requestFocus || setProgress || onImeAction || copyText || cutText || pasteText || expand ||
               collapse || dismiss || setTextSubstitution || showTextSubstitution || clearTextSubstitution ||
               !customActions.empty() || getTextLayoutResult || scrollBy || scrollToIndex || scrollViewportLength ||
               pageUp || pageDown || pageLeft || pageRight;
    }

    namespace {
        template <class T> void Fill(std::optional<T>& into, const std::optional<T>& from) { if (!into) into = from; }
        template <class F> void Fill(std::function<F>& into, const std::function<F>& from) { if (!into) into = from; }
        template <class T> void Take(std::optional<T>& into, const std::optional<T>& from) { if (from) into = from; }
        template <class F> void Take(std::function<F>& into, const std::function<F>& from) { if (from) into = from; }
    }

    void SemanticsConfiguration::Merge(const SemanticsConfiguration& c) {
        contentDescription.insert(contentDescription.end(), c.contentDescription.begin(), c.contentDescription.end());
        const int base = int(text.size());
        text.insert(text.end(), c.text.begin(), c.text.end());
        for (TextSpanSemantics span : c.textSpans) {
            span.textIndex += base;
            textSpans.push_back(std::move(span));
        }
        customActions.insert(customActions.end(), c.customActions.begin(), c.customActions.end());
        // Parent value, else the child's. Role, testTag, traversalIndex, the hidden flags, shape
        // and pane title never take the child's.
        Fill(editableText, c.editableText);
        Fill(stateDescription, c.stateDescription);
        Fill(hintText, c.hintText);
        Fill(error, c.error);
        Fill(textSubstitution, c.textSubstitution);
        Fill(isShowingTextSubstitution, c.isShowingTextSubstitution);
        Fill(isPasswordObfuscated, c.isPasswordObfuscated);
        Fill(isSensitiveData, c.isSensitiveData);
        Fill(isEditable, c.isEditable);
        Fill(maxTextLength, c.maxTextLength);
        Fill(imeAction, c.imeAction);
        Fill(selected, c.selected);
        Fill(toggleableState, c.toggleableState);
        Fill(focused, c.focused);
        Fill(progressBarRangeInfo, c.progressBarRangeInfo);
        Fill(liveRegion, c.liveRegion);
        Fill(testTagsAsResourceId, c.testTagsAsResourceId);
        Fill(accessibilityClassName, c.accessibilityClassName);
        Fill(textSelection, c.textSelection);
        Fill(horizontalScrollAxisRange, c.horizontalScrollAxisRange);
        Fill(verticalScrollAxisRange, c.verticalScrollAxisRange);
        Fill(collectionInfo, c.collectionInfo);
        Fill(collectionItemInfo, c.collectionItemInfo);
        Fill(contentType, c.contentType);
        Fill(contentDataType, c.contentDataType);
        Fill(fillableData, c.fillableData);
        Fill(inputText, c.inputText);
        Fill(onFillData, c.onFillData);
        heading = heading || c.heading;
        disabled = disabled || c.disabled;
        password = password || c.password;
        textEntryKey = textEntryKey || c.textEntryKey;
        selectableGroup = selectableGroup || c.selectableGroup;
        isTraversalGroup = isTraversalGroup || c.isTraversalGroup;
        // Actions: the parent's label and action, else the child's.
        Fill(onClick, c.onClick); Fill(onClickLabel, c.onClickLabel);
        Fill(onLongClick, c.onLongClick); Fill(onLongClickLabel, c.onLongClickLabel);
        Fill(setText, c.setText); Fill(insertTextAtCursor, c.insertTextAtCursor); Fill(setSelection, c.setSelection);
        Fill(requestFocus, c.requestFocus); Fill(setProgress, c.setProgress);
        Fill(onImeAction, c.onImeAction); Fill(onImeActionLabel, c.onImeActionLabel);
        Fill(copyText, c.copyText); Fill(cutText, c.cutText); Fill(pasteText, c.pasteText);
        Fill(expand, c.expand); Fill(expandLabel, c.expandLabel);
        Fill(collapse, c.collapse); Fill(collapseLabel, c.collapseLabel);
        Fill(dismiss, c.dismiss); Fill(dismissLabel, c.dismissLabel);
        Fill(setTextSubstitution, c.setTextSubstitution); Fill(showTextSubstitution, c.showTextSubstitution);
        Fill(clearTextSubstitution, c.clearTextSubstitution);
        Fill(getTextLayoutResult, c.getTextLayoutResult);
        Fill(scrollBy, c.scrollBy); Fill(scrollByOffset, c.scrollByOffset); Fill(scrollToIndex, c.scrollToIndex);
        Fill(scrollViewportLength, c.scrollViewportLength); Fill(scrollViewportPadding, c.scrollViewportPadding);
        Fill(pageUp, c.pageUp); Fill(pageDown, c.pageDown); Fill(pageLeft, c.pageLeft); Fill(pageRight, c.pageRight);
        Fill(indexForKey, c.indexForKey);
        for (const auto& [key, v] : c.custom) {
            auto it = custom.find(key);
            if (v.merge) {
                auto merged = v.merge(it == custom.end() ? nullptr : &it->second.value, v.value);
                if (merged) custom[key] = { std::move(*merged), v.merge };
                else if (it != custom.end()) custom.erase(it);
            } else if (it == custom.end()) {
                custom[key] = v;
            }
        }
    }

    void SemanticsConfiguration::Overlay(const SemanticsConfiguration& o) {
        mergeDescendants = mergeDescendants || o.mergeDescendants;
        isTraversalGroup = isTraversalGroup || o.isTraversalGroup;
        if (!o.contentDescription.empty()) contentDescription = o.contentDescription;
        if (!o.text.empty()) { text = o.text; textSpans = o.textSpans; }
        else if (!o.textSpans.empty()) textSpans = o.textSpans;
        if (!o.customActions.empty()) customActions = o.customActions;
        Take(editableText, o.editableText); Take(stateDescription, o.stateDescription); Take(paneTitle, o.paneTitle);
        Take(testTag, o.testTag); Take(hintText, o.hintText); Take(error, o.error);
        Take(textSubstitution, o.textSubstitution); Take(isShowingTextSubstitution, o.isShowingTextSubstitution);
        if (o.role != Role::None) role = o.role;
        heading = heading || o.heading;
        disabled = disabled || o.disabled;
        password = password || o.password;
        textEntryKey = textEntryKey || o.textEntryKey;
        isDialog = isDialog || o.isDialog;
        isPopup = isPopup || o.isPopup;
        hideFromAccessibility = hideFromAccessibility || o.hideFromAccessibility;
        invisibleToUser = invisibleToUser || o.invisibleToUser;
        selectableGroup = selectableGroup || o.selectableGroup;
        Take(isPasswordObfuscated, o.isPasswordObfuscated); Take(isSensitiveData, o.isSensitiveData);
        Take(isEditable, o.isEditable); Take(maxTextLength, o.maxTextLength); Take(imeAction, o.imeAction);
        Take(selected, o.selected); Take(toggleableState, o.toggleableState); Take(focused, o.focused);
        Take(progressBarRangeInfo, o.progressBarRangeInfo); Take(liveRegion, o.liveRegion);
        Take(traversalIndex, o.traversalIndex); Take(testTagsAsResourceId, o.testTagsAsResourceId);
        Take(accessibilityClassName, o.accessibilityClassName);
        if (o.shape) shape = o.shape;
        Take(textSelection, o.textSelection);
        Take(horizontalScrollAxisRange, o.horizontalScrollAxisRange); Take(verticalScrollAxisRange, o.verticalScrollAxisRange);
        Take(collectionInfo, o.collectionInfo); Take(collectionItemInfo, o.collectionItemInfo);
        Take(contentType, o.contentType); Take(contentDataType, o.contentDataType);
        Take(fillableData, o.fillableData); Take(inputText, o.inputText); Take(onFillData, o.onFillData);
        if (o.credentialRequest) credentialRequest = o.credentialRequest;
        Take(onClick, o.onClick); Take(onClickLabel, o.onClickLabel);
        Take(onLongClick, o.onLongClick); Take(onLongClickLabel, o.onLongClickLabel);
        Take(setText, o.setText); Take(insertTextAtCursor, o.insertTextAtCursor); Take(setSelection, o.setSelection);
        Take(requestFocus, o.requestFocus); Take(setProgress, o.setProgress);
        Take(onImeAction, o.onImeAction); Take(onImeActionLabel, o.onImeActionLabel);
        Take(copyText, o.copyText); Take(cutText, o.cutText); Take(pasteText, o.pasteText);
        Take(expand, o.expand); Take(expandLabel, o.expandLabel);
        Take(collapse, o.collapse); Take(collapseLabel, o.collapseLabel);
        Take(dismiss, o.dismiss); Take(dismissLabel, o.dismissLabel);
        Take(setTextSubstitution, o.setTextSubstitution); Take(showTextSubstitution, o.showTextSubstitution);
        Take(clearTextSubstitution, o.clearTextSubstitution);
        Take(getTextLayoutResult, o.getTextLayoutResult);
        Take(scrollBy, o.scrollBy); Take(scrollByOffset, o.scrollByOffset); Take(scrollToIndex, o.scrollToIndex);
        Take(scrollViewportLength, o.scrollViewportLength); Take(scrollViewportPadding, o.scrollViewportPadding);
        Take(pageUp, o.pageUp); Take(pageDown, o.pageDown); Take(pageLeft, o.pageLeft); Take(pageRight, o.pageRight);
        Take(indexForKey, o.indexForKey);
        for (const auto& [key, v] : o.custom) custom[key] = v;
    }

    bool SemanticsConfiguration::Equivalent(const SemanticsConfiguration& o) const {
        auto spansEq = [](const std::vector<TextSpanSemantics>& a, const std::vector<TextSpanSemantics>& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (a[i].kind != b[i].kind || a[i].textIndex != b[i].textIndex || a[i].start != b[i].start ||
                    a[i].end != b[i].end || a[i].value != b[i].value || bool(a[i].onClick) != bool(b[i].onClick)) return false;
            return true;
        };
        auto actionsEq = [](const std::vector<CustomAccessibilityAction>& a, const std::vector<CustomAccessibilityAction>& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) if (a[i].label != b[i].label || bool(a[i].action) != bool(b[i].action)) return false;
            return true;
        };
        auto rangeEq = [](const std::optional<ScrollAxisRange>& a, const std::optional<ScrollAxisRange>& b) {
            return bool(a) == bool(b) && (!a || a->reverseScrolling == b->reverseScrolling);
        };
        return mergeDescendants == o.mergeDescendants && clearAndSet == o.clearAndSet && isTraversalGroup == o.isTraversalGroup &&
               contentDescription == o.contentDescription && text == o.text && spansEq(textSpans, o.textSpans) &&
               editableText == o.editableText && stateDescription == o.stateDescription && paneTitle == o.paneTitle &&
               testTag == o.testTag && hintText == o.hintText && error == o.error && textSubstitution == o.textSubstitution &&
               isShowingTextSubstitution == o.isShowingTextSubstitution && role == o.role && heading == o.heading &&
               disabled == o.disabled && password == o.password && isPasswordObfuscated == o.isPasswordObfuscated &&
               textEntryKey == o.textEntryKey && isDialog == o.isDialog && isPopup == o.isPopup &&
               hideFromAccessibility == o.hideFromAccessibility && invisibleToUser == o.invisibleToUser &&
               isSensitiveData == o.isSensitiveData && isEditable == o.isEditable && maxTextLength == o.maxTextLength &&
               imeAction == o.imeAction && selected == o.selected && toggleableState == o.toggleableState &&
               focused == o.focused && progressBarRangeInfo == o.progressBarRangeInfo && liveRegion == o.liveRegion &&
               traversalIndex == o.traversalIndex && testTagsAsResourceId == o.testTagsAsResourceId &&
               accessibilityClassName == o.accessibilityClassName && shape == o.shape && selectableGroup == o.selectableGroup &&
               textSelection == o.textSelection && rangeEq(horizontalScrollAxisRange, o.horizontalScrollAxisRange) &&
               rangeEq(verticalScrollAxisRange, o.verticalScrollAxisRange) && collectionInfo == o.collectionInfo &&
               collectionItemInfo == o.collectionItemInfo && bool(onClick) == bool(o.onClick) && onClickLabel == o.onClickLabel &&
               bool(onLongClick) == bool(o.onLongClick) && onLongClickLabel == o.onLongClickLabel &&
               bool(setText) == bool(o.setText) && bool(insertTextAtCursor) == bool(o.insertTextAtCursor) &&
               bool(setSelection) == bool(o.setSelection) && bool(requestFocus) == bool(o.requestFocus) &&
               bool(setProgress) == bool(o.setProgress) && bool(onImeAction) == bool(o.onImeAction) &&
               onImeActionLabel == o.onImeActionLabel && bool(copyText) == bool(o.copyText) && bool(cutText) == bool(o.cutText) &&
               bool(pasteText) == bool(o.pasteText) && bool(expand) == bool(o.expand) && expandLabel == o.expandLabel &&
               bool(collapse) == bool(o.collapse) && collapseLabel == o.collapseLabel && bool(dismiss) == bool(o.dismiss) &&
               dismissLabel == o.dismissLabel && bool(setTextSubstitution) == bool(o.setTextSubstitution) &&
               bool(showTextSubstitution) == bool(o.showTextSubstitution) && bool(clearTextSubstitution) == bool(o.clearTextSubstitution) &&
               actionsEq(customActions, o.customActions) && bool(getTextLayoutResult) == bool(o.getTextLayoutResult) &&
               bool(scrollBy) == bool(o.scrollBy) && bool(scrollByOffset) == bool(o.scrollByOffset) && bool(scrollToIndex) == bool(o.scrollToIndex) &&
               bool(scrollViewportLength) == bool(o.scrollViewportLength) && bool(scrollViewportPadding) == bool(o.scrollViewportPadding) &&
               bool(pageUp) == bool(o.pageUp) && bool(pageDown) == bool(o.pageDown) && bool(pageLeft) == bool(o.pageLeft) &&
               bool(pageRight) == bool(o.pageRight) && contentType == o.contentType && contentDataType == o.contentDataType &&
               fillableData == o.fillableData && inputText == o.inputText && bool(onFillData) == bool(o.onFillData) &&
               credentialRequest == o.credentialRequest && bool(indexForKey) == bool(o.indexForKey) && SameCustomKeys(o);
    }

    bool SemanticsConfiguration::SameCustomKeys(const SemanticsConfiguration& o) const {
        // Values are opaque here: the same keys count as the same.
        if (custom.size() != o.custom.size()) return false;
        for (const auto& [k, v] : custom) if (!o.custom.count(k)) return false;
        return true;
    }

    FillableData FillableData::FromText(std::u16string_view text) {
        constexpr std::size_t kMax = 5000;   // MAX_AUTOFILL_TEXT_LENGTH
        std::size_t n = text.size();
        if (n > kMax) {
            n = kMax;
            // trimToSafeLength: do not break a surrogate pair.
            if (text[kMax - 1] >= 0xD800 && text[kMax - 1] < 0xDC00 && text[kMax] >= 0xDC00 && text[kMax] < 0xE000) n = kMax - 1;
        }
        FillableData d;
        d.kind = Kind::Text;
        std::string out;
        for (std::size_t i = 0; i < n; ++i) {
            char32_t c = text[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
                ++i;
            }
            if (c < 0x80) out += char(c);
            else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
            else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
        }
        d.text = std::move(out);
        return d;
    }

    bool CalculateSemanticsConfiguration(LayoutNode& n, SemanticsConfiguration& out) {
        // Tail to head: modifiers further out replace what inner ones set, and a clearing one
        // drops what the inner ones set (LayoutNode.calculateSemanticsConfiguration).
        const auto mods = n.ModifierNodes();
        bool any = false;
        for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
            if (!n.IsModifierPlaced(*it)) continue;
            auto* s = const_cast<ModifierNode*>(*it)->AsSemantics();
            if (!s) continue;
            any = true;
            SemanticsConfiguration mine;
            s->ApplySemantics(mine);
            if (mine.clearAndSet) { out = SemanticsConfiguration{}; out.clearAndSet = true; }
            out.Overlay(mine);
        }
        return any;
    }

    namespace {
        // A node's own configuration from its modifiers (leftmost wins for single values).
        bool OwnConfig(LayoutNode& n, SemanticsConfiguration& out) {
            return CalculateSemanticsConfiguration(n, out) && (out.HasContent() || out.mergeDescendants || out.isTraversalGroup);
        }
        // findCoordinatorToGetBounds: the space of the outermost semantics modifier that is
        // important for bounds (a merging one first when the node merges), else the node's inner
        // content (padding before the semantics, such as a clickable's, is outside the bounds).
        Rect Bounds(LayoutNode& n, bool merging) {
            const ModifierNode* pick = nullptr;
            for (const ModifierNode* m : n.ModifierNodes()) {
                if (!n.IsModifierPlaced(m)) continue;
                auto* s = const_cast<ModifierNode*>(m)->AsSemantics();
                if (!s || !s->IsImportantForBounds()) continue;
                if (!merging) { pick = m; break; }
                SemanticsConfiguration own;
                s->ApplySemantics(own);
                if (own.mergeDescendants) { pick = m; break; }
                if (!pick) pick = m;
            }
            return pick ? n.RectInRoot(std::nullopt, pick) : n.InnerRectInRoot();
        }
        // Collects descendants' content into `into` (stopping at nodes that merge their own).
        void MergeDescendants(LayoutNode& n, SemanticsConfiguration& into) {
            for (LayoutNode* c : n.ZSortedChildren()) {
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
        // `clip` narrowed by the node's clipping modifiers, each in its own space (an offset
        // before the clip moves it, so the node's outer bounds are not the clip).
        Rect ClipOf(LayoutNode& n, Rect clip) {
            for (const ModifierNode* m : n.ModifierNodes())
                if (m->ClipsChildren() && n.IsModifierPlaced(m)) clip = Intersect(clip, n.RectInRoot(std::nullopt, m));
            return clip;
        }
        void Build(LayoutNode& n, const Rect& clip, std::vector<SemanticsNode>& out);
        // findOneLayerOfMergingSemanticsNodes: a merging node's children are the merging nodes
        // below it (a clickable row's button), not stopping at nodes that only add content.
        void BuildMergingChildren(LayoutNode& n, const Rect& clip, std::vector<SemanticsNode>& out) {
            for (LayoutNode* c : n.ZSortedChildren()) {
                if (!c->IsPlaced()) continue;
                SemanticsConfiguration cc;
                const bool has = OwnConfig(*c, cc);
                if (has && cc.mergeDescendants) Build(*c, clip, out);
                else if (!cc.clearAndSet) BuildMergingChildren(*c, ClipOf(*c, clip), out);
            }
        }
        // `clip`: the ancestors' clip in root coordinates; bounds are clipped by it
        // (SemanticsNode.boundsInRoot uses the clipped coordinator bounds).
        void Build(LayoutNode& n, const Rect& clip, std::vector<SemanticsNode>& out) {
            if (!n.IsPlaced() && n.Parent()) return;
            const Rect inner = ClipOf(n, clip);
            SemanticsConfiguration cfg;
            if (OwnConfig(n, cfg)) {
                const Rect bounds = Bounds(n, cfg.mergeDescendants);
                SemanticsNode node;
                node.id = n.SemanticsId();
                node.layout = &n;
                node.boundsInRoot = Intersect(bounds, clip);
                if (cfg.mergeDescendants && !cfg.clearAndSet) MergeDescendants(n, cfg);
                node.config = cfg;
                if (!cfg.clearAndSet) {
                    if (cfg.mergeDescendants) BuildMergingChildren(n, inner, node.children);
                    else for (LayoutNode* c : n.ZSortedChildren()) Build(*c, inner, node.children);
                }
                out.push_back(std::move(node));
                return;
            }
            // clearAndSetSemantics {} with nothing set: no node, and nothing from below.
            if (cfg.clearAndSet) return;
            for (LayoutNode* c : n.ZSortedChildren()) Build(*c, inner, out);
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

    SemanticsNode Owner::BuildSemanticsTree(bool uncoveredOnly) {
        SemanticsNode root;
        root.id = -1;
        root.layout = m_Root.get();
        root.boundsInRoot = { 0, 0, float(m_Window.width), float(m_Window.height) };
        for (LayoutNode* c : m_Root->Children()) Build(*c, root.boundsInRoot, root.children);
        if (!uncoveredOnly) return root;
        Region unaccounted{ { Round(root.boundsInRoot) } };
        KeepUncovered(root, unaccounted);
        return root;
    }


    namespace {
        void Flatten(const SemanticsNode& n, std::vector<SemanticsNode>& out) {
            SemanticsNode copy;
            copy.id = n.id;
            copy.layout = n.layout;
            copy.boundsInRoot = n.boundsInRoot;
            copy.config = n.config;
            out.push_back(std::move(copy));
            for (const auto& c : n.children) Flatten(c, out);
        }
        void Visit(const SemanticsNode& n, const UncoveredSemanticsNodeVisitor& v) {
            v(n);
            for (const auto& c : n.children) Visit(c, v);
        }
    }

    std::vector<SemanticsNode> GetAllSemanticsNodes(Owner& owner) {
        std::vector<SemanticsNode> out;
        Flatten(owner.BuildSemanticsTree(false), out);
        return out;
    }

    void ForEachUncoveredSemanticsNode(Owner& owner, const UncoveredSemanticsNodeVisitor& visitor) {
        Visit(owner.BuildSemanticsTree(true), visitor);
    }

}
