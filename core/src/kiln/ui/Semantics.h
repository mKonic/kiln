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
#include <vector>

namespace kiln::ui {

    enum class Role : std::uint8_t { None, Button, Checkbox, Switch, RadioButton, Tab, Image, DropdownList, ValuePicker, Carousel };

    struct SemanticsConfiguration {
        bool mergeDescendants = false;
        bool clearAndSet = false;
        bool isTraversalGroup = false;
        std::vector<std::string> contentDescription;   // merged by concatenation
        std::vector<std::string> text;
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
        std::function<bool()> onLongClick;
        std::function<bool(const std::string&)> setText;
        std::function<bool()> requestFocus;
        std::optional<std::pair<int, int>> textSelection;

        bool HasContent() const {
            return !contentDescription.empty() || !text.empty() || editableText || stateDescription || onClick ||
                   focused || role != Role::None || heading || selected || toggleableState || paneTitle;
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

}
