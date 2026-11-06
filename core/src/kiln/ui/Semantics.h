#pragma once

// Semantics (spec 01 req. 1-4): what a node means, independent of how it draws. Modifier nodes
// contribute to a SemanticsConfiguration; the Owner builds a tree from layout nodes, merging the
// descendants of `mergeDescendants` nodes (clickables, list items) into them. The host turns it
// into AccessibilityNodeInfo (spec 01 req. 5).

#include "kiln/ui/Geometry.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln::ui {

    enum class Role : std::uint8_t { None, Button, Checkbox, Switch, RadioButton, Tab, Image, DropdownList, ValuePicker, Carousel };

    // ScrollAxisRange: the scroll position and its maximum, read when asked (they change with
    // every scroll). reverseScrolling: a higher value means further towards the start.
    struct ScrollAxisRange {
        std::function<float()> value, maxValue;
        bool reverseScrolling = false;
    };

    // CollectionItemInfo: an item's place in its collection (row / column index and span).
    struct CollectionItemInfo {
        int rowIndex = 0, rowSpan = 1, columnIndex = 0, columnSpan = 1;
        bool operator==(const CollectionItemInfo&) const = default;
    };

    // A span on a text entry for accessibility (toAccessibilitySpannableString): a URL, a
    // clickable link, or text to speak instead (TtsAnnotation). Offsets are UTF-16 in that entry.
    struct TextSpanSemantics {
        enum class Kind : std::uint8_t { Url, Clickable, Tts };
        Kind kind = Kind::Url;
        int textIndex = 0;
        int start = 0, end = 0;
        std::string value;               // the URL, the verbatim text
        std::function<void()> onClick;   // Clickable
    };

    struct SemanticsConfiguration {
        bool mergeDescendants = false;
        bool clearAndSet = false;
        bool isTraversalGroup = false;
        std::vector<std::string> contentDescription;   // merged by concatenation
        std::vector<std::string> text;
        std::vector<TextSpanSemantics> textSpans;
        std::optional<std::string> editableText;
        std::optional<std::string> stateDescription;
        std::optional<std::string> paneTitle;
        std::optional<std::string> testTag;
        Role role = Role::None;
        bool heading = false;
        bool disabled = false;
        bool password = false;
        std::optional<bool> selected;
        std::optional<int> toggleableState;            // 0 off, 1 on, 2 indeterminate
        std::optional<bool> focused;                  // presence = focusable
        std::function<bool()> onClick;
        std::optional<std::string> onClickLabel;
        std::optional<std::string> onLongClickLabel;
        bool selectableGroup = false;
        std::function<bool()> onLongClick;
        std::function<bool(const std::string&)> setText;
        std::function<bool()> requestFocus;
        std::optional<std::pair<int, int>> textSelection;
        // Scrolling (not merged into ancestors, like Compose's unmerged config).
        std::optional<ScrollAxisRange> horizontalScrollAxisRange, verticalScrollAxisRange;
        std::function<bool(float x, float y)> scrollBy;        // positive: further into the content
        std::function<bool(int index)> scrollToIndex;
        std::function<float()> scrollViewportLength;          // GetScrollViewportLength (minus padding)
        std::optional<std::pair<int, int>> collectionInfo;     // rows, columns; -1 = unknown (lazy)
        std::optional<CollectionItemInfo> collectionItemInfo;
        // Paging (pagers): scroll actions on a node that pages use these instead of scrollBy.
        std::function<bool()> pageUp, pageDown, pageLeft, pageRight;

        bool HasContent() const {
            return !contentDescription.empty() || !text.empty() || editableText || stateDescription || onClick ||
                   focused || role != Role::None || heading || selected || toggleableState || paneTitle ||
                   scrollBy || collectionInfo || collectionItemInfo;
        }
        // Merge `child` into this (SemanticsConfiguration.mergeChild: lists concatenate, a
        // parent's own value wins for single values).
        void Merge(const SemanticsConfiguration& child);
    };

    class SemanticsModifier {
    public:
        virtual ~SemanticsModifier() = default;
        virtual void ApplySemantics(SemanticsConfiguration& config) = 0;
    };

    class LayoutNode;

    struct SemanticsNode {
        int id = 0;                    // stable per layout node; -1 = the root (host view)
        LayoutNode* layout = nullptr;
        Rect boundsInRoot;
        SemanticsConfiguration config; // merged when mergeDescendants
        std::vector<SemanticsNode> children;
    };

    class Owner;

    // TYPE_VIEW_SCROLLED (scheduleScrollEventIfNeeded): scroll containers whose axis range value
    // moved since the last Collect, with their ranges and the pixels they scrolled themselves.
    // A container seen for the first time only starts being observed.
    struct ScrollEvent {
        int id = 0;
        std::optional<std::pair<int, int>> x, y;   // value, max
        int deltaX = 0, deltaY = 0;
    };
    class ScrollEventTracker {
    public:
        std::vector<ScrollEvent> Collect(const SemanticsNode& root, Owner& owner);
    private:
        struct Old { std::optional<float> x, y; };
        std::unordered_map<int, Old> m_Old;
    };

    // What the host reports as collection (item) info (CollectionInfo.android.kt): the node's
    // own, else from a selectableGroup of selectable children — the group is a collection of
    // them, stacked horizontally or vertically by their centers, and each is an item at its
    // place among them (not inside a lazy collection, which has its own).
    std::optional<std::pair<int, int>> ResolveCollectionInfo(const SemanticsNode& node);
    std::optional<CollectionItemInfo> ResolveCollectionItemInfo(const SemanticsNode& node, const SemanticsNode* parent);

}
