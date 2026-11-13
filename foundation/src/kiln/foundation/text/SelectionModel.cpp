// The selection model (Selection.kt, SelectionLayout.kt, SelectionAdjustment.kt,
// MultiWidgetSelectionDelegate.kt, SelectionRegistrarImpl.kt, TextSelectionDelegate.kt).

#include "kiln/foundation/text/Selection.h"
#include <kiln/Function.h>

#include "kiln/TextBackend.h"
#include "kiln/foundation/text/TextFieldState.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation::selection {

    // --- coordinates ---------------------------------------------------------------------------

    bool Coordinates::IsAttached() const {
        return node && !alive.expired() && node->GetOwner() && (node->IsPlaced() || !node->Parent());
    }
    Mat3 Coordinates::ToRoot() const { return node->SpaceToRoot(at); }
    ui::Size Coordinates::GetSize() const {
        ui::Size s;
        node->SpaceToRoot(at, &s);
        return s;
    }

    Offset LocalPositionOf(const Coordinates& target, const Coordinates& source, Offset p) {
        const Offset root = ui::LayoutNode::MapPoint(source.ToRoot(), p);
        return ui::LayoutNode::UnmapPoint(target.ToRoot(), root).value_or(Offset{ NAN, NAN });
    }

    Offset PositionInRoot(const Coordinates& c, Offset p) { return ui::LayoutNode::MapPoint(c.ToRoot(), p); }

    Rect VisibleBounds(const Coordinates& c) {
        // boundsInWindow: the bounds through the ancestors' clips and the window, mapped back.
        Rect r = c.node->RectInRoot(std::nullopt, c.at);
        for (const ui::LayoutNode* n = c.node->Parent(); n; n = n->Parent())
            for (const ui::ModifierNode* m : n->ModifierNodes())
                if (m->ClipsChildren()) r = r.Intersect(n->RectInRoot(std::nullopt, m));
        if (const ui::Owner* o = c.node->GetOwner()) {
            const ui::IntSize w = const_cast<ui::Owner*>(o)->Root().Size();
            r = r.Intersect({ 0, 0, float(w.width), float(w.height) });
        }
        const Mat3 m = c.ToRoot();
        const Offset tl = ui::LayoutNode::UnmapPoint(m, { r.left, r.top }).value_or(Offset{});
        const Offset br = ui::LayoutNode::UnmapPoint(m, { r.right, r.bottom }).value_or(Offset{});
        return { tl.x, tl.y, br.x, br.y };
    }

    namespace {
        std::pair<int, int> ParagraphBoundary(const SelectableInfo& info, int index) {
            const std::u16string t = ToUtf16(info.layout->LayoutInput().text.Text());
            const int n = int(t.size());
            int start = 0;
            for (int i = std::min(index, n); i >= 1; --i)
                if (t[std::size_t(i - 1)] == u'\n') { start = i; break; }
            int end = n;
            for (int i = std::max(index, 0); i < n; ++i)
                if (t[std::size_t(i)] == u'\n') { end = i; break; }
            return { start, end };
        }

        int CharBreak(const SelectableInfo& info, int offset, bool following) {
            const std::u16string t = ToUtf16(info.layout->LayoutInput().text.Text());
            return info.backend ? info.backend->CharacterBreak(t, offset, following) : TextBackend::SurrogateCharacterBreak(t, offset, following);
        }

        Direction Resolve2d(Direction x, Direction y) {
            if (y == Direction::Before) return Direction::Before;
            if (y == Direction::After) return Direction::After;
            return x;
        }
    }

    ResolvedTextDirection TextDirectionForOffset(const TextLayoutResult& layout, int offset) {
        const int length = layout.LayoutInput().text.Length();
        bool emptyLine = length == 0;
        if (!emptyLine) {
            const int line = layout.LineForOffset(offset);
            emptyLine = (offset == 0 || line != layout.LineForOffset(offset - 1)) && (offset == length || line != layout.LineForOffset(offset + 1));
        }
        return emptyLine ? layout.ParagraphDirection(offset) : layout.BidiRunDirection(offset);
    }

    // --- Selection -------------------------------------------------------------------------------

    Selection Selection::Merge(const std::optional<Selection>& other) const {
        if (!other) return *this;
        if (handlesCrossed || other->handlesCrossed)
            return { other->handlesCrossed ? other->start : other->end, handlesCrossed ? end : start, true };
        Selection s = *this;
        s.end = other->end;
        return s;
    }

    // --- SelectableInfo / SelectionLayout ------------------------------------------------------

    CrossStatus SelectableInfo::RawCrossStatus() const {
        if (rawStartHandleOffset < rawEndHandleOffset) return CrossStatus::NotCrossed;
        if (rawStartHandleOffset > rawEndHandleOffset) return CrossStatus::Crossed;
        return CrossStatus::Collapsed;
    }
    AnchorInfo SelectableInfo::AnchorForOffset(int offset) const { return { TextDirectionForOffset(*layout, offset), offset, selectableId }; }
    Selection SelectableInfo::MakeSingleLayoutSelection(int start, int end) const { return { AnchorForOffset(start), AnchorForOffset(end), start > end }; }

    CrossStatus SelectionLayout::Cross() const {
        if (infos.size() == 1) {
            const CrossStatus raw = infos[0].RawCrossStatus();
            if (raw != CrossStatus::Collapsed) return raw;
            if (startSlot < endSlot) return CrossStatus::NotCrossed;
            if (startSlot > endSlot) return CrossStatus::Crossed;
            return raw;
        }
        if (startSlot < endSlot) return CrossStatus::NotCrossed;
        if (startSlot > endSlot) return CrossStatus::Crossed;
        if (startSlot % 2 == 0) return CrossStatus::Collapsed;   // both between texts
        return infos[std::size_t(startSlot / 2)].RawCrossStatus();
    }

    int SelectionLayout::SlotToIndex(int slot, bool isStartSlot) const {
        const CrossStatus c = Cross();
        const bool roundDown = c == CrossStatus::Collapsed ? true : c == CrossStatus::NotCrossed ? !isStartSlot : isStartSlot;
        return (slot - (roundDown ? 1 : 0)) / 2;
    }

    const SelectableInfo& SelectionLayout::StartInfo() const {
        return infos.size() == 1 ? infos[0] : infos[std::size_t(SlotToIndex(startSlot, true))];
    }
    const SelectableInfo& SelectionLayout::EndInfo() const {
        return infos.size() == 1 ? infos[0] : infos[std::size_t(SlotToIndex(endSlot, false))];
    }

    void SelectionLayout::ForEachMiddleInfo(const kiln::Function<void(const SelectableInfo&)>& block) const {
        if (infos.size() == 1) return;
        const int minIndex = indexById.at(FirstInfo().selectableId), maxIndex = indexById.at(LastInfo().selectableId);
        for (int i = minIndex + 1; i < maxIndex; ++i) block(infos[std::size_t(i)]);
    }

    const SelectableInfo* SelectionLayout::InfoForSelectable(std::int64_t id) const {
        auto it = indexById.find(id);
        return it == indexById.end() ? nullptr : &infos[std::size_t(it->second)];
    }

    bool SelectionLayout::ShouldRecomputeSelection(const std::optional<SelectionLayout>& other) const {
        if (!previousSelection || !other || (other->infos.size() == 1) != (infos.size() == 1)) return true;
        if (isStartHandle != other->isStartHandle || startSlot != other->startSlot || endSlot != other->endSlot) return true;
        if (infos.size() != other->infos.size()) return true;
        for (std::size_t i = 0; i < infos.size(); ++i)
            if (infos[i].ShouldRecomputeSelection(other->infos[i])) return true;
        return false;
    }

    namespace {
        // createSubSelections: the boundary selectables keep their offsets, the ones between are
        // fully selected.
        template<typename T>
        SubSelections CreateSubSelectionsOf(const Selection& selection, const std::vector<T>& items, const kiln::Function<std::int64_t(const T&)>& getId,
                                            const kiln::Function<std::optional<Selection>(const T&, bool)>& getSelectAll,
                                            const kiln::Function<std::optional<Selection>(const T&, bool, int, bool)>& boundary) {
            const std::int64_t startId = selection.start.selectableId, endId = selection.end.selectableId;
            if (startId == endId) return { { startId, selection } };
            int startIndex = -1, endIndex = -1;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (startIndex < 0 && getId(items[i]) == startId) startIndex = int(i);
                if (endIndex < 0 && getId(items[i]) == endId) endIndex = int(i);
            }
            if (startIndex == -1 || endIndex == -1) return {};
            const bool crossed = startIndex > endIndex;
            SubSelections result;
            for (int i = std::min(startIndex, endIndex); i <= std::max(startIndex, endIndex); ++i) {
                const T& item = items[std::size_t(i)];
                const std::int64_t id = getId(item);
                std::optional<Selection> sub = id == startId ? boundary(item, true, selection.start.offset, crossed)
                                             : id == endId   ? boundary(item, false, selection.end.offset, crossed)
                                                             : getSelectAll(item, crossed);
                if (sub) result[id] = *sub;
            }
            return result;
        }
    }

    SubSelections SelectionLayout::CreateSubSelections(const Selection& selection) const {
        if (infos.size() == 1) {
            Selection s = selection;
            if ((!s.handlesCrossed && s.start.offset > s.end.offset) || (s.handlesCrossed && s.start.offset <= s.end.offset))
                s.handlesCrossed = !s.handlesCrossed;
            return { { infos[0].selectableId, s } };
        }
        return CreateSubSelectionsOf<SelectableInfo>(
            selection, infos, [](const SelectableInfo& i) { return i.selectableId; },
            [&](const SelectableInfo& info, bool) -> std::optional<Selection> {
                return selection.handlesCrossed ? info.MakeSingleLayoutSelection(info.TextLength(), 0) : info.MakeSingleLayoutSelection(0, info.TextLength());
            },
            [&](const SelectableInfo& info, bool isStart, int offset, bool) -> std::optional<Selection> {
                const bool crossed = selection.handlesCrossed;
                if (isStart) return crossed ? info.MakeSingleLayoutSelection(offset, 0) : info.MakeSingleLayoutSelection(offset, info.TextLength());
                return crossed ? info.MakeSingleLayoutSelection(info.TextLength(), offset) : info.MakeSingleLayoutSelection(0, offset);
            });
    }

    bool IsCollapsed(const std::optional<Selection>& selection, const SelectionLayout* layout) {
        if (!selection || !layout) return true;
        if (selection->start.selectableId == selection->end.selectableId) return selection->start.offset == selection->end.offset;
        const AnchorInfo& maxAnchor = selection->handlesCrossed ? selection->start : selection->end;
        if (maxAnchor.offset != 0) return false;
        const AnchorInfo& minAnchor = selection->handlesCrossed ? selection->end : selection->start;
        if (layout->FirstInfo().TextLength() != minAnchor.offset) return false;
        bool allEmpty = true;
        layout->ForEachMiddleInfo([&](const SelectableInfo& i) { if (i.TextLength() > 0) allEmpty = false; });
        return allEmpty;
    }

    // --- adjustment ----------------------------------------------------------------------------

    namespace {
        using BoundaryFn = kiln::Function<std::pair<int, int>(const SelectableInfo&, int)>;

        AnchorInfo AnchorOnBoundary(const SelectableInfo& info, bool crossed, bool isStart, int slot, const BoundaryFn& boundary) {
            const int offset = isStart ? info.rawStartHandleOffset : info.rawEndHandleOffset;
            if (slot != info.slot) return info.AnchorForOffset(offset);
            const auto range = boundary(info, offset);
            return info.AnchorForOffset(isStart != crossed ? range.first : range.second);
        }

        Selection AdjustToBoundaries(const SelectionLayout& layout, const BoundaryFn& boundary) {
            const bool crossed = layout.Cross() == CrossStatus::Crossed;
            return { AnchorOnBoundary(layout.StartInfo(), crossed, true, layout.startSlot, boundary),
                     AnchorOnBoundary(layout.EndInfo(), crossed, false, layout.endSlot, boundary), crossed };
        }

        AnchorInfo ChangeOffset(const AnchorInfo& a, const SelectableInfo& info, int newOffset) {
            AnchorInfo r = a;
            r.offset = newOffset;
            r.direction = info.layout->BidiRunDirection(newOffset);
            return r;
        }

        Selection ExpandOneChar(const Selection& s, const SelectionLayout& layout) {
            const SelectableInfo& info = layout.CurrentInfo();
            const int offset = info.rawStartHandleOffset;
            const int lastOffset = info.TextLength();
            Selection r = s;
            if (offset == 0) {
                const int following = CharBreak(info, 0, true);
                if (layout.isStartHandle) { r.start = ChangeOffset(s.start, info, following); r.handlesCrossed = true; }
                else { r.end = ChangeOffset(s.end, info, following); r.handlesCrossed = false; }
            } else if (offset == lastOffset) {
                const int preceding = CharBreak(info, lastOffset, false);
                if (layout.isStartHandle) { r.start = ChangeOffset(s.start, info, preceding); r.handlesCrossed = false; }
                else { r.end = ChangeOffset(s.end, info, preceding); r.handlesCrossed = true; }
            } else {
                const bool crossed = layout.previousSelection && layout.previousSelection->handlesCrossed;
                const int newOffset = layout.isStartHandle != crossed ? CharBreak(info, offset, false) : CharBreak(info, offset, true);
                if (layout.isStartHandle) r.start = ChangeOffset(s.start, info, newOffset);
                else r.end = ChangeOffset(s.end, info, newOffset);
                r.handlesCrossed = crossed;
            }
            return r;
        }

        Selection EnsureAtLeastOneChar(const Selection& s, const SelectionLayout& layout) {
            if (!IsCollapsed(s, &layout)) return s;
            if (layout.Size() > 1 || !layout.previousSelection || layout.CurrentInfo().TextLength() == 0) return s;
            return ExpandOneChar(s, layout);
        }

        bool IsExpanding(const SelectableInfo& info, int currentRawOffset, bool isStart) {
            if (info.rawPreviousHandleOffset == -1) return true;
            if (currentRawOffset == info.rawPreviousHandleOffset) return false;
            const bool crossed = info.RawCrossStatus() == CrossStatus::Crossed;
            return isStart != crossed ? currentRawOffset < info.rawPreviousHandleOffset : currentRawOffset > info.rawPreviousHandleOffset;
        }

        AnchorInfo SnapToWordBoundary(const SelectableInfo& info, int currentLine, int currentOffset, int otherOffset, bool isStart, bool crossed) {
            const TextLayoutResult& t = *info.layout;
            const auto word = t.WordBoundary(currentOffset);
            const int lines = t.LineCount();
            const int start = t.LineForOffset(word.first) == currentLine ? word.first
                            : currentLine >= lines ? t.LineStart(lines - 1) : t.LineStart(currentLine);
            const int end = t.LineForOffset(word.second) == currentLine ? word.second
                          : currentLine >= lines ? t.LineEnd(lines - 1) : t.LineEnd(currentLine);
            if (start == otherOffset) return info.AnchorForOffset(end);
            if (end == otherOffset) return info.AnchorForOffset(start);
            const int result = isStart != crossed ? (currentOffset <= end ? start : end) : (currentOffset >= start ? end : start);
            return info.AnchorForOffset(result);
        }

        AnchorInfo UpdateSelectionBoundary(const SelectionLayout& layout, const SelectableInfo& info, const AnchorInfo& previousAnchor) {
            const bool isStart = layout.isStartHandle;
            const int currentRawOffset = isStart ? info.rawStartHandleOffset : info.rawEndHandleOffset;
            const int currentSlot = isStart ? layout.startSlot : layout.endSlot;
            if (currentSlot != info.slot) return info.AnchorForOffset(currentRawOffset);
            const int currentRawLine = info.layout->LineForOffset(currentRawOffset);
            const int otherRawOffset = isStart ? info.rawEndHandleOffset : info.rawStartHandleOffset;
            auto snapped = [&] { return SnapToWordBoundary(info, currentRawLine, currentRawOffset, otherRawOffset, isStart, layout.Cross() == CrossStatus::Crossed); };
            if (info.selectableId != previousAnchor.selectableId) return snapped();
            if (currentRawOffset == info.rawPreviousHandleOffset) return previousAnchor;
            if (currentRawLine != info.layout->LineForOffset(info.rawPreviousHandleOffset)) return snapped();
            const int previousOffset = previousAnchor.offset;
            const auto previousWord = info.layout->WordBoundary(previousOffset);
            if (!IsExpanding(info, currentRawOffset, isStart)) return info.AnchorForOffset(currentRawOffset);
            if (previousOffset == previousWord.first || previousOffset == previousWord.second) return snapped();
            return info.AnchorForOffset(currentRawOffset);
        }
    }

    Selection Adjust(Adjustment adjustment, const SelectionLayout& layout) {
        switch (adjustment) {
            case Adjustment::None:
                return { layout.StartInfo().AnchorForOffset(layout.StartInfo().rawStartHandleOffset),
                         layout.EndInfo().AnchorForOffset(layout.EndInfo().rawEndHandleOffset), layout.Cross() == CrossStatus::Crossed };
            case Adjustment::Character:
                return EnsureAtLeastOneChar(Adjust(Adjustment::None, layout), layout);
            case Adjustment::Word:
                return AdjustToBoundaries(layout, [](const SelectableInfo& i, int o) { return i.layout->WordBoundary(o); });
            case Adjustment::Paragraph:
                return AdjustToBoundaries(layout, [](const SelectableInfo& i, int o) { return ParagraphBoundary(i, o); });
            case Adjustment::CharacterWithWordAccelerate: {
                if (!layout.previousSelection) return Adjust(Adjustment::Word, layout);
                const Selection& previous = *layout.previousSelection;
                AnchorInfo previousAnchor, newAnchor, startAnchor, endAnchor;
                if (layout.isStartHandle) {
                    previousAnchor = previous.start;
                    newAnchor = UpdateSelectionBoundary(layout, layout.StartInfo(), previousAnchor);
                    startAnchor = newAnchor;
                    endAnchor = previous.end;
                } else {
                    previousAnchor = previous.end;
                    newAnchor = UpdateSelectionBoundary(layout, layout.EndInfo(), previousAnchor);
                    startAnchor = previous.start;
                    endAnchor = newAnchor;
                }
                if (newAnchor == previousAnchor) return previous;
                const bool crossed = layout.Cross() == CrossStatus::Crossed ||
                                     (layout.Cross() == CrossStatus::Collapsed && startAnchor.offset > endAnchor.offset);
                return EnsureAtLeastOneChar({ startAnchor, endAnchor, crossed }, layout);
            }
        }
        return Adjust(Adjustment::None, layout);
    }

    // --- SelectionLayoutBuilder ----------------------------------------------------------------

    std::optional<SelectionLayout> SelectionLayoutBuilder::Build() const {
        if (m_Infos.empty()) return std::nullopt;
        const int lastSlot = m_CurrentSlot + 1;   // the empty slot after the last text
        SelectionLayout l;
        l.infos = m_Infos;
        l.indexById = m_IndexById;
        l.startSlot = m_StartSlot == -1 ? lastSlot : m_StartSlot;
        l.endSlot = m_EndSlot == -1 ? lastSlot : m_EndSlot;
        l.isStartHandle = isStartHandle;
        l.previousSelection = previousSelection;
        return l;
    }

    int SelectionLayoutBuilder::UpdateSlot(int slot, Direction x, Direction y) const {
        if (slot != -1) return slot;
        switch (Resolve2d(x, y)) {
            case Direction::Before: return m_CurrentSlot - 1;
            case Direction::On: return m_CurrentSlot;
            case Direction::After: return slot;
        }
        return slot;
    }

    void SelectionLayoutBuilder::AppendInfo(std::int64_t id, int rawStart, Direction startX, Direction startY, int rawEnd, Direction endX,
                                            Direction endY, int rawPrevious, std::shared_ptr<const TextLayoutResult> layout, TextBackend* backend) {
        m_CurrentSlot += 2;
        SelectableInfo info;
        info.selectableId = id;
        info.slot = m_CurrentSlot;
        info.rawStartHandleOffset = rawStart;
        info.rawEndHandleOffset = rawEnd;
        info.rawPreviousHandleOffset = rawPrevious;
        info.layout = std::move(layout);
        info.backend = backend;
        m_StartSlot = UpdateSlot(m_StartSlot, startX, startY);
        m_EndSlot = UpdateSlot(m_EndSlot, endX, endY);
        m_IndexById[id] = int(m_Infos.size());
        m_Infos.push_back(std::move(info));
    }

    namespace {
        Direction XDirection(Offset p, const Rect& b) { return p.x < b.left ? Direction::Before : p.x > b.right ? Direction::After : Direction::On; }
        Direction YDirection(Offset p, const Rect& b) { return p.y < b.top ? Direction::Before : p.y > b.bottom ? Direction::After : Direction::On; }
        int OffsetForPosition(Offset p, const TextLayoutResult& t) {
            if (p.y <= 0) return 0;
            if (p.y >= t.Paragraphs().Height()) return t.LayoutInput().text.Length();
            return t.OffsetForPosition(p);
        }
    }

    void SelectionLayoutBuilder::AppendSelectableInfo(std::shared_ptr<const TextLayoutResult> layout, TextBackend* backend, Offset localPosition,
                                                      int givenRawPrevious, std::optional<Offset> previousHandle, std::int64_t selectableId, bool isLast) {
        const Rect bounds{ 0, 0, float(layout->Size().width), float(layout->Size().height) };
        const Direction currentX = XDirection(localPosition, bounds), currentY = YDirection(localPosition, bounds);
        auto byId = [&](std::int64_t anchorId) {
            const int c = compareIds(anchorId, selectableId);
            return c < 0 ? Direction::Before : c > 0 ? Direction::After : Direction::On;
        };
        auto otherDirectionFor = [&](bool forStartSlot) {
            if (!previousSelection) return Resolve2d(currentX, currentY);
            const AnchorInfo& anchor = forStartSlot ? previousSelection->start : previousSelection->end;
            const Direction d = byId(anchor.selectableId);
            if (d != Direction::On || !allowSelectionBetweenSelectables) return d;
            if (!previousLayout) return Direction::On;
            const int handleSlot = forStartSlot ? previousLayout->startSlot : previousLayout->endSlot;
            const int selectableSlot = (forStartSlot ? previousLayout->StartInfo() : previousLayout->EndInfo()).slot;
            return handleSlot < selectableSlot ? Direction::Before : handleSlot > selectableSlot ? Direction::After : Direction::On;
        };
        Direction other, startX, startY, endX, endY;
        if (isStartHandle) {
            other = otherDirectionFor(false);
            startX = currentX; startY = currentY; endX = other; endY = other;
        } else {
            other = otherDirectionFor(true);
            startX = other; startY = other; endX = currentX; endY = currentY;
        }
        const Direction current = Resolve2d(currentX, currentY);
        if (allowSelectionBetweenSelectables) {
            if (current == Direction::After && other == Direction::After && !isLast) return;
            if (current == Direction::Before && other == Direction::Before && !IsEmpty()) return;
        } else if (!(current == Direction::On || current != other)) {
            return;
        }
        const int length = layout->LayoutInput().text.Length();
        auto previousAdjusted = [&](const AnchorInfo& a) {
            const int c = compareIds(a.selectableId, selectableId);
            return c < 0 ? 0 : c > 0 ? length : a.offset;
        };
        int rawStart, rawEnd;
        if (isStartHandle) {
            rawStart = OffsetForPosition(localPosition, *layout);
            rawEnd = previousSelection ? previousAdjusted(previousSelection->end) : rawStart;
        } else {
            rawEnd = OffsetForPosition(localPosition, *layout);
            rawStart = previousSelection ? previousAdjusted(previousSelection->start) : rawEnd;
        }
        const int rawPrevious = givenRawPrevious != -1 ? givenRawPrevious : !previousHandle ? -1 : OffsetForPosition(*previousHandle, *layout);
        AppendInfo(selectableId, rawStart, startX, startY, rawEnd, endX, endY, rawPrevious, std::move(layout), backend);
    }

    // --- Selectable ------------------------------------------------------------------------------

    std::optional<Coordinates> Selectable::GetLayoutCoordinates() const {
        auto c = coordinatesCallback ? coordinatesCallback() : std::nullopt;
        if (!c || !c->IsAttached()) return std::nullopt;
        return c;
    }

    void Selectable::AppendSelectableInfoToBuilder(SelectionLayoutBuilder& builder, bool isLast, const SelectionLayout* previousLayout) {
        const auto coords = GetLayoutCoordinates();
        const auto layout = layoutCallback ? layoutCallback() : nullptr;
        if (!coords || !layout) return;
        const Offset relative = LocalPositionOf(builder.containerCoordinates, *coords, {});
        const Offset local = builder.currentPosition - relative;
        // isSelectionAutoScrollEnabled: the handle's previous raw offset from the last layout.
        int rawPrevious = -1;
        if (previousLayout)
            if (const SelectableInfo* info = previousLayout->InfoForSelectable(selectableId))
                rawPrevious = builder.isStartHandle ? info->rawStartHandleOffset : info->rawEndHandleOffset;
        std::optional<Offset> localPrevious;
        if (builder.previousHandlePosition) localPrevious = *builder.previousHandlePosition - relative;
        builder.AppendSelectableInfo(layout, backend, local, rawPrevious, localPrevious, selectableId, isLast);
    }

    std::optional<Selection> Selectable::GetSelectAllSelection() const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return std::nullopt;
        const int end = t->LayoutInput().text.Length();
        return Selection{ { t->BidiRunDirection(0), 0, selectableId }, { t->BidiRunDirection(std::max(end - 1, 0)), end, selectableId }, false };
    }

    int Selectable::GetLastVisibleOffset() const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return 0;
        if (m_LastVisibleFor != t.get()) {
            int lastLine;
            const float height = float(t->Size().height);
            if (!t->DidOverflowHeight() || t->Paragraphs().DidExceedMaxLines()) {
                lastLine = t->LineCount() - 1;
            } else {
                int v = std::min(t->LineForVerticalPosition(height), t->LineCount() - 1);
                while (v >= 0 && t->LineTop(v) >= height) --v;
                lastLine = std::max(v, 0);
            }
            m_LastVisible = t->LineEnd(lastLine, true);
            m_LastVisibleFor = t.get();
        }
        return m_LastVisible;
    }

    std::optional<Offset> Selectable::GetHandlePosition(const Selection& selection, bool isStartHandle) const {
        if ((isStartHandle && selection.start.selectableId != selectableId) || (!isStartHandle && selection.end.selectableId != selectableId)) return std::nullopt;
        if (!GetLayoutCoordinates()) return std::nullopt;
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return std::nullopt;
        const int offset = std::clamp(isStartHandle ? selection.start.offset : selection.end.offset, 0, GetLastVisibleOffset());
        // getSelectionHandleCoordinates: the caret x on the line's bottom.
        const int line = t->LineForOffset(offset);
        if (line >= t->LineCount()) return std::nullopt;
        const bool crossed = selection.handlesCrossed;
        const int check = (isStartHandle && !crossed) || (!isStartHandle && crossed) ? offset : std::max(offset - 1, 0);
        const bool primary = t->BidiRunDirection(check) == t->ParagraphDirection(offset);
        const float x = std::clamp(t->HorizontalPosition(offset, primary), 0.0f, float(t->Size().width));
        const float y = std::clamp(t->LineBottom(line), 0.0f, float(t->Size().height));
        return Offset{ x, y };
    }

    AnnotatedString Selectable::GetText() const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        return t ? t->LayoutInput().text : AnnotatedString();
    }

    RectF Selectable::GetBoundingBox(int offset) const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return {};
        const int length = t->LayoutInput().text.Length();
        if (length < 1) return {};
        return t->BoundingBox(std::clamp(offset, 0, length - 1));
    }

    float Selectable::GetLineLeft(int offset) const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return -1;
        const int line = t->LineForOffset(offset);
        return line >= t->LineCount() ? -1 : t->LineLeft(line);
    }
    float Selectable::GetLineRight(int offset) const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return -1;
        const int line = t->LineForOffset(offset);
        return line >= t->LineCount() ? -1 : t->LineRight(line);
    }
    float Selectable::GetCenterYForOffset(int offset) const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return -1;
        const int line = t->LineForOffset(offset);
        if (line >= t->LineCount()) return -1;
        const float top = t->LineTop(line), bottom = t->LineBottom(line);
        return (bottom - top) / 2 + top;
    }
    std::pair<int, int> Selectable::GetRangeOfLineContaining(int offset) const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return { 0, 0 };
        const int visible = GetLastVisibleOffset();
        if (visible < 1) return { 0, 0 };
        const int line = t->LineForOffset(std::clamp(offset, 0, visible - 1));
        return { t->LineStart(line), t->LineEnd(line, true) };
    }
    float Selectable::GetLineHeight(int offset) const {
        const auto t = layoutCallback ? layoutCallback() : nullptr;
        if (!t) return 0;
        const int line = t->LineForOffset(offset);
        return t->LineBottom(line) - t->LineTop(line);
    }

    // --- SelectionRegistrar ----------------------------------------------------------------------

    std::shared_ptr<Selectable> SelectionRegistrar::Subscribe(std::shared_ptr<Selectable> s) {
        if (s->selectableId == kInvalidSelectableId || m_Map.count(s->selectableId)) return s;
        m_Map[s->selectableId] = s.get();
        m_Selectables.push_back(s);
        m_Sorted = false;
        return s;
    }

    void SelectionRegistrar::Unsubscribe(const Selectable& s) {
        auto it = m_Map.find(s.selectableId);
        if (it == m_Map.end() || it->second != &s) return;
        const std::int64_t id = s.selectableId;
        m_Map.erase(it);
        std::erase_if(m_Selectables, [&](const auto& p) { return p.get() == &s; });
        if (afterSelectableUnsubscribe) afterSelectableUnsubscribe(id);
    }

    std::int64_t SelectionRegistrar::NextSelectableId() {
        std::int64_t id = m_NextId++;
        while (id == kInvalidSelectableId) id = m_NextId++;
        return id;
    }

    Selectable* SelectionRegistrar::Find(std::int64_t id) const {
        auto it = m_Map.find(id);
        return it == m_Map.end() ? nullptr : it->second;
    }

    namespace {
        // inARow: vertically aligned by half a height and horizontally distinct.
        bool InARow(Offset aTL, Offset aBR, Offset bTL, Offset bBR) {
            const float heightA = aBR.y - aTL.y, widthA = aBR.x - aTL.x, heightB = bBR.y - bTL.y, widthB = bBR.x - bTL.x;
            const float vert = std::max(0.0f, std::min(aBR.y, bBR.y) - std::max(aTL.y, bTL.y));
            const float horz = std::max(0.0f, std::min(aBR.x, bBR.x) - std::max(aTL.x, bTL.x));
            return (vert >= heightA * 0.5f || vert >= heightB * 0.5f) && (horz < widthA * 0.5f && horz < widthB * 0.5f);
        }
    }

    const std::vector<std::shared_ptr<Selectable>>& SelectionRegistrar::Sort(const Coordinates& container) {
        if (!m_Sorted) {
            auto box = [&](const Selectable& s) -> std::pair<Offset, Offset> {
                const auto c = s.GetLayoutCoordinates();
                if (!c) return { {}, {} };
                const Offset tl = LocalPositionOf(container, *c, {});
                const ui::Size size = c->GetSize();
                return { tl, { tl.x + size.width, tl.y + size.height } };
            };
            std::stable_sort(m_Selectables.begin(), m_Selectables.end(), [&](const auto& a, const auto& b) {
                const auto [aTL, aBR] = box(*a);
                const auto [bTL, bBR] = box(*b);
                return InARow(aTL, aBR, bTL, bBR) ? aTL.x < bTL.x : aTL.y < bTL.y;
            });
            m_Sorted = true;
        }
        return m_Selectables;
    }

    void SelectionRegistrar::NotifyPositionChange(std::int64_t id) {
        m_Sorted = false;
        if (onPositionChange) onPositionChange(id);
    }
    void SelectionRegistrar::NotifySelectionUpdateStart(const Coordinates& c, Offset start, Adjustment a, bool touch) {
        if (onSelectionUpdateStart) onSelectionUpdateStart(touch, c, start, a);
    }
    void SelectionRegistrar::NotifySelectionUpdateSelectAll(std::int64_t id, bool touch) {
        if (onSelectionUpdateSelectAll) onSelectionUpdateSelectAll(touch, id);
    }
    bool SelectionRegistrar::NotifySelectionUpdate(const Coordinates& c, Offset newPosition, Offset previousPosition, bool isStartHandle, Adjustment a, bool touch) {
        return onSelectionUpdate ? onSelectionUpdate(touch, c, newPosition, previousPosition, isStartHandle, a) : true;
    }
    void SelectionRegistrar::NotifySelectionUpdateEnd() { if (onSelectionUpdateEnd) onSelectionUpdateEnd(); }
    void SelectionRegistrar::NotifySelectableChange(std::int64_t id) { if (onSelectableChange) onSelectableChange(id); }

}
