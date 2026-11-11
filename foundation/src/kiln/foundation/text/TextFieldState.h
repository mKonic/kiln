#pragma once

// TextFieldState (spec 04 req. 1-3; Compose's TextFieldState): the text, selection and
// composition of a text field as one observable value, edited through TextFieldBuffer. User edits
// go through the field's input transformation and are recorded for undo (typing merges into one
// step); programmatic edits through Edit never merge. TransformedTextFieldState is the field's view
// of it through output and codepoint transformations.

#include "kiln/foundation/text/input/TextFieldBuffer.h"
#include "kiln/foundation/text/input/Transformations.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Composition.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiln::foundation {

    // --- undo ---------------------------------------------------------------------------------

    enum class TextEditType : std::uint8_t { Insert, Delete, Replace };
    enum class TextDeleteType : std::uint8_t { Start, End, Inner, NotByUser };
    enum class TextFieldEditUndoBehavior : std::uint8_t { MergeIfPossible, ClearHistory, NeverMerge };

    struct TextUndoOperation {
        int index = 0;
        std::u16string preText, postText;
        TextRange preSelection, postSelection;
        std::int64_t timeInMillis = 0;
        bool canMerge = true;
        TextEditType EditType() const;
        TextDeleteType DeletionType() const;
        bool operator==(const TextUndoOperation&) const = default;
    };
    // Edits within this many ms of each other merge (SNAPSHOTS_INTERVAL_MILLIS).
    constexpr std::int64_t kUndoSnapshotsIntervalMillis = 5000;
    // The clock undo stamps edits with (System.currentTimeMillis; replaceable for tests).
    void SetUndoClock(std::function<std::int64_t()> clock);
    std::int64_t UndoNowMillis();
    // merge: typing (inserts at the end of the last one) and deleting in one direction merge;
    // new lines, other kinds and edits over 5 s apart do not.
    std::optional<TextUndoOperation> Merge(const TextUndoOperation& a, const TextUndoOperation& next);

    class TextFieldState;

    class TextUndoManager {
    public:
        static constexpr int kCapacity = 100;
        bool CanUndo() const;
        bool CanRedo() const;
        void Undo(TextFieldState& state);
        void Redo(TextFieldState& state);
        void Record(TextUndoOperation op);
        void RecordChanges(const TextFieldCharSequence& pre, const TextFieldCharSequence& post, const ChangeList& changes, bool allowMerge);
        void ClearHistory();
        // For saving and restoring (Saver).
        struct Saved { std::optional<TextUndoOperation> staging; std::vector<TextUndoOperation> undo, redo; };
        Saved Save() const { return { m_Staging.Peek(), m_Undo, m_Redo }; }
        void Restore(Saved s);
    private:
        void Flush();
        rt::State<std::optional<TextUndoOperation>> m_Staging{ std::nullopt };
        std::vector<TextUndoOperation> m_Undo, m_Redo;
        rt::State<int> m_Version{ 0 };   // read by CanUndo / CanRedo observers
    };

    // --- state --------------------------------------------------------------------------------

    class TextFieldState {
    public:
        explicit TextFieldState(std::string_view initialText = {}, std::optional<TextRange> initialSelection = std::nullopt);
        TextFieldState(const TextFieldState&) = delete;
        TextFieldState& operator=(const TextFieldState&) = delete;

        const TextFieldCharSequence& Value() const { return m_Value.Get(); }
        const TextFieldCharSequence& Peek() const { return m_Value.Peek(); }
        const std::u16string& Text() const { return Value().text; }
        std::string TextUtf8() const { return ToUtf8(Value().text); }
        TextRange Selection() const { return Value().selection; }
        std::optional<TextRange> Composition() const { return Value().composition; }
        // textStyles: the text's styles (empty without any).
        std::vector<text::Range<text::SpanStyle>> SpanStyles(TextRange range) const {
            const auto& s = Value().textFieldTextStyles;
            return s ? s->GetSpanStyles(range.Min(), range.Max()) : std::vector<text::Range<text::SpanStyle>>{};
        }
        std::vector<text::Range<text::ParagraphStyle>> ParagraphStyles(TextRange range) const {
            const auto& s = Value().textFieldTextStyles;
            return s ? s->GetParagraphStyles(range.Min(), range.Max()) : std::vector<text::Range<text::ParagraphStyle>>{};
        }

        // edit { }: a programmatic edit (no input transformation; never merged for undo).
        void Edit(const std::function<void(TextFieldBuffer&)>& block);
        // setTextAndPlaceCursorAtEnd / setTextAndSelectAll / clearText.
        void SetTextAndPlaceCursorAtEnd(std::string_view text);
        void SetTextAndSelectAll(std::string_view text);
        void ClearText();

        // The user's edits (typing, the IME, paste): through the input transformation, merged
        // for undo where possible.
        void EditAsUser(const InputTransformation* inputTransformation, const std::function<void(TextFieldBuffer&)>& block,
                        bool restartImeIfContentChanges = true,
                        TextFieldEditUndoBehavior undoBehavior = TextFieldEditUndoBehavior::MergeIfPossible);
        // Undo / redo applying: no transformation, no undo record.
        void EditWithNoSideEffects(const std::function<void(TextFieldBuffer&)>& block);

        // undoState.
        bool CanUndo() const { return m_Undo.CanUndo(); }
        bool CanRedo() const { return m_Undo.CanRedo(); }
        void Undo() { CheckNotInOnTextChanged(); m_Undo.Undo(*this); }
        void Redo() { CheckNotInOnTextChanged(); m_Undo.Redo(*this); }
        void ClearUndoHistory() { m_Undo.ClearHistory(); }
        TextUndoManager& UndoManager() { return m_Undo; }

        // Whether the last change came from the user (userCommit) and picked an IME suggestion.
        bool UserCommit() const { return m_UserCommit; }
        bool SuggestionSelected() const { return m_SuggestionSelected; }
        // onTextChanged: told after the text changes (not from within it).
        void SetOnTextChanged(std::function<void(const std::u16string&)> fn) { m_OnTextChanged = std::move(fn); }

        // NotifyImeListener: told of every value change; restartIme when the content changed
        // under a composition.
        using NotifyImeListener = std::function<void(const TextFieldCharSequence& oldValue, const TextFieldCharSequence& newValue, bool restartIme)>;
        int AddNotifyImeListener(NotifyImeListener l);
        void RemoveNotifyImeListener(int token);

        // Incremented on every change (lets the platform session detect external edits).
        std::uint64_t Version() const { return m_Version; }
        // Expires when the state is destroyed (holders that may outlive it).
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }

        // Saver: text, selection and the undo history.
        struct Saved {
            std::u16string text;
            TextRange selection;
            TextUndoManager::Saved undo;
            std::shared_ptr<const TextFieldTextStyles> styles;
        };
        Saved Save() const;
        void Restore(const Saved& s);

        void CheckNotInOnTextChanged() const;

    private:
        void CommitEdit(TextFieldBuffer& next);
        void CommitEditAsUser(const InputTransformation* inputTransformation, bool restartIme, TextFieldEditUndoBehavior undo);
        void UpdateValueAndNotifyListeners(const TextFieldCharSequence& oldValue, const TextFieldCharSequence& newValue,
                                           bool restartImeIfContentChanges, std::optional<bool> textChanged);
        void SyncMainBufferToTemporaryBuffer(TextFieldBuffer& temp, bool textChanged, bool selectionChanged, bool styleChanged,
                                             std::optional<bool> valueTextChanged);
        void RecordEditForUndo(const TextFieldCharSequence& pre, const TextFieldCharSequence& post, const ChangeList& changes, TextFieldEditUndoBehavior b);

        TextUndoManager m_Undo;
        TextFieldBuffer m_Main;
        rt::State<TextFieldCharSequence> m_Value;
        bool m_Editing = false;
        bool m_UserCommit = false;
        bool m_SuggestionSelected = false;
        bool m_InOnTextChanged = false;
        std::function<void(const std::u16string&)> m_OnTextChanged;
        std::vector<std::pair<int, NotifyImeListener>> m_ImeListeners;
        int m_NextListener = 0;
        std::uint64_t m_Version = 0;
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
    };

    // rememberTextFieldState: the text, selection and undo history survive re-creation. The
    // selection starts at the end unless given.
    TextFieldState& RememberTextFieldState(std::string_view initialText = {}, std::optional<TextRange> initialSelection = std::nullopt, rt::Key key = {});

    // --- the field's transformed view (TransformedTextFieldState) -----------------------------

    enum class WedgeAffinity : std::uint8_t { Start, End };
    struct SelectionWedgeAffinity {
        WedgeAffinity startAffinity = WedgeAffinity::Start, endAffinity = WedgeAffinity::Start;
        bool operator==(const SelectionWedgeAffinity&) const = default;
    };
    enum class IndexTransformationType : std::uint8_t { Untransformed, Insertion, Replacement, Deletion };

    class TransformedTextFieldState {
    public:
        TransformedTextFieldState(TextFieldState& state, InputTransformationPtr input = nullptr,
                                  CodepointTransformationPtr codepoint = nullptr, OutputTransformationPtr output = nullptr);

        TextFieldState& State() { return m_State; }
        void Update(InputTransformationPtr input) { m_Input = std::move(input); }
        const InputTransformation* Input() const { return m_Input.get(); }

        const TextFieldCharSequence& UntransformedText() const { return m_State.Value(); }
        // outputText: after the output transformation; visualText: also after the codepoint one.
        TextFieldCharSequence OutputText() const;
        TextFieldCharSequence VisualText() const;
        bool IsTransformed() const { return m_Output || m_Codepoint; }
        // outputText !== untransformedText: an output transformation changed the text.
        bool IsOutputTransformed() const { return OutputTransformed().has_value(); }
        SelectionWedgeAffinity selectionWedgeAffinity;

        void PlaceCursorBeforeCharAt(int transformedOffset) { SelectCharsIn(TextRange(transformedOffset)); }
        void SelectCharsIn(TextRange transformedRange);
        void SelectUntransformedCharsIn(TextRange untransformedRange);
        void HighlightCharsIn(TextHighlightType type, TextRange transformedRange);
        void ReplaceAll(std::u16string_view text);
        void SelectAll();
        void DeleteSelectedText(bool isFromHardwareSource = false);
        void ReplaceText(std::u16string_view text, TextRange transformedRange,
                         TextFieldEditUndoBehavior undo = TextFieldEditUndoBehavior::MergeIfPossible,
                         bool restartImeIfContentChanges = true, bool isFromHardwareSource = false);
        void ReplaceSelectedText(std::u16string_view text, bool clearComposition = false,
                                 TextFieldEditUndoBehavior undo = TextFieldEditUndoBehavior::MergeIfPossible,
                                 bool restartImeIfContentChanges = true, bool isFromHardwareSource = false);
        void CollapseSelectionToMax();
        void CollapseSelectionToEnd();
        void Undo() { m_State.Undo(); }
        void Redo() { m_State.Redo(); }
        void EditUntransformedTextAsUser(const std::function<void(TextFieldBuffer&)>& block, bool restartImeIfContentChanges = true);

        TextRange MapToTransformed(int offset) const;
        TextRange MapToTransformed(TextRange range) const;
        TextRange MapFromTransformed(int offset) const;
        TextRange MapFromTransformed(TextRange range) const;
        IndexTransformationType IndexTransformation(int transformedIndex, TextRange* untransformed = nullptr, TextRange* retransformed = nullptr) const;

    private:
        struct Transformed { TextFieldCharSequence text; OffsetMappingCalculator mapping; };
        void UpdateWedgeAffinity(TextFieldBuffer& b);
        // The transformed texts for the current value (recomputed when it changes).
        const std::optional<Transformed>& OutputTransformed() const;
        const std::optional<Transformed>& CodepointTransformed() const;

        // What a transformation made of the value, derived (derivedStateOf): it is redone when
        // the value or any state the transformation read changes, and readers of the visual
        // text hear about both. The selection is mapped per wedge affinity on top.
        struct OutputRaw {
            OffsetMappingCalculator mapping;
            std::optional<TextFieldBuffer> buffer;   // null: nothing changed
            TextFieldCharSequence source;
        };
        struct CodepointRaw {
            OffsetMappingCalculator mapping;
            std::optional<std::u16string> visual;     // null: nothing changed
            TextFieldCharSequence source;
        };
        std::shared_ptr<const OutputRaw> ComputeOutputRaw() const;
        std::shared_ptr<const CodepointRaw> ComputeCodepointRaw() const;

        TextFieldState& m_State;
        InputTransformationPtr m_Input;
        CodepointTransformationPtr m_Codepoint;
        OutputTransformationPtr m_Output;
        mutable rt::Derived<std::shared_ptr<const OutputRaw>, rt::NeverEqual> m_OutputRaw{ [this] { return ComputeOutputRaw(); } };
        mutable rt::Derived<std::shared_ptr<const CodepointRaw>, rt::NeverEqual> m_CodepointRaw{ [this] { return ComputeCodepointRaw(); } };
        mutable std::optional<Transformed> m_OutputCache, m_CodepointCache;
        mutable const void* m_OutputFor = nullptr;
        mutable const void* m_CodepointFor = nullptr;
        mutable SelectionWedgeAffinity m_OutputAffinity, m_CodepointAffinity;
    };

    // The IME's edit commands (ImeEditCommand.android.kt) against a transformed state, batched
    // between BeginBatchEdit / EndBatchEdit (one user edit per batch).
    class ImeEditCommandScope {
    public:
        explicit ImeEditCommandScope(TransformedTextFieldState& state) : m_State(state) {}
        bool BeginBatchEdit() { ++m_Depth; return true; }
        bool EndBatchEdit();
        void Edit(std::function<void(TextFieldBuffer&)> block);

        void CommitText(std::u16string_view text, int newCursorPosition, bool isTextSuggestionSelected = false);
        void SetComposingText(std::u16string_view text, int newCursorPosition, const std::vector<PlacedSpan>* annotations = nullptr,
                              bool isTextSuggestionSelected = false);
        void SetComposingRegion(int start, int end);
        void FinishComposingText();
        void DeleteSurroundingText(int before, int after);
        void DeleteSurroundingTextInCodePoints(int before, int after);
        void SetSelection(int start, int end);

    private:
        TextRange MapFromTransformed(TextRange r) const { return m_State.IsTransformed() ? m_State.MapFromTransformed(r) : r; }
        TextRange MapToTransformed(TextRange r) const { return m_State.IsTransformed() ? m_State.MapToTransformed(r) : r; }
        int TransformedLength() const { return m_Batch ? m_Batch->Length() : m_State.VisualText().Length(); }
        TransformedTextFieldState& m_State;
        std::vector<std::function<void(TextFieldBuffer&)>> m_Commands;
        TextFieldBuffer* m_Batch = nullptr;
        int m_Depth = 0;
    };

    // imeReplace / imeDelete: replacements keep the common prefix and suffix of the old text
    // (fewer changes for undo and spans); deletes keep the composition around them.
    void ImeReplace(TextFieldBuffer& b, int start, int end, std::u16string_view text);
    void ImeDelete(TextFieldBuffer& b, int start, int end);

}
