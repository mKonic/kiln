#pragma once

// Semantics (spec 01 req. 1-4): what a node means, independent of how it draws. Modifier nodes
// contribute to a SemanticsConfiguration; the Owner builds a tree from layout nodes, merging the
// descendants of `mergeDescendants` nodes (clickables, list items) into them. The host turns it
// into AccessibilityNodeInfo (spec 01 req. 5).

#include "kiln/ui/Geometry.h"
#include "kiln/ui/Credentials.h"
#include "kiln/ui/Modifier.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln::text { class TextLayoutResult; }
namespace kiln::gfx { class Shape; }

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

    // ProgressBarRangeInfo: `current` within [start, end]; steps > 0 for a discrete range.
    struct ProgressBarRangeInfo {
        float current = 0, start = 0, end = 1;
        int steps = 0;
        bool indeterminate = false;   // ProgressBarRangeInfo.Indeterminate
        bool operator==(const ProgressBarRangeInfo&) const = default;
        static ProgressBarRangeInfo Indeterminate() { ProgressBarRangeInfo r; r.indeterminate = true; return r; }
    };

    enum class LiveRegionMode : std::uint8_t { Polite, Assertive };

    struct CustomAccessibilityAction {
        std::string label;
        std::function<bool()> action;
    };

    // FillableData (AndroidFillableData over AutofillValue): what autofill fills or reads.
    struct FillableData {
        enum class Kind : std::uint8_t { Text, Toggle, List, Date };
        Kind kind = Kind::Text;
        std::string text;            // UTF-8
        bool toggle = false;
        int listIndex = 0;
        std::int64_t dateMillis = 0;
        bool operator==(const FillableData&) const = default;
        // createFromText: trimmed to 5000 UTF-16 units without splitting a surrogate pair.
        static FillableData FromText(std::u16string_view text);
    };
    // ContentDataType (View.AUTOFILL_TYPE_*).
    namespace autofill {
        inline constexpr int kTypeNone = 0, kTypeText = 1, kTypeToggle = 2, kTypeList = 3, kTypeDate = 4;
    }

    // SemanticsConfiguration: the properties and actions of SemanticsProperties /
    // SemanticsActions. Unset optionals and empty functions are absent keys; Unit keys are bools.
    // SemanticsPropertyKey: an app's own semantics property (for tests or custom services).
    // Merging into a parent goes through mergePolicy (parent value, child value) -> value; by
    // default the parent's value stays, else the child's.
    template<typename T>
    struct SemanticsPropertyKey {
        std::string name;
        std::function<std::optional<T>(const std::optional<T>& parent, const T& child)> mergePolicy;
    };

    // InputTextSuggestionState: whether the text was committed by the IME (not set in code or by
    // an accessibility service), and whether a transliteration suggestion is picked to replace it.
    struct InputTextSuggestionState {
        bool isCommittedByInputMethodEditor = false;
        bool isTransliterationSuggestionSelected = false;
        bool operator==(const InputTextSuggestionState&) const = default;
    };

    struct SemanticsConfiguration {
        // Custom properties (SemanticsPropertyKey), by key identity.
        struct CustomValue {
            std::any value;
            std::function<std::optional<std::any>(const std::any* parent, const std::any& child)> merge;
        };
        std::map<const void*, CustomValue> custom;
        template<typename T>
        void Set(const SemanticsPropertyKey<T>& key, T value) {
            CustomValue v;
            v.value = std::move(value);
            if (key.mergePolicy) {
                auto policy = key.mergePolicy;
                v.merge = [policy](const std::any* parent, const std::any& child) -> std::optional<std::any> {
                    auto r = policy(parent ? std::optional<T>(std::any_cast<const T&>(*parent)) : std::nullopt, std::any_cast<const T&>(child));
                    return r ? std::optional<std::any>(std::move(*r)) : std::nullopt;
                };
            }
            custom[&key] = std::move(v);
        }
        // getOrNull / contains.
        template<typename T>
        const T* GetOrNull(const SemanticsPropertyKey<T>& key) const {
            auto it = custom.find(&key);
            return it == custom.end() ? nullptr : std::any_cast<T>(&it->second.value);
        }
        template<typename T>
        bool Contains(const SemanticsPropertyKey<T>& key) const { return custom.count(&key) != 0; }
        // IndexForKey (lazy layouts): the item index of a key, -1 when not there.
        std::function<int(const rt::Key&)> indexForKey;

        bool mergeDescendants = false;   // isMergingSemanticsOfDescendants
        bool clearAndSet = false;        // isClearingSemantics
        bool isTraversalGroup = false;
        std::vector<std::string> contentDescription;   // merged by concatenation
        std::vector<std::string> text;
        std::vector<TextSpanSemantics> textSpans;
        std::optional<std::string> editableText;
        std::optional<std::string> stateDescription;
        std::optional<std::string> paneTitle;
        std::optional<std::string> testTag;
        std::optional<std::string> hintText;
        std::optional<std::string> error;
        std::optional<std::string> textSubstitution;
        std::optional<bool> isShowingTextSubstitution;
        Role role = Role::None;
        bool heading = false;
        bool disabled = false;
        bool password = false;
        std::optional<bool> isPasswordObfuscated;
        bool textEntryKey = false;
        bool isDialog = false, isPopup = false;
        bool hideFromAccessibility = false;
        bool invisibleToUser = false;     // deprecated in Compose; read like hideFromAccessibility
        std::optional<bool> isSensitiveData;
        std::optional<bool> isEditable;
        std::optional<int> maxTextLength;
        std::optional<int> imeAction;                  // EditorInfo.IME_ACTION_*
        std::optional<bool> selected;
        std::optional<int> toggleableState;            // 0 off, 1 on, 2 indeterminate
        std::optional<bool> focused;                  // presence = focusable
        std::optional<ProgressBarRangeInfo> progressBarRangeInfo;
        std::optional<LiveRegionMode> liveRegion;
        std::optional<float> traversalIndex;          // never merged
        std::optional<bool> testTagsAsResourceId;      // SemanticsPropertiesAndroid
        std::optional<std::string> accessibilityClassName;
        std::shared_ptr<const gfx::Shape> shape;       // the node's shape (extra data), never merged
        bool selectableGroup = false;
        std::optional<std::pair<int, int>> textSelection;
        // textCompositionRange: the IME's composing region, when there is one.
        std::optional<std::pair<int, int>> textCompositionRange;
        std::optional<InputTextSuggestionState> inputTextSuggestionState;
        // Autofill: the content's hints (ContentType, View.AUTOFILL_HINT_*; `+` unions them), its
        // data type, the value it holds and the untransformed input text.
        std::optional<std::vector<std::string>> contentType;
        std::optional<int> contentDataType;
        std::optional<FillableData> fillableData;
        std::optional<std::string> inputText;
        // CredentialRequest (API 34+): a Credential Manager request for this node; merging keeps
        // the parent's.
        std::shared_ptr<const CredentialRequestData> credentialRequest;
        // Scrolling.
        std::optional<ScrollAxisRange> horizontalScrollAxisRange, verticalScrollAxisRange;
        std::optional<std::pair<int, int>> collectionInfo;     // rows, columns; -1 = unknown (lazy)
        std::optional<CollectionItemInfo> collectionItemInfo;

        // Actions (an empty function is an absent action).
        std::function<bool()> onClick;
        std::optional<std::string> onClickLabel;
        std::function<bool()> onLongClick;
        std::optional<std::string> onLongClickLabel;
        std::function<bool(const std::string&)> setText;
        std::function<bool(const std::string&)> insertTextAtCursor;
        std::function<bool(int start, int end, bool relativeToOriginal)> setSelection;   // traversal mode
        std::function<bool()> requestFocus;
        std::function<bool(float)> setProgress;
        std::function<bool()> onImeAction;
        std::optional<std::string> onImeActionLabel;
        std::function<bool()> copyText, cutText, pasteText;
        std::function<bool()> expand, collapse, dismiss;
        std::optional<std::string> expandLabel, collapseLabel, dismissLabel;
        std::function<bool(const std::string&)> setTextSubstitution;
        std::function<bool(bool)> showTextSubstitution;
        std::function<bool()> clearTextSubstitution;
        std::vector<CustomAccessibilityAction> customActions;   // merged by concatenation
        // GetTextLayoutResult: the text's current layout (null: none yet).
        std::function<std::shared_ptr<const text::TextLayoutResult>()> getTextLayoutResult;
        std::function<bool(float x, float y)> scrollBy;        // positive: further into the content
        // ScrollByOffset (scroll capture): scrolls by the offset and returns what was consumed.
        std::function<rt::Task<Offset>(Offset)> scrollByOffset;
        std::function<bool(int index)> scrollToIndex;
        // GetScrollViewportLength: the viewport minus content padding, and the padding before and
        // after the content.
        std::function<float()> scrollViewportLength;
        std::function<std::pair<float, float>()> scrollViewportPadding;
        // Paging (pagers): scroll actions on a node that pages use these instead of scrollBy.
        std::function<bool()> pageUp, pageDown, pageLeft, pageRight;
        // OnFillData: autofill hands the node a value.
        std::function<bool(const FillableData&)> onFillData;

        bool HasContent() const {
            return !contentDescription.empty() || !text.empty() || editableText || stateDescription || onClick ||
                   focused || role != Role::None || heading || selected || toggleableState || paneTitle ||
                   scrollBy || collectionInfo || collectionItemInfo || progressBarRangeInfo || !customActions.empty() ||
                   hintText || error || liveRegion || selectableGroup || !custom.empty();
        }
        // containsImportantForAccessibility: a property other than testTag, traversal index,
        // traversal group, test-tag-as-id, shape and the sensitive / substitution flags.
        bool ContainsImportantForAccessibility() const;
        // Merge `child` into this (SemanticsConfiguration.mergeChild with each key's merge
        // policy): content descriptions, text and custom actions concatenate; role, test tag,
        // traversal index, hidden flags and shape keep the parent's; others take the parent's
        // value, else the child's.
        void Merge(const SemanticsConfiguration& child);
        // A semantics modifier further out on the same node (calculateSemanticsConfiguration
        // applies tail to head): every key it sets replaces this one's.
        void Overlay(const SemanticsConfiguration& outer);
        // Same keys and values (actions compare by presence and label, like accessibilityEquals).
        bool Equivalent(const SemanticsConfiguration& o) const;
        bool SameCustomKeys(const SemanticsConfiguration& o) const;
    };

    class SemanticsModifier {
    public:
        virtual ~SemanticsModifier() = default;
        virtual void ApplySemantics(SemanticsConfiguration& config) = 0;
        // isImportantForBounds: whether the node's bounds may come from this modifier's space.
        virtual bool IsImportantForBounds() const { return true; }
    };

    class LayoutNode;

    // A layout node's own configuration from its semantics modifiers; false without any.
    bool CalculateSemanticsConfiguration(LayoutNode& n, SemanticsConfiguration& out);

    struct SemanticsNode {
        int id = 0;                    // stable per layout node; -1 = the root (host view)
        LayoutNode* layout = nullptr;
        Rect boundsInRoot;
        SemanticsConfiguration config; // merged when mergeDescendants
        std::vector<SemanticsNode> children;
    };

    class Owner;

    // SemanticsOwner.getAllSemanticsNodes: every placed node of the tree (merged as the tree
    // merges them), flattened with each one's children left out.
    std::vector<SemanticsNode> GetAllSemanticsNodes(Owner& owner);
    // forEachUncoveredSemanticsNode: the nodes accessibility sees, each with its uncovered bounds.
    using UncoveredSemanticsNodeVisitor = std::function<void(const SemanticsNode&)>;
    void ForEachUncoveredSemanticsNode(Owner& owner, const UncoveredSemanticsNodeVisitor& visitor);

}
