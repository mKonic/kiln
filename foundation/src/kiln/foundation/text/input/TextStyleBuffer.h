#pragma once

// Styled text in fields (Compose's TextStyleBuffer, IntIntervalTree, TrackedRange, ExpandPolicy and
// TextFieldTextStyles): span and paragraph styles over ranges of a field's text that follow its
// edits. Ranges live in gap-buffer coordinates, so an edit only remaps the styles around the
// gap; each range's expand policy says whether text inserted at its start or end joins it.
//
// IntIntervalTree is a red-black interval tree in Compose; its observable behaviour is kept
// here over a list in addition order (queries return ranges in addition order, overlap is
// half-open with an empty range touching a start, remapping drops collapsed ranges), which is
// what a field's handful of styles needs.

#include "kiln/text/AnnotatedString.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::foundation {

    // ExpandPolicy: whether text inserted at a range's start / end becomes part of it.
    struct ExpandPolicy {
        bool startExpands = false, endExpands = false;
        static constexpr ExpandPolicy InsideOnly() { return { false, false }; }
        static constexpr ExpandPolicy AtStart() { return { true, false }; }
        static constexpr ExpandPolicy AtEnd() { return { false, true }; }
        static constexpr ExpandPolicy AtBoth() { return { true, true }; }
        bool operator==(const ExpandPolicy&) const = default;
    };

    // TrackedRange: a style added to a TextFieldBuffer, valid in that buffer only.
    template<typename T>
    struct TrackedRange {
        const void* creatorId = nullptr;
        int handle = 0;
        bool operator==(const TrackedRange&) const = default;
    };

    class TextStyleBuffer {
    public:
        struct Interval {
            int start = 0, end = 0;
            bool startExpands = false, endExpands = false;
            bool operator==(const Interval&) const = default;
        };

        TextStyleBuffer() = default;
        // A copy of `source` (a new identity: its tracked ranges are not this buffer's).
        TextStyleBuffer(const TextStyleBuffer& source, bool mutableBuffer);
        TextStyleBuffer(const TextStyleBuffer&) = delete;
        TextStyleBuffer& operator=(const TextStyleBuffer&) = delete;

        const void* Id() const { return &m_Id; }
        std::shared_ptr<const TextStyleBuffer> ToImmutable() const { return std::make_shared<const TextStyleBuffer>(*this, false); }
        void SyncTo(const TextStyleBuffer& other);
        bool Empty() const { return m_Entries.empty(); }

        // addStyle: null handle for an empty range.
        int AddStyle(text::Annotation style, Interval interval);
        bool RemoveStyle(const void* creatorId, int handle);
        // getStyles / getImmutableStyles: the styles of one kind overlapping [start, end).
        template<typename R> std::vector<int> GetStyles(int start, int end) const;
        template<typename R> std::vector<text::Range<R>> GetImmutableStyles(int start, int end) const;
        std::vector<text::Range<text::Annotation>> GetAllStyles() const;
        // Each style with its expand policy, in addition order (for saving).
        struct StyleInfo { text::Annotation item; int start, end; ExpandPolicy policy; };
        std::vector<StyleInfo> GetAllStyleInfo() const;

        bool IsValid(const void* creatorId, int handle) const { return creatorId == Id() && Find(handle) != nullptr; }
        // setRange: a non-empty range; the expand policy stays.
        void SetRange(int handle, int start, int end);
        std::pair<int, int> GetRange(int handle) const;
        ExpandPolicy GetExpandPolicy(int handle) const;
        void SetExpandPolicy(int handle, ExpandPolicy policy);
        const text::Annotation* GetItem(int handle) const;
        void SetItem(int handle, text::Annotation item);

        // replaceText: [start, end) became newLength characters. False when there are no styles.
        bool ReplaceText(int start, int end, int newLength);
        // The next / previous start or end of a style after / before `start`, up to `limit`.
        int NextTransition(int start, int limit) const;
        int PreviousTransition(int start, int limit) const;
        void Clear() { m_Entries.clear(); }

        bool operator==(const TextStyleBuffer& o) const;

    private:
        struct Entry {
            int id;
            Interval interval;
            text::Annotation item;
        };
        const Entry* Find(int handle) const;
        Entry* Find(int handle);
        int GapLength() const { return m_GapEnd - m_GapStart; }
        int OriginalIndexToGapBuffer(int index, bool isStart, bool expand) const;
        int GapBufferToOriginalIndex(int index) const;
        std::pair<int, int> OriginalToGapBufferForGetStyle(int start, int end) const;
        template<typename Mapper> void MapIntervals(int start, int end, Mapper mapper);
        std::vector<const Entry*> InRange(int start, int end) const;
        void DeleteText(int start, int end);
        void MoveGapLeft(int count);
        void MoveGapRight(int count);
        void DeleteBeforeGap(int count);
        void DeleteAfterGap(int count);
        void EnlargeGapIfNeeded(int requiredSize);
        void RequireMutable() const;

        std::vector<Entry> m_Entries;   // in addition order
        int m_NextId = 1;
        int m_GapStart = 0, m_GapEnd = 1000;
        bool m_Mutable = true;
        char m_Id = 0;   // its address is the identity
    };

    template<typename R>
    std::vector<int> TextStyleBuffer::GetStyles(int start, int end) const {
        std::vector<int> out;
        if (start > end) return out;
        const auto [s, e] = OriginalToGapBufferForGetStyle(start, end);
        for (const Entry* en : InRange(s, e))
            if (std::holds_alternative<R>(en->item)) out.push_back(en->id);
        return out;
    }

    template<typename R>
    std::vector<text::Range<R>> TextStyleBuffer::GetImmutableStyles(int start, int end) const {
        std::vector<text::Range<R>> out;
        if (start > end) return out;
        const auto [s, e] = OriginalToGapBufferForGetStyle(start, end);
        for (const Entry* en : InRange(s, e))
            if (const R* r = std::get_if<R>(&en->item))
                out.push_back({ *r, GapBufferToOriginalIndex(en->interval.start), GapBufferToOriginalIndex(en->interval.end), {} });
        return out;
    }

    // TextFieldTextStyles: a value's styles, read-only.
    class TextFieldTextStyles {
    public:
        TextFieldTextStyles(std::shared_ptr<const TextStyleBuffer> buffer, int length) : m_Buffer(std::move(buffer)), m_Length(length) {}
        std::vector<text::Range<text::SpanStyle>> GetSpanStyles(int start, int end) const;
        std::vector<text::Range<text::ParagraphStyle>> GetParagraphStyles(int start, int end) const;
        const TextStyleBuffer& Buffer() const { return *m_Buffer; }
        bool operator==(const TextFieldTextStyles& o) const { return m_Length == o.m_Length && *m_Buffer == *o.m_Buffer; }
    private:
        std::shared_ptr<const TextStyleBuffer> m_Buffer;
        int m_Length;
    };

}
