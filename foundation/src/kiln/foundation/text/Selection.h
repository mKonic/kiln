#pragma once

// Text selection across texts (foundation text/selection): SelectionContainer makes the BasicTexts
// inside it selectable together, DisableSelection opts content out. Long press selects a word and
// drags extend it; double / triple taps select a word / paragraph; mouse clicks select by
// characters, words (double) or paragraphs (triple) and shift-click extends. Handles drag either
// end; the floating toolbar offers Copy and Select all; Ctrl+C copies. Selected texts copy joined
// by '\n'.

#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Focus.h"
#include "kiln/runtime/State.h"
#include "kiln/text/Paragraph.h"
#include "kiln/ui/Locals.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace kiln {
    class TextBackend;
}

namespace kiln::foundation {

    // TextSelectionColors: the handles and the highlight behind selected text.
    struct TextSelectionColors {
        Color handleColor;
        Color backgroundColor;
        bool operator==(const TextSelectionColors&) const = default;
    };
    // DefaultTextSelectionColors (Android): 0xFF4286F4, the highlight at 40 % alpha.
    inline const TextSelectionColors kDefaultTextSelectionColors{ Color(0xFF4286F4u), Color(0x664286F4u) };
    inline rt::Local<TextSelectionColors> LocalTextSelectionColors{ kDefaultTextSelectionColors };

    namespace selection {

        using text::AnnotatedString;
        using text::ResolvedTextDirection;
        using text::TextLayoutResult;
        using Mat3 = ui::LayoutNode::Mat3;

        // Selection.AnchorInfo / Selection: each end in one selectable, with the run direction
        // there; handlesCrossed when the start comes after the end.
        struct AnchorInfo {
            ResolvedTextDirection direction = ResolvedTextDirection::Ltr;
            int offset = 0;
            std::int64_t selectableId = 0;
            bool operator==(const AnchorInfo&) const = default;
        };
        struct Selection {
            AnchorInfo start, end;
            bool handlesCrossed = false;
            bool operator==(const Selection&) const = default;
            Selection Merge(const std::optional<Selection>& other) const;
            int Min() const { return std::min(start.offset, end.offset); }
            int Max() const { return std::max(start.offset, end.offset); }
        };
        using SubSelections = std::map<std::int64_t, Selection>;
        inline constexpr std::int64_t kInvalidSelectableId = 0;

        // LayoutCoordinates of a modifier's space (the node's outer bounds when `at` is null).
        struct Coordinates {
            const ui::LayoutNode* node = nullptr;
            const ui::ModifierNode* at = nullptr;
            std::weak_ptr<const char> alive;   // the node's (or the modifier's)
            bool IsAttached() const;
            Mat3 ToRoot() const;
            ui::Size GetSize() const;
        };
        // target.localPositionOf(source, p): `p` in `source` mapped into `target`.
        Offset LocalPositionOf(const Coordinates& target, const Coordinates& source, Offset p);
        Offset PositionInRoot(const Coordinates& c, Offset p = {});
        // visibleBounds: the part of the space not clipped by its ancestors or the window, local.
        Rect VisibleBounds(const Coordinates& c);

        // getTextDirectionForOffset: the paragraph's direction on an empty line, else the run's.
        ResolvedTextDirection TextDirectionForOffset(const TextLayoutResult& layout, int offset);

        enum class CrossStatus : std::uint8_t { Crossed, NotCrossed, Collapsed };
        enum class Direction : std::uint8_t { Before, On, After };
        enum class Adjustment : std::uint8_t { None, Character, Word, Paragraph, CharacterWithWordAccelerate };
        enum class Handle : std::uint8_t { SelectionStart, SelectionEnd };

        // SelectableInfo: one selectable's raw handle offsets in a SelectionLayout.
        struct SelectableInfo {
            std::int64_t selectableId = 0;
            int slot = 0;
            int rawStartHandleOffset = 0, rawEndHandleOffset = 0, rawPreviousHandleOffset = -1;
            std::shared_ptr<const TextLayoutResult> layout;
            TextBackend* backend = nullptr;
            int TextLength() const { return layout->LayoutInput().text.Length(); }
            CrossStatus RawCrossStatus() const;
            bool ShouldRecomputeSelection(const SelectableInfo& o) const {
                return selectableId != o.selectableId || rawStartHandleOffset != o.rawStartHandleOffset || rawEndHandleOffset != o.rawEndHandleOffset;
            }
            AnchorInfo AnchorForOffset(int offset) const;
            Selection MakeSingleLayoutSelection(int start, int end) const;
        };

        // SelectionLayout: the selectables a gesture touches, sorted, with the slots of the two
        // handles (odd slots are on a text, even ones between texts).
        class SelectionLayout {
        public:
            std::vector<SelectableInfo> infos;
            std::map<std::int64_t, int> indexById;
            int startSlot = 0, endSlot = 0;
            bool isStartHandle = false;
            std::optional<Selection> previousSelection;

            int Size() const { return int(infos.size()); }
            CrossStatus Cross() const;
            const SelectableInfo& StartInfo() const;
            const SelectableInfo& EndInfo() const;
            const SelectableInfo& CurrentInfo() const { return isStartHandle ? StartInfo() : EndInfo(); }
            const SelectableInfo& FirstInfo() const { return Cross() == CrossStatus::Crossed ? EndInfo() : StartInfo(); }
            const SelectableInfo& LastInfo() const { return Cross() == CrossStatus::Crossed ? StartInfo() : EndInfo(); }
            void ForEachMiddleInfo(const std::function<void(const SelectableInfo&)>& block) const;
            const SelectableInfo* InfoForSelectable(std::int64_t id) const;
            bool ShouldRecomputeSelection(const std::optional<SelectionLayout>& other) const;
            SubSelections CreateSubSelections(const Selection& selection) const;

        private:
            int SlotToIndex(int slot, bool isStartSlot) const;
        };

        // SelectionAdjustment.adjust.
        Selection Adjust(Adjustment adjustment, const SelectionLayout& layout);
        // isCollapsed(layout): nothing between the anchors.
        bool IsCollapsed(const std::optional<Selection>& selection, const SelectionLayout* layout);

        class SelectionLayoutBuilder {
        public:
            Offset currentPosition;
            std::optional<Offset> previousHandlePosition;
            Coordinates containerCoordinates;
            bool isStartHandle = false;
            std::optional<Selection> previousSelection;
            const SelectionLayout* previousLayout = nullptr;
            std::function<int(std::int64_t, std::int64_t)> compareIds;   // selectable order
            bool allowSelectionBetweenSelectables = false;

            bool IsEmpty() const { return m_Infos.empty(); }
            std::optional<SelectionLayout> Build() const;
            void AppendInfo(std::int64_t id, int rawStart, Direction startX, Direction startY, int rawEnd, Direction endX, Direction endY,
                            int rawPrevious, std::shared_ptr<const TextLayoutResult> layout, TextBackend* backend);
            // MultiWidgetSelectionDelegate's appendSelectableInfo.
            void AppendSelectableInfo(std::shared_ptr<const TextLayoutResult> layout, TextBackend* backend, Offset localPosition,
                                      int givenRawPreviousHandleOffset, std::optional<Offset> previousHandlePosition,
                                      std::int64_t selectableId, bool isLast);

        private:
            int UpdateSlot(int slot, Direction x, Direction y) const;
            std::vector<SelectableInfo> m_Infos;
            std::map<std::int64_t, int> m_IndexById;
            int m_StartSlot = -1, m_EndSlot = -1, m_CurrentSlot = -1;
        };

        // Selectable (MultiWidgetSelectionDelegate): one text's part in a selection.
        class Selectable {
        public:
            std::int64_t selectableId = 0;
            std::function<std::optional<Coordinates>()> coordinatesCallback;
            std::function<std::shared_ptr<const TextLayoutResult>()> layoutCallback;
            TextBackend* backend = nullptr;
            std::function<void(Rect)> bringIntoView;   // the rect in the selectable's space
            std::function<std::shared_ptr<ui::PinnableContainer>()> pinnableContainer;

            void AppendSelectableInfoToBuilder(SelectionLayoutBuilder& builder, bool isLast, const SelectionLayout* previousLayout);
            std::optional<Selection> GetSelectAllSelection() const;
            std::optional<Offset> GetHandlePosition(const Selection& selection, bool isStartHandle) const;
            std::optional<Coordinates> GetLayoutCoordinates() const;
            AnnotatedString GetText() const;
            RectF GetBoundingBox(int offset) const;
            float GetLineLeft(int offset) const;
            float GetLineRight(int offset) const;
            float GetCenterYForOffset(int offset) const;
            std::pair<int, int> GetRangeOfLineContaining(int offset) const;
            int GetLastVisibleOffset() const;
            float GetLineHeight(int offset) const;

        private:
            mutable const TextLayoutResult* m_LastVisibleFor = nullptr;
            mutable int m_LastVisible = -1;
        };

        // SelectionRegistrar(Impl): the selectables in a container and the gesture callbacks the
        // manager installs.
        class SelectionRegistrar {
        public:
            explicit SelectionRegistrar(std::int64_t firstId = 1) : m_NextId(firstId) {}
            rt::State<SubSelections> subselections;

            std::shared_ptr<Selectable> Subscribe(std::shared_ptr<Selectable> selectable);
            void Unsubscribe(const Selectable& selectable);
            std::int64_t NextSelectableId();
            std::int64_t PeekNextId() const { return m_NextId; }
            const std::vector<std::shared_ptr<Selectable>>& Sort(const Coordinates& container);
            const std::vector<std::shared_ptr<Selectable>>& Selectables() const { return m_Selectables; }
            Selectable* Find(std::int64_t id) const;
            bool HasSelection(std::int64_t id) const { return subselections.Peek().count(id) != 0; }

            void NotifyPositionChange(std::int64_t id);
            void NotifySelectionUpdateStart(const Coordinates& c, Offset start, Adjustment a, bool touch);
            void NotifySelectionUpdateSelectAll(std::int64_t id, bool touch);
            bool NotifySelectionUpdate(const Coordinates& c, Offset newPosition, Offset previousPosition, bool isStartHandle, Adjustment a, bool touch);
            void NotifySelectionUpdateEnd();
            void NotifySelectableChange(std::int64_t id);

            std::function<void(std::int64_t)> onPositionChange;
            std::function<void(bool, const Coordinates&, Offset, Adjustment)> onSelectionUpdateStart;
            std::function<void(bool, std::int64_t)> onSelectionUpdateSelectAll;
            std::function<bool(bool, const Coordinates&, Offset, Offset, bool, Adjustment)> onSelectionUpdate;
            std::function<void()> onSelectionUpdateEnd;
            std::function<void(std::int64_t)> onSelectableChange;
            std::function<void(std::int64_t)> afterSelectableUnsubscribe;

        private:
            bool m_Sorted = false;
            std::vector<std::shared_ptr<Selectable>> m_Selectables;
            std::map<std::int64_t, Selectable*> m_Map;
            std::int64_t m_NextId;
        };

        // LocalSelectionRegistrar: set inside a SelectionContainer, null under DisableSelection.
        inline rt::Local<std::shared_ptr<SelectionRegistrar>> LocalSelectionRegistrar{ nullptr };

        // makeSelectionModifier: a text's selection gestures (long press and drag, double / triple
        // taps, mouse clicks and drags), reported to the registrar in the text's coordinates.
        Modifier SelectionGestures(Modifier m, std::shared_ptr<SelectionRegistrar> registrar, std::int64_t selectableId,
                                   std::function<std::optional<Coordinates>()> coordinates);
        // isCopyKeyEvent (Android key mapping): Ctrl+C, Ctrl+Insert or the Copy key.
        bool IsCopyKeyEvent(const ui::KeyEvent& e);

        class SelectionManager;
    }

    // SelectionState: observe a container's selection and drive it (select all, clear, ...).
    class SelectionState {
    public:
        SelectionState() = default;
        SelectionState(const SelectionState&) = delete;
        SelectionState& operator=(const SelectionState&) = delete;
        // The selected text of each selectable with a selection, in order.
        const std::vector<text::AnnotatedString>& SelectedTexts() const { return m_SelectedTexts.Get(); }
        void SelectAll();
        void Clear();
        void ExtendSelectionByWord();
        std::vector<text::AnnotatedString> GetSelectableTexts() const;
        // select(TextRange): offsets over the selectables' texts laid end to end.
        void Select(int start, int end);

    private:
        friend class selection::SelectionManager;
        friend void SelectionContainer(SelectionState&, const Modifier&, std::function<void()>);
        friend SelectionState& RememberSelectionState();
        rt::State<std::optional<selection::Selection>> m_Selection;
        rt::State<std::vector<text::AnnotatedString>> m_SelectedTexts;
        selection::SelectionManager* m_Manager = nullptr;
    };
    // rememberSelectionState: survives re-creation.
    SelectionState& RememberSelectionState();

    void SelectionContainer(const Modifier& modifier, std::function<void()> content);
    void SelectionContainer(SelectionState& state, const Modifier& modifier, std::function<void()> content);
    // DisableSelection: texts inside are not selectable (LocalSelectionRegistrar provides null).
    void DisableSelection(std::function<void()> content);

    // Selection handles (SelectionHandles.kt): 25 dp, a quarter square joined to a circle with the
    // point at the selection; the start handle of LTR text points right (mirrored).
    inline constexpr float kHandleWidthDp = 25, kHandleHeightDp = 25;

}
