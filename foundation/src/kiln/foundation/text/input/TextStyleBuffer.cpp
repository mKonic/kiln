#include "kiln/foundation/text/input/TextStyleBuffer.h"

#include <algorithm>
#include <climits>
#include <stdexcept>

namespace kiln::foundation {

    namespace {
        constexpr int kDefaultGapLength = 1000;

        // intersect: [lStart, lEnd) and [rStart, rEnd) overlap, or an empty one sits on the
        // other's start.
        bool Intersect(int lStart, int lEnd, int rStart, int rEnd) {
            return (((lStart == lEnd) || (rStart == rEnd)) && (lStart == rStart)) || ((lStart < rEnd) && (rStart < lEnd));
        }
    }

    TextStyleBuffer::TextStyleBuffer(const TextStyleBuffer& source, bool mutableBuffer)
        : m_Entries(source.m_Entries), m_NextId(source.m_NextId), m_GapStart(source.m_GapStart), m_GapEnd(source.m_GapEnd), m_Mutable(mutableBuffer) {}

    void TextStyleBuffer::RequireMutable() const {
        if (!m_Mutable) throw std::logic_error("This TextStyleBuffer is immutable");
    }

    void TextStyleBuffer::SyncTo(const TextStyleBuffer& other) {
        RequireMutable();
        m_GapStart = other.m_GapStart;
        m_GapEnd = other.m_GapEnd;
        m_Entries = other.m_Entries;
        m_NextId = other.m_NextId;
    }

    const TextStyleBuffer::Entry* TextStyleBuffer::Find(int handle) const {
        for (const Entry& e : m_Entries) if (e.id == handle) return &e;
        return nullptr;
    }
    TextStyleBuffer::Entry* TextStyleBuffer::Find(int handle) {
        for (Entry& e : m_Entries) if (e.id == handle) return &e;
        return nullptr;
    }

    int TextStyleBuffer::AddStyle(text::Annotation style, Interval interval) {
        RequireMutable();
        const Interval inBuffer{ OriginalIndexToGapBuffer(interval.start, true, interval.startExpands),
                                 OriginalIndexToGapBuffer(interval.end, false, interval.endExpands), interval.startExpands, interval.endExpands };
        // addInterval: an empty interval is not added.
        if (inBuffer.start >= inBuffer.end) return 0;
        const int id = m_NextId++;
        m_Entries.push_back({ id, inBuffer, std::move(style) });
        return id;
    }

    bool TextStyleBuffer::RemoveStyle(const void* creatorId, int handle) {
        RequireMutable();
        if (creatorId != Id()) return false;
        const auto it = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const Entry& e) { return e.id == handle; });
        if (it == m_Entries.end()) return false;
        m_Entries.erase(it);
        return true;
    }

    std::vector<const TextStyleBuffer::Entry*> TextStyleBuffer::InRange(int start, int end) const {
        std::vector<const Entry*> out;
        for (const Entry& e : m_Entries)
            if (Intersect(e.interval.start, e.interval.end, start, end)) out.push_back(&e);
        return out;
    }

    std::vector<text::Range<text::Annotation>> TextStyleBuffer::GetAllStyles() const {
        std::vector<text::Range<text::Annotation>> out;
        out.reserve(m_Entries.size());
        for (const Entry& e : m_Entries)
            out.push_back({ e.item, GapBufferToOriginalIndex(e.interval.start), GapBufferToOriginalIndex(e.interval.end), {} });
        return out;
    }

    std::vector<TextStyleBuffer::StyleInfo> TextStyleBuffer::GetAllStyleInfo() const {
        std::vector<StyleInfo> out;
        for (const Entry& e : m_Entries)
            out.push_back({ e.item, GapBufferToOriginalIndex(e.interval.start), GapBufferToOriginalIndex(e.interval.end),
                            { e.interval.startExpands, e.interval.endExpands } });
        return out;
    }

    void TextStyleBuffer::SetRange(int handle, int start, int end) {
        RequireMutable();
        if (start >= end) throw std::invalid_argument("Reversed or collapsed range is not accepted.");
        Entry* e = Find(handle);
        if (!e) throw std::logic_error("TrackedRange is not found.");
        e->interval = { OriginalIndexToGapBuffer(start, true, e->interval.startExpands), OriginalIndexToGapBuffer(end, false, e->interval.endExpands),
                        e->interval.startExpands, e->interval.endExpands };
    }

    std::pair<int, int> TextStyleBuffer::GetRange(int handle) const {
        const Entry* e = Find(handle);
        if (!e) throw std::logic_error("TrackedRange is not found.");
        return { GapBufferToOriginalIndex(e->interval.start), GapBufferToOriginalIndex(e->interval.end) };
    }

    ExpandPolicy TextStyleBuffer::GetExpandPolicy(int handle) const {
        const Entry* e = Find(handle);
        if (!e) throw std::logic_error("TrackedRange is not found.");
        return { e->interval.startExpands, e->interval.endExpands };
    }

    void TextStyleBuffer::SetExpandPolicy(int handle, ExpandPolicy policy) {
        RequireMutable();
        Entry* e = Find(handle);
        if (!e) throw std::logic_error("TrackedRange is not found.");
        const int originalStart = GapBufferToOriginalIndex(e->interval.start);
        const int originalEnd = GapBufferToOriginalIndex(e->interval.end);
        e->interval = { OriginalIndexToGapBuffer(originalStart, true, policy.startExpands), OriginalIndexToGapBuffer(originalEnd, false, policy.endExpands),
                        policy.startExpands, policy.endExpands };
    }

    const text::Annotation* TextStyleBuffer::GetItem(int handle) const {
        const Entry* e = Find(handle);
        return e ? &e->item : nullptr;
    }

    void TextStyleBuffer::SetItem(int handle, text::Annotation item) {
        RequireMutable();
        if (Entry* e = Find(handle)) e->item = std::move(item);
    }

    bool TextStyleBuffer::ReplaceText(int start, int end, int newLength) {
        RequireMutable();
        if (m_Entries.empty()) return false;
        EnlargeGapIfNeeded(newLength - (end - start));
        DeleteText(start, end);
        m_GapStart += newLength;
        return true;
    }

    int TextStyleBuffer::NextTransition(int start, int limit) const {
        if (limit < start) throw std::invalid_argument("limit cannot be less than start");
        if (start == limit) return limit;
        const int startInBuffer = start < m_GapStart ? start : start + GapLength();
        const int limitInBuffer = limit < m_GapStart ? limit : limit + GapLength();
        // The nearest start or end after startInBuffer, up to limitInBuffer.
        int best = limitInBuffer;
        for (const Entry& e : m_Entries)
            for (const int b : { e.interval.start, e.interval.end })
                if (b > startInBuffer && b < best) best = b;
        return GapBufferToOriginalIndex(best);
    }

    int TextStyleBuffer::PreviousTransition(int start, int limit) const {
        if (limit > start) throw std::invalid_argument("limit cannot be greater than start");
        if (start == limit) return limit;
        const int startInBuffer = start <= m_GapStart ? start : start + GapLength();
        const int limitInBuffer = limit <= m_GapStart ? limit : limit + GapLength();
        int best = limitInBuffer;
        for (const Entry& e : m_Entries)
            for (const int b : { e.interval.start, e.interval.end })
                if (b < startInBuffer && b > best) best = b;
        return GapBufferToOriginalIndex(best);
    }

    int TextStyleBuffer::OriginalIndexToGapBuffer(int index, bool isStart, bool expand) const {
        if (index < m_GapStart) return index;
        if (index == m_GapStart) return (isStart != expand) ? index + GapLength() : index;
        return index + GapLength();
    }

    int TextStyleBuffer::GapBufferToOriginalIndex(int index) const {
        return index <= m_GapStart ? index : index - GapLength();
    }

    std::pair<int, int> TextStyleBuffer::OriginalToGapBufferForGetStyle(int start, int end) const {
        if (start == end && start == m_GapStart) return { m_GapEnd, m_GapEnd };
        return { OriginalIndexToGapBuffer(start, true, true), OriginalIndexToGapBuffer(end, false, true) };
    }

    // mapIntervals: every interval touching [start, end] (inclusive) is mapped; a mapped interval
    // that is empty or reversed goes.
    template<typename Mapper>
    void TextStyleBuffer::MapIntervals(int start, int end, Mapper mapper) {
        for (Entry& e : m_Entries)
            if (e.interval.start <= end && e.interval.end >= start) e.interval = mapper(e.interval);
        std::erase_if(m_Entries, [](const Entry& e) { return e.interval.start >= e.interval.end; });
    }

    void TextStyleBuffer::DeleteText(int start, int end) {
        if (start < m_GapStart && end <= m_GapStart) {
            MoveGapLeft(m_GapStart - end);
            DeleteBeforeGap(end - start);
        } else if (start < m_GapStart && end >= m_GapStart) {
            const int before = m_GapStart - start;
            const int after = end - m_GapStart;
            DeleteBeforeGap(before);
            DeleteAfterGap(after);
        } else {
            MoveGapRight(start - m_GapStart);
            DeleteAfterGap(end - start);
        }
    }

    void TextStyleBuffer::MoveGapLeft(int count) {
        if (count == 0) return;
        const int newGapStart = m_GapStart - count, gapStart = m_GapStart, gapLength = GapLength();
        MapIntervals(newGapStart, gapStart, [&](Interval i) {
            const auto in = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
            const int s = (i.start == newGapStart && i.startExpands) ? i.start : in(i.start, newGapStart, gapStart) ? i.start + gapLength : i.start;
            const int e = (i.end == newGapStart && !i.endExpands) ? i.end : in(i.end, newGapStart, gapStart) ? i.end + gapLength : i.end;
            return Interval{ s, e, i.startExpands, i.endExpands };
        });
        m_GapStart -= count;
        m_GapEnd -= count;
    }

    void TextStyleBuffer::MoveGapRight(int count) {
        if (count == 0) return;
        const int newGapEnd = m_GapEnd + count, gapEnd = m_GapEnd, gapLength = GapLength();
        MapIntervals(gapEnd, newGapEnd, [&](Interval i) {
            const auto in = [](int v, int lo, int hiExclusive) { return v >= lo && v < hiExclusive; };
            const int s = (i.start == newGapEnd && i.startExpands) ? i.start - gapLength : in(i.start, gapEnd, newGapEnd) ? i.start - gapLength : i.start;
            const int e = (i.end == newGapEnd && !i.endExpands) ? i.end - gapLength : in(i.end, gapEnd, newGapEnd) ? i.end - gapLength : i.end;
            return Interval{ s, e, i.startExpands, i.endExpands };
        });
        m_GapStart += count;
        m_GapEnd += count;
    }

    void TextStyleBuffer::DeleteBeforeGap(int count) {
        if (count == 0) return;
        const int newGapStart = m_GapStart - count, gapStart = m_GapStart, gapEnd = m_GapEnd;
        MapIntervals(newGapStart, gapStart, [&](Interval i) {
            const auto in = [](int v, int lo, int hiExclusive) { return v >= lo && v < hiExclusive; };
            const int s = i.start == gapStart ? newGapStart : in(i.start, newGapStart, gapStart) ? (i.startExpands ? newGapStart : gapEnd) : i.start;
            const int e = i.end == gapStart ? newGapStart : in(i.end, newGapStart, gapStart) ? (i.endExpands ? gapEnd : newGapStart) : i.end;
            if (s >= e || (s == newGapStart && e == gapEnd)) return Interval{ s, s };
            return Interval{ s, e, i.startExpands, i.endExpands };
        });
        m_GapStart -= count;
    }

    void TextStyleBuffer::DeleteAfterGap(int count) {
        if (count == 0) return;
        const int newGapEnd = m_GapEnd + count, gapStart = m_GapStart, gapEnd = m_GapEnd;
        MapIntervals(gapEnd, newGapEnd, [&](Interval i) {
            const auto in = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
            const int s = i.start == gapEnd ? newGapEnd : in(i.start, gapEnd, newGapEnd) ? (i.startExpands ? gapStart : newGapEnd) : i.start;
            const int e = i.end == gapEnd ? newGapEnd : in(i.end, gapEnd, newGapEnd) ? (i.endExpands ? newGapEnd : gapStart) : i.end;
            if (s >= e || (s == gapStart && e == newGapEnd)) return Interval{ s, s };
            return Interval{ s, e, i.startExpands, i.endExpands };
        });
        m_GapEnd += count;
    }

    void TextStyleBuffer::EnlargeGapIfNeeded(int requiredSize) {
        if (m_Entries.empty()) return;
        if (GapLength() >= requiredSize) return;
        const int offset = GapLength() - requiredSize + kDefaultGapLength;
        const int gapStart = m_GapStart;
        MapIntervals(gapStart, INT_MAX, [&](Interval i) {
            return Interval{ i.start > gapStart ? i.start + offset : i.start, i.end > gapStart ? i.end + offset : i.end, i.startExpands, i.endExpands };
        });
        m_GapEnd += offset;
    }

    bool TextStyleBuffer::operator==(const TextStyleBuffer& o) const {
        if (m_GapStart != o.m_GapStart || m_GapEnd != o.m_GapEnd || m_Entries.size() != o.m_Entries.size()) return false;
        for (std::size_t i = 0; i < m_Entries.size(); ++i)
            if (!(m_Entries[i].interval == o.m_Entries[i].interval) || !(m_Entries[i].item == o.m_Entries[i].item)) return false;
        return true;
    }

    std::vector<text::Range<text::SpanStyle>> TextFieldTextStyles::GetSpanStyles(int start, int end) const {
        const int lo = std::clamp(std::min(start, end), 0, m_Length), hi = std::clamp(std::max(start, end), 0, m_Length);
        return m_Buffer->GetImmutableStyles<text::SpanStyle>(lo, hi);
    }

    std::vector<text::Range<text::ParagraphStyle>> TextFieldTextStyles::GetParagraphStyles(int start, int end) const {
        const int lo = std::clamp(std::min(start, end), 0, m_Length), hi = std::clamp(std::max(start, end), 0, m_Length);
        return m_Buffer->GetImmutableStyles<text::ParagraphStyle>(lo, hi);
    }

}
