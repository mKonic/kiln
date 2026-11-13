#include "kiln/foundation/text/TextFieldState.h"
#include <kiln/Function.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace kiln::foundation {

    // --- undo ---------------------------------------------------------------------------------

    namespace {
        kiln::Function<std::int64_t()>& UndoClock() {
            static kiln::Function<std::int64_t()> clock;
            return clock;
        }
    }

    void SetUndoClock(kiln::Function<std::int64_t()> clock) { UndoClock() = std::move(clock); }
    std::int64_t UndoNowMillis() {
        if (UndoClock()) return UndoClock()();
        return std::int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }

    TextEditType TextUndoOperation::EditType() const {
        if (preText.empty() && postText.empty()) throw std::invalid_argument("either pre or post text must not be empty");
        if (preText.empty()) return TextEditType::Insert;
        if (postText.empty()) return TextEditType::Delete;
        return TextEditType::Replace;
    }

    TextDeleteType TextUndoOperation::DeletionType() const {
        if (EditType() != TextEditType::Delete) return TextDeleteType::NotByUser;
        if (!postSelection.Collapsed()) return TextDeleteType::NotByUser;
        if (preSelection.Collapsed()) return preSelection.start > postSelection.start ? TextDeleteType::Start : TextDeleteType::End;
        if (preSelection.Min() == postSelection.start && preSelection.Min() == index) return TextDeleteType::Inner;
        return TextDeleteType::NotByUser;
    }

    std::optional<TextUndoOperation> Merge(const TextUndoOperation& a, const TextUndoOperation& next) {
        if (!a.canMerge || !next.canMerge) return std::nullopt;
        if (next.timeInMillis < a.timeInMillis || next.timeInMillis - a.timeInMillis >= kUndoSnapshotsIntervalMillis) return std::nullopt;
        auto newLine = [](const TextUndoOperation& o) { return o.postText == u"\n" || o.postText == u"\r\n"; };
        if (newLine(a) || newLine(next)) return std::nullopt;
        if (a.EditType() != next.EditType()) return std::nullopt;
        if (a.EditType() == TextEditType::Insert && a.index + int(a.postText.size()) == next.index)
            return TextUndoOperation{ a.index, u"", a.postText + next.postText, a.preSelection, next.postSelection, a.timeInMillis, true };
        if (a.EditType() == TextEditType::Delete) {
            const auto type = a.DeletionType();
            if (type == next.DeletionType() && (type == TextDeleteType::Start || type == TextDeleteType::End)) {
                if (a.index == next.index + int(next.preText.size()))
                    return TextUndoOperation{ next.index, next.preText + a.preText, u"", a.preSelection, next.postSelection, a.timeInMillis, true };
                if (a.index == next.index)
                    return TextUndoOperation{ a.index, a.preText + next.preText, u"", a.preSelection, next.postSelection, a.timeInMillis, true };
            }
        }
        return std::nullopt;
    }

    bool TextUndoManager::CanUndo() const { m_Version.Get(); return !m_Undo.empty() || m_Staging.Get().has_value(); }
    bool TextUndoManager::CanRedo() const { m_Version.Get(); return !m_Redo.empty() && !m_Staging.Get().has_value(); }

    void TextUndoManager::Record(TextUndoOperation op) {
        const auto& staging = m_Staging.Peek();
        if (!staging) { m_Staging.Set(std::move(op)); return; }
        if (auto merged = Merge(*staging, op)) { m_Staging.Set(std::move(merged)); return; }
        Flush();
        m_Staging.Set(std::move(op));
    }

    void TextUndoManager::Flush() {
        if (const auto& staging = m_Staging.Peek()) {
            // UndoManager.record: a new action drops the redo stack; the oldest go past capacity.
            m_Redo.clear();
            while (int(m_Undo.size() + m_Redo.size()) > kCapacity - 1) m_Undo.erase(m_Undo.begin());
            m_Undo.push_back(*staging);
            m_Version.Set(m_Version.Peek() + 1);
        }
        m_Staging.Set(std::nullopt);
    }

    void TextUndoManager::ClearHistory() {
        m_Staging.Set(std::nullopt);
        m_Undo.clear();
        m_Redo.clear();
        m_Version.Set(m_Version.Peek() + 1);
    }

    void TextUndoManager::Undo(TextFieldState& state) {
        if (!CanUndo()) return;
        Flush();
        TextUndoOperation op = m_Undo.back();
        m_Undo.pop_back();
        m_Redo.push_back(op);
        m_Version.Set(m_Version.Peek() + 1);
        state.EditWithNoSideEffects([&](TextFieldBuffer& b) {
            b.Replace(op.index, op.index + int(op.postText.size()), op.preText);
            b.SetSelectionCoerced(op.preSelection.start, op.preSelection.end);
        });
    }

    void TextUndoManager::Redo(TextFieldState& state) {
        if (!CanRedo()) return;
        TextUndoOperation op = m_Redo.back();
        m_Redo.pop_back();
        m_Undo.push_back(op);
        m_Version.Set(m_Version.Peek() + 1);
        state.EditWithNoSideEffects([&](TextFieldBuffer& b) {
            b.Replace(op.index, op.index + int(op.preText.size()), op.postText);
            b.SetSelectionCoerced(op.postSelection.start, op.postSelection.end);
        });
    }

    void TextUndoManager::RecordChanges(const TextFieldCharSequence& pre, const TextFieldCharSequence& post, const ChangeList& changes, bool allowMerge) {
        auto whole = [&] {
            Record({ 0, pre.text, post.text, pre.selection, post.selection, UndoNowMillis(), false });
        };
        if (changes.ChangeCount() > 1) { whole(); return; }
        if (changes.ChangeCount() != 1) return;
        const TextRange origPre = changes.OriginalRange(0), origPost = changes.Range(0);
        const TextRange preR = origPre.CoerceIn(0, pre.Length()), postR = origPost.CoerceIn(0, post.Length());
        const bool single = origPre == preR && origPost == postR && preR.Min() == postR.Min();
        if (!single) { whole(); return; }
        if (preR.Collapsed() && postR.Collapsed()) return;
        Record({ preR.Min(), pre.text.substr(std::size_t(preR.Min()), std::size_t(preR.Length())),
                 post.text.substr(std::size_t(postR.Min()), std::size_t(postR.Length())), pre.selection, post.selection,
                 UndoNowMillis(), allowMerge });
    }

    void TextUndoManager::Restore(Saved s) {
        m_Staging.Set(std::move(s.staging));
        m_Undo = std::move(s.undo);
        m_Redo = std::move(s.redo);
        m_Version.Set(m_Version.Peek() + 1);
    }

    // --- TextFieldState -----------------------------------------------------------------------

    TextFieldState::TextFieldState(std::string_view initialText, std::optional<TextRange> initialSelection)
        : m_Main(TextFieldCharSequence(ToUtf16(initialText), TextRange(0))),
          m_Value(TextFieldCharSequence(ToUtf16(initialText), TextRange(0))) {
        const std::u16string text = ToUtf16(initialText);
        const TextRange sel = initialSelection.value_or(TextRange(int(text.size()))).CoerceIn(0, int(text.size()));
        m_Main = TextFieldBuffer(TextFieldCharSequence(text, sel));
        m_Value.Set(TextFieldCharSequence(text, sel));
    }

    void TextFieldState::CheckNotInOnTextChanged() const {
        if (m_InOnTextChanged)
            throw std::logic_error("TextFieldState cannot be edited from its onTextChanged callback; use an InputTransformation");
    }

    void TextFieldState::Edit(const kiln::Function<void(TextFieldBuffer&)>& block) {
        CheckNotInOnTextChanged();
        if (m_Editing) throw std::logic_error("TextFieldState does not support concurrent or nested editing");
        m_Editing = true;
        TextFieldBuffer buffer(m_Value.Peek());
        try {
            block(buffer);
            CommitEdit(buffer);
        } catch (...) {
            m_Editing = false;
            m_UserCommit = false;
            throw;
        }
        m_Editing = false;
        m_UserCommit = false;
    }

    void TextFieldState::SetTextAndPlaceCursorAtEnd(std::string_view text) {
        const std::u16string t = ToUtf16(text);
        Edit([&](TextFieldBuffer& b) { b.Replace(0, b.Length(), t); b.PlaceCursorAtEnd(); });
    }
    void TextFieldState::SetTextAndSelectAll(std::string_view text) {
        const std::u16string t = ToUtf16(text);
        Edit([&](TextFieldBuffer& b) { b.Replace(0, b.Length(), t); b.SelectAll(); });
    }
    void TextFieldState::ClearText() {
        Edit([](TextFieldBuffer& b) { b.Delete(0, b.Length()); b.PlaceCursorAtEnd(); });
    }

    namespace {
        // hasTextChanged: a single in-place change compares just its characters.
        bool HasTextChanged(const TextFieldCharSequence& prev, std::u16string_view next, const ChangeList& changes) {
            if (changes.ChangeCount() == 0) return false;
            if (prev.Length() != int(next.size())) return true;
            if (changes.ChangeCount() == 1) {
                const TextRange o = changes.OriginalRange(0), n = changes.Range(0);
                if (o == n && o.Min() >= 0 && o.Max() <= prev.Length()) {
                    for (int i = o.Min(); i < o.Max(); ++i) if (prev.text[std::size_t(i)] != next[std::size_t(i)]) return true;
                    return false;
                }
            }
            return prev.text != next;
        }
        // finalizeComposingAnnotations: the IME's spans, else an underline over the composition.
        std::vector<PlacedSpan> FinalizeComposing(const std::optional<TextRange>& composition, const std::vector<PlacedSpan>& list) {
            if (!list.empty()) return list;
            if (composition && !composition->Collapsed()) {
                text::SpanStyle underline;
                underline.textDecoration = text::TextDecoration::Underline;
                return { PlacedSpan{ underline, composition->Min(), composition->Max(), {} } };
            }
            return {};
        }
    }

    namespace {
        bool SameStyles(const TextStyleBuffer* a, const TextStyleBuffer* b) { return a == b || (a && b && *a == *b); }
    }

    void TextFieldState::CommitEdit(TextFieldBuffer& next) {
        const bool textChanged = next.Changes().ChangeCount() > 0;
        const bool selectionChanged = next.Selection() != m_Main.Selection();
        const bool styleChanged = !SameStyles(next.StyleBuffer(), m_Main.StyleBuffer());
        std::optional<bool> valueTextChanged;
        if (!textChanged) valueTextChanged = false;
        else if (m_OnTextChanged) valueTextChanged = HasTextChanged(m_Value.Peek(), next.AsCharSequence(), next.Changes());
        if (textChanged) RecordEditForUndo(m_Value.Peek(), next.ToTextFieldCharSequence(), next.Changes(), TextFieldEditUndoBehavior::NeverMerge);
        SyncMainBufferToTemporaryBuffer(next, textChanged, selectionChanged, styleChanged, valueTextChanged);
    }

    void TextFieldState::EditAsUser(const InputTransformation* input, const kiln::Function<void(TextFieldBuffer&)>& block,
                                    bool restartIme, TextFieldEditUndoBehavior undo) {
        CheckNotInOnTextChanged();
        m_Main.Tracker().ClearChanges();
        block(m_Main);
        CommitEditAsUser(input, restartIme, undo);
        m_UserCommit = true;
        m_SuggestionSelected = m_Main.suggestionSelected;
    }

    void TextFieldState::EditWithNoSideEffects(const kiln::Function<void(TextFieldBuffer&)>& block) {
        m_Main.Tracker().ClearChanges();
        block(m_Main);
        const TextFieldCharSequence before = m_Value.Peek();
        const TextFieldCharSequence after = m_Main.ToTextFieldCharSequence();
        UpdateValueAndNotifyListeners(before, after, true,
                                      m_OnTextChanged ? std::optional(HasTextChanged(before, after.text, m_Main.Changes())) : std::nullopt);
    }

    void TextFieldState::CommitEditAsUser(const InputTransformation* input, bool restartIme, TextFieldEditUndoBehavior undo) {
        const TextFieldCharSequence before = m_Value.Peek();
        if (m_Main.Changes().ChangeCount() == 0 && before.selection == m_Main.Selection()) {
            const auto composing = FinalizeComposing(m_Main.Composition(), m_Main.ComposingAnnotations());
            if (before.composition != m_Main.Composition() || before.highlight != m_Main.Highlight() ||
                before.composingAnnotations != composing) {
                UpdateValueAndNotifyListeners(before,
                                              TextFieldCharSequence(m_Main.AsCharSequence(), m_Main.Selection(), m_Main.Composition(),
                                                                    m_Main.Highlight(), composing, before.textFieldTextStyles),
                                              restartIme, false);
            }
            return;
        }
        const bool contentMayHaveChanged = m_Main.Changes().ChangeCount() != 0;
        const TextFieldCharSequence after(m_Main.AsCharSequence(), m_Main.Selection(), m_Main.Composition(), m_Main.Highlight(),
                                          FinalizeComposing(m_Main.Composition(), m_Main.ComposingAnnotations()), m_Main.GetTextFieldTextStyles());
        if (!input) {
            RecordEditForUndo(before, after, m_Main.Changes(), undo);
            std::optional<bool> changed;
            if (!contentMayHaveChanged) changed = false;
            else if (m_OnTextChanged) changed = HasTextChanged(before, after.text, m_Main.Changes());
            UpdateValueAndNotifyListeners(before, after, contentMayHaveChanged && restartIme, changed);
            return;
        }
        const ChangeTracker changes = m_Main.Tracker();
        TextFieldBuffer filtered(after, &changes, before);
        input->TransformInput(filtered);
        const bool textChangedByFilter = filtered.AsCharSequence() != after.text;
        const bool selectionChangedByFilter = filtered.Selection() != after.selection;
        const bool styleChangedByFilter =
            !SameStyles(filtered.StyleBuffer(), after.textFieldTextStyles ? &after.textFieldTextStyles->Buffer() : nullptr);
        const bool changedByFilter = textChangedByFilter || selectionChangedByFilter || styleChangedByFilter;
        const TextFieldCharSequence post = changedByFilter ? filtered.ToTextFieldCharSequence()
                                                           : filtered.ToTextFieldCharSequence(filtered.Selection(), after.composition);
        RecordEditForUndo(before, post, filtered.Changes(), undo);
        std::optional<bool> valueTextChanged;
        if (!contentMayHaveChanged && !textChangedByFilter) valueTextChanged = false;
        else if (!contentMayHaveChanged && textChangedByFilter) valueTextChanged = true;
        else if (m_OnTextChanged) valueTextChanged = HasTextChanged(before, post.text, filtered.Changes());
        if (changedByFilter) SyncMainBufferToTemporaryBuffer(filtered, textChangedByFilter, selectionChangedByFilter, styleChangedByFilter, valueTextChanged);
        else UpdateValueAndNotifyListeners(before, post, restartIme, valueTextChanged);
    }

    void TextFieldState::SyncMainBufferToTemporaryBuffer(TextFieldBuffer& temp, bool textChanged, bool selectionChanged, bool styleChanged,
                                                         std::optional<bool> valueTextChanged) {
        const TextFieldCharSequence oldValue = m_Main.ToTextFieldCharSequence();
        if (textChanged || styleChanged)
            m_Main = TextFieldBuffer(TextFieldCharSequence(temp.AsCharSequence(), temp.Selection(), std::nullopt, std::nullopt, {}, temp.GetTextFieldTextStyles()));
        else if (selectionChanged) m_Main.SetSelection(temp.Selection());
        if (textChanged || selectionChanged || oldValue.composition != temp.Composition()) m_Main.CommitComposition();
        UpdateValueAndNotifyListeners(oldValue, m_Main.ToTextFieldCharSequence(), true, valueTextChanged);
    }

    void TextFieldState::UpdateValueAndNotifyListeners(const TextFieldCharSequence& oldValue, const TextFieldCharSequence& newValue,
                                                       bool restartImeIfContentChanges, std::optional<bool> textChanged) {
        const TextFieldCharSequence previous = m_Value.Peek();
        m_Value.Set(newValue);
        ++m_Version;
        const bool restart = restartImeIfContentChanges && !oldValue.ContentEquals(newValue.text) && oldValue.composition.has_value();
        const auto listeners = m_ImeListeners;
        for (const auto& [token, l] : listeners) l(oldValue, newValue, restart);
        m_UserCommit = false;
        // dispatchOnTextChanged.
        if (!m_OnTextChanged) return;
        const bool did = textChanged.value_or(previous.Length() != newValue.Length() || !previous.ContentEquals(newValue.text));
        if (!did) return;
        m_InOnTextChanged = true;
        try { m_OnTextChanged(newValue.text); } catch (...) { m_InOnTextChanged = false; throw; }
        m_InOnTextChanged = false;
    }

    void TextFieldState::RecordEditForUndo(const TextFieldCharSequence& pre, const TextFieldCharSequence& post, const ChangeList& changes,
                                           TextFieldEditUndoBehavior b) {
        switch (b) {
            case TextFieldEditUndoBehavior::ClearHistory: m_Undo.ClearHistory(); break;
            case TextFieldEditUndoBehavior::MergeIfPossible: m_Undo.RecordChanges(pre, post, changes, true); break;
            case TextFieldEditUndoBehavior::NeverMerge: m_Undo.RecordChanges(pre, post, changes, false); break;
        }
    }

    int TextFieldState::AddNotifyImeListener(NotifyImeListener l) {
        const int token = ++m_NextListener;
        m_ImeListeners.emplace_back(token, std::move(l));
        return token;
    }
    void TextFieldState::RemoveNotifyImeListener(int token) {
        std::erase_if(m_ImeListeners, [&](const auto& p) { return p.first == token; });
    }

    TextFieldState::Saved TextFieldState::Save() const {
        return { m_Value.Peek().text, m_Value.Peek().selection, m_Undo.Save(), m_Value.Peek().textFieldTextStyles };
    }
    void TextFieldState::Restore(const Saved& s) {
        m_Main = TextFieldBuffer(TextFieldCharSequence(s.text, s.selection, std::nullopt, std::nullopt, {}, s.styles));
        m_Value.Set(TextFieldCharSequence(s.text, s.selection, std::nullopt, std::nullopt, {}, s.styles));
        m_Undo.Restore(s.undo);
        ++m_Version;
    }

    // --- TransformedTextFieldState ------------------------------------------------------------

    TransformedTextFieldState::TransformedTextFieldState(TextFieldState& state, InputTransformationPtr input,
                                                         CodepointTransformationPtr codepoint, OutputTransformationPtr output)
        : m_State(state), m_Input(std::move(input)), m_Codepoint(std::move(codepoint)), m_Output(std::move(output)) {}

    namespace {
        TextRange MapToTransformedWith(TextRange range, const OffsetMappingCalculator& m, const SelectionWedgeAffinity* affinity) {
            TextRange s = m.MapFromSource(range.start);
            TextRange e = range.Collapsed() ? s : m.MapFromSource(range.end);
            const std::optional<WedgeAffinity> sa = affinity ? std::optional(affinity->startAffinity) : std::nullopt;
            const std::optional<WedgeAffinity> ea = range.Collapsed() ? sa : (affinity ? std::optional(affinity->endAffinity) : std::nullopt);
            if (sa && !s.Collapsed()) s = *sa == WedgeAffinity::Start ? TextRange(s.start) : TextRange(s.end);
            if (ea && !e.Collapsed()) e = *ea == WedgeAffinity::Start ? TextRange(e.start) : TextRange(e.end);
            const int lo = std::min(s.Min(), e.Min()), hi = std::max(s.Max(), e.Max());
            return range.Reversed() ? TextRange(hi, lo) : TextRange(lo, hi);
        }
        TextRange MapFromTransformedWith(TextRange range, const OffsetMappingCalculator& m) {
            const TextRange s = m.MapFromDest(range.start);
            const TextRange e = range.Collapsed() ? s : m.MapFromDest(range.end);
            const int lo = std::min(s.Min(), e.Min()), hi = std::max(s.Max(), e.Max());
            return range.Reversed() ? TextRange(hi, lo) : TextRange(lo, hi);
        }
    }

    std::shared_ptr<const TransformedTextFieldState::OutputRaw> TransformedTextFieldState::ComputeOutputRaw() const {
        auto raw = std::make_shared<OutputRaw>();
        raw->source = m_State.Value();
        TextFieldBuffer buffer(raw->source, nullptr, std::nullopt, &raw->mapping);
        m_Output->TransformOutput(buffer);
        // Styles an output transformation adds count as changes.
        if (buffer.Changes().ChangeCount() != 0) raw->buffer = std::move(buffer);
        return raw;
    }

    const std::optional<TransformedTextFieldState::Transformed>& TransformedTextFieldState::OutputTransformed() const {
        if (!m_Output) { m_OutputCache.reset(); return m_OutputCache; }
        const std::shared_ptr<const OutputRaw>& raw = m_OutputRaw.Get();
        if (m_OutputFor == raw.get() && m_OutputAffinity == selectionWedgeAffinity) return m_OutputCache;
        m_OutputFor = raw.get();
        m_OutputAffinity = selectionWedgeAffinity;
        if (!raw->buffer) { m_OutputCache.reset(); return m_OutputCache; }
        const TextFieldCharSequence& value = raw->source;
        Transformed t;
        t.mapping = raw->mapping;
        t.text = raw->buffer->ToTextFieldCharSequence(
            MapToTransformedWith(value.selection, t.mapping, &selectionWedgeAffinity),
            value.composition ? std::optional(MapToTransformedWith(*value.composition, t.mapping, &selectionWedgeAffinity)) : std::nullopt);
        m_OutputCache = std::move(t);
        return m_OutputCache;
    }

    std::shared_ptr<const TransformedTextFieldState::CodepointRaw> TransformedTextFieldState::ComputeCodepointRaw() const {
        auto raw = std::make_shared<CodepointRaw>();
        raw->source = OutputText();
        raw->visual = ToVisualText(raw->source.text, *m_Codepoint, raw->mapping);
        return raw;
    }

    const std::optional<TransformedTextFieldState::Transformed>& TransformedTextFieldState::CodepointTransformed() const {
        if (!m_Codepoint) { m_CodepointCache.reset(); return m_CodepointCache; }
        const std::shared_ptr<const CodepointRaw>& raw = m_CodepointRaw.Get();
        if (m_CodepointFor == raw.get() && m_CodepointAffinity == selectionWedgeAffinity) return m_CodepointCache;
        m_CodepointFor = raw.get();
        m_CodepointAffinity = selectionWedgeAffinity;
        if (!raw->visual) { m_CodepointCache.reset(); return m_CodepointCache; }
        // The same text; its selection as the output affinity maps it now.
        const TextFieldCharSequence source = OutputText();
        Transformed t;
        t.mapping = raw->mapping;
        t.text = TextFieldCharSequence(*raw->visual, MapToTransformedWith(source.selection, t.mapping, &selectionWedgeAffinity),
                                       source.composition ? std::optional(MapToTransformedWith(*source.composition, t.mapping, &selectionWedgeAffinity))
                                                          : std::nullopt);
        m_CodepointCache = std::move(t);
        return m_CodepointCache;
    }

    TextFieldCharSequence TransformedTextFieldState::OutputText() const {
        const auto& out = OutputTransformed();
        return out ? out->text : m_State.Value();
    }
    TextFieldCharSequence TransformedTextFieldState::VisualText() const {
        const auto& cp = CodepointTransformed();
        return cp ? cp->text : OutputText();
    }

    TextRange TransformedTextFieldState::MapToTransformed(int offset) const {
        const auto& out = OutputTransformed();
        const auto& cp = CodepointTransformed();
        const TextRange mid = out ? out->mapping.MapFromSource(offset) : TextRange(offset);
        return cp ? MapToTransformedWith(mid, cp->mapping, &selectionWedgeAffinity) : mid;
    }
    TextRange TransformedTextFieldState::MapToTransformed(TextRange range) const {
        const auto& out = OutputTransformed();
        const auto& cp = CodepointTransformed();
        const TextRange mid = out ? MapToTransformedWith(range, out->mapping, nullptr) : range;
        return cp ? MapToTransformedWith(mid, cp->mapping, &selectionWedgeAffinity) : mid;
    }
    TextRange TransformedTextFieldState::MapFromTransformed(int offset) const {
        const auto& out = OutputTransformed();
        const auto& cp = CodepointTransformed();
        const TextRange mid = cp ? cp->mapping.MapFromDest(offset) : TextRange(offset);
        return out ? MapFromTransformedWith(mid, out->mapping) : mid;
    }
    TextRange TransformedTextFieldState::MapFromTransformed(TextRange range) const {
        const auto& out = OutputTransformed();
        const auto& cp = CodepointTransformed();
        const TextRange mid = cp ? MapFromTransformedWith(range, cp->mapping) : range;
        return out ? MapFromTransformedWith(mid, out->mapping) : mid;
    }

    IndexTransformationType TransformedTextFieldState::IndexTransformation(int index, TextRange* untransformed, TextRange* retransformed) const {
        const TextRange u = MapFromTransformed(index);
        const TextRange r = MapToTransformed(u);
        if (untransformed) *untransformed = u;
        if (retransformed) *retransformed = r;
        if (u.Collapsed() && r.Collapsed()) return IndexTransformationType::Untransformed;
        if (!u.Collapsed() && !r.Collapsed()) return IndexTransformationType::Replacement;
        if (u.Collapsed()) return IndexTransformationType::Insertion;
        return IndexTransformationType::Deletion;
    }

    void TransformedTextFieldState::UpdateWedgeAffinity(TextFieldBuffer& b) {
        if (b.Changes().ChangeCount() > 0 && b.Selection().Collapsed()) selectionWedgeAffinity = {};
    }

    void TransformedTextFieldState::SelectCharsIn(TextRange transformedRange) { SelectUntransformedCharsIn(MapFromTransformed(transformedRange)); }
    void TransformedTextFieldState::SelectUntransformedCharsIn(TextRange r) {
        m_State.EditAsUser(m_Input.get(), [&](TextFieldBuffer& b) { b.SetSelectionCoerced(r.start, r.end); });
    }
    void TransformedTextFieldState::HighlightCharsIn(TextHighlightType type, TextRange transformedRange) {
        const TextRange r = MapFromTransformed(transformedRange);
        m_State.EditAsUser(m_Input.get(), [&](TextFieldBuffer& b) { b.SetHighlight(type, r.Min(), r.Max()); });
    }
    void TransformedTextFieldState::ReplaceAll(std::u16string_view text) {
        m_State.EditAsUser(m_Input.get(), [&](TextFieldBuffer& b) {
            b.Delete(0, b.Length());
            b.Append(text);
            UpdateWedgeAffinity(b);
        });
    }
    void TransformedTextFieldState::SelectAll() {
        m_State.EditAsUser(m_Input.get(), [](TextFieldBuffer& b) { b.SetSelectionCoerced(0, b.Length()); });
    }
    void TransformedTextFieldState::DeleteSelectedText(bool hardware) {
        m_State.EditAsUser(m_Input.get(), [&](TextFieldBuffer& b) {
            const TextRange sel = b.Selection();
            b.Replace(sel.Min(), sel.Max(), std::u16string_view{}, hardware);
            b.SetSelectionCoerced(sel.Min());
            UpdateWedgeAffinity(b);
        }, true, TextFieldEditUndoBehavior::NeverMerge);
    }
    void TransformedTextFieldState::ReplaceText(std::u16string_view text, TextRange range, TextFieldEditUndoBehavior undo,
                                                bool restartIme, bool hardware) {
        m_State.EditAsUser(m_Input.get(), [&](TextFieldBuffer& b) {
            const TextRange sel = MapFromTransformed(range);
            b.Replace(sel.Min(), sel.Max(), text, hardware);
            b.SetSelectionCoerced(sel.Min() + int(text.size()));
            UpdateWedgeAffinity(b);
        }, restartIme, undo);
    }
    void TransformedTextFieldState::ReplaceSelectedText(std::u16string_view text, bool clearComposition, TextFieldEditUndoBehavior undo,
                                                        bool restartIme, bool hardware) {
        m_State.EditAsUser(m_Input.get(), [&](TextFieldBuffer& b) {
            if (clearComposition) b.CommitComposition();
            const TextRange sel = b.Selection();
            b.Replace(sel.Min(), sel.Max(), text, hardware);
            b.SetSelectionCoerced(sel.Min() + int(text.size()));
            UpdateWedgeAffinity(b);
        }, restartIme, undo);
    }
    void TransformedTextFieldState::CollapseSelectionToMax() {
        m_State.EditAsUser(m_Input.get(), [](TextFieldBuffer& b) { b.SetSelectionCoerced(b.Selection().Max()); });
    }
    void TransformedTextFieldState::CollapseSelectionToEnd() {
        m_State.EditAsUser(m_Input.get(), [](TextFieldBuffer& b) { b.SetSelectionCoerced(b.Selection().end); });
    }
    void TransformedTextFieldState::EditUntransformedTextAsUser(const kiln::Function<void(TextFieldBuffer&)>& block, bool restartIme) {
        m_State.EditAsUser(m_Input.get(), [&](TextFieldBuffer& b) { block(b); UpdateWedgeAffinity(b); }, restartIme);
    }

    // --- IME edit commands --------------------------------------------------------------------

    void ImeReplace(TextFieldBuffer& b, int start, int end, std::u16string_view text) {
        const int lo = std::min(start, end), hi = std::max(start, end);
        int i = 0, cMin = lo;
        while (cMin < hi && i < int(text.size()) && text[std::size_t(i)] == b.CharAt(cMin)) { ++i; ++cMin; }
        int j = int(text.size()), cMax = hi;
        while (cMax > cMin && j > i && text[std::size_t(j - 1)] == b.CharAt(cMax - 1)) { --j; --cMax; }
        if (cMin != cMax || i != j) {
            b.Replace(cMin, cMax, text.substr(std::size_t(i), std::size_t(j - i)));
        } else {
            b.CommitComposition();
            b.ClearHighlight();
        }
        b.SetSelection(TextRange(lo + int(text.size())));
    }

    void ImeDelete(TextFieldBuffer& b, int start, int end) {
        b.suggestionSelected = false;
        const auto initial = b.Composition();
        const int lo = std::min(start, end), hi = std::max(start, end);
        b.Delete(lo, hi);
        if (initial) {
            const TextRange adjusted = AdjustTextRange(*initial, lo, hi, 0);
            if (adjusted.Collapsed()) b.CommitComposition();
            else b.SetComposition(adjusted.Min(), adjusted.Max());
        }
    }

    void ImeEditCommandScope::Edit(kiln::Function<void(TextFieldBuffer&)> block) {
        BeginBatchEdit();
        m_Commands.push_back(std::move(block));
        EndBatchEdit();
    }

    bool ImeEditCommandScope::EndBatchEdit() {
        if (m_Depth == 0) return false;
        --m_Depth;
        if (m_Depth == 0 && !m_Commands.empty()) {
            auto commands = std::move(m_Commands);
            m_Commands.clear();
            m_State.EditUntransformedTextAsUser([&](TextFieldBuffer& b) {
                if (!m_State.IsTransformed()) m_Batch = &b;
                for (auto& c : commands) c(b);
            }, false);
            m_Batch = nullptr;
        }
        return m_Depth > 0;
    }

    void ImeEditCommandScope::CommitText(std::u16string_view text, int newCursorPosition, bool suggestion) {
        std::u16string t(text);
        Edit([t, newCursorPosition, suggestion](TextFieldBuffer& b) {
            if (const auto c = b.Composition()) ImeReplace(b, c->start, c->end, t);
            else ImeReplace(b, b.Selection().Min(), b.Selection().Max(), t);
            const int cursor = b.Selection().Min();
            const int inBuffer = newCursorPosition > 0 ? cursor + newCursorPosition - 1 : cursor + newCursorPosition - int(t.size());
            b.suggestionSelected = suggestion;
            b.SetSelection(TextRange(std::clamp(inBuffer, 0, b.Length())));
        });
    }

    void ImeEditCommandScope::SetComposingRegion(int start, int end) {
        Edit([this, start, end](TextFieldBuffer& b) {
            if (b.HasComposition()) b.CommitComposition();
            const TextRange r = MapFromTransformed(TextRange(std::max(start, 0), std::max(end, 0)));
            const int s = std::clamp(r.Min(), 0, b.Length()), e = std::clamp(r.Max(), 0, b.Length());
            if (s < e) b.SetComposition(s, e);
            else if (s > e) b.SetComposition(e, s);
        });
    }

    void ImeEditCommandScope::SetComposingText(std::u16string_view text, int newCursorPosition, const std::vector<PlacedSpan>* annotations, bool suggestion) {
        std::u16string t(text);
        std::optional<std::vector<PlacedSpan>> spans;
        if (annotations) spans = *annotations;
        Edit([t, newCursorPosition, spans, suggestion](TextFieldBuffer& b) {
            const std::vector<PlacedSpan>* a = spans ? &*spans : nullptr;
            if (const auto c = b.Composition()) {
                ImeReplace(b, c->start, c->end, t);
                if (!t.empty()) b.SetComposition(c->start, c->start + int(t.size()), a);
            } else {
                const int start = b.Selection().Min();
                ImeReplace(b, start, b.Selection().Max(), t);
                if (!t.empty()) b.SetComposition(start, start + int(t.size()), a);
            }
            const int cursor = b.Selection().Min();
            const int inBuffer = newCursorPosition > 0 ? cursor + newCursorPosition - 1 : cursor + newCursorPosition - int(t.size());
            b.suggestionSelected = suggestion;
            b.SetSelection(TextRange(std::clamp(inBuffer, 0, b.Length())));
        });
    }

    void ImeEditCommandScope::DeleteSurroundingText(int before, int after) {
        if (before < 0 || after < 0) throw std::invalid_argument("negative delete lengths");
        Edit([this, before, after](TextFieldBuffer& b) {
            const TextRange sel = MapToTransformed(b.Selection());
            const int length = TransformedLength();
            const long long end = std::min<long long>((long long)sel.Max() + after, length);
            const TextRange afterRange = MapFromTransformed(TextRange(sel.Max(), int(end)));
            ImeDelete(b, afterRange.Min(), afterRange.Max());
            const int start = int(std::max<long long>(0, (long long)sel.Min() - before));
            const TextRange beforeRange = MapFromTransformed(TextRange(start, sel.Min()));
            ImeDelete(b, beforeRange.Min(), beforeRange.Max());
        });
    }

    void ImeEditCommandScope::DeleteSurroundingTextInCodePoints(int before, int after) {
        if (before < 0 || after < 0) throw std::invalid_argument("negative delete lengths");
        Edit([before, after](TextFieldBuffer& b) {
            auto pair = [](char16_t hi, char16_t lo) { return hi >= 0xD800 && hi < 0xDC00 && lo >= 0xDC00 && lo < 0xE000; };
            int beforeChars = 0;
            for (int i = 0; i < before; ++i) {
                ++beforeChars;
                if (b.Selection().Min() > beforeChars) {
                    if (pair(b.CharAt(b.Selection().Min() - beforeChars - 1), b.CharAt(b.Selection().Min() - beforeChars))) ++beforeChars;
                } else {
                    beforeChars = b.Selection().Min();
                    break;
                }
            }
            int afterChars = 0;
            for (int i = 0; i < after; ++i) {
                ++afterChars;
                if (b.Selection().Max() + afterChars < b.Length()) {
                    if (pair(b.CharAt(b.Selection().Max() + afterChars - 1), b.CharAt(b.Selection().Max() + afterChars))) ++afterChars;
                } else {
                    afterChars = b.Length() - b.Selection().Max();
                    break;
                }
            }
            ImeDelete(b, b.Selection().Max(), b.Selection().Max() + afterChars);
            ImeDelete(b, b.Selection().Min() - beforeChars, b.Selection().Min());
        });
    }

    void ImeEditCommandScope::FinishComposingText() { Edit([](TextFieldBuffer& b) { b.CommitComposition(); }); }

    void ImeEditCommandScope::SetSelection(int start, int end) {
        Edit([this, start, end](TextFieldBuffer& b) {
            const TextRange space = MapToTransformed(TextRange(0, b.Length()));
            const int s = std::clamp(start, space.Min(), space.Max()), e = std::clamp(end, space.Min(), space.Max());
            b.SetSelection(MapFromTransformed(TextRange(s, e)));
        });
    }

}
