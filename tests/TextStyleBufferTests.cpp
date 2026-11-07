#include "Check.h"

#include "kiln/foundation/text/input/TextStyleBuffer.h"
#include "kiln/foundation/text/TextFieldState.h"

#include <random>

// Compose's TextStyleBufferTest: replaceText's expand rules, transitions, and a randomized run
// against a reference model of delete-then-insert.

using namespace kiln;
using namespace kiln::foundation;

namespace {
    using Interval = TextStyleBuffer::Interval;
    text::Annotation Item(int v) {
        text::SpanStyle s;
        s.fontWeight = text::FontWeight{ v + 1 };
        return text::Annotation(s);
    }
    int ValueOf(const text::Annotation& a) { return std::get<text::SpanStyle>(a).fontWeight->weight - 1; }
    struct R {
        int item, start, end;
        bool operator==(const R&) const = default;
    };
    std::vector<R> All(const TextStyleBuffer& b) {
        std::vector<R> out;
        for (const auto& r : b.GetAllStyles()) out.push_back({ ValueOf(r.item), r.start, r.end });
        return out;
    }

    // ReferenceTextStyleBuffer
    struct Reference {
        int textLength;
        std::vector<int> items;
        std::vector<Interval> intervals;
        std::vector<int> handles;
        static bool Overlaps(const Interval& i, int start, int end) {
            return (((i.start == i.end) || (start == end)) && (i.start == start)) || ((i.start < end) && (start < i.end));
        }
        std::vector<R> Styles(int start, int end) const {
            std::vector<R> out;
            for (std::size_t i = 0; i < intervals.size(); ++i)
                if (Overlaps(intervals[i], start, end)) out.push_back({ items[i], intervals[i].start, intervals[i].end });
            return out;
        }
        std::vector<R> All() const {
            std::vector<R> out;
            for (std::size_t i = 0; i < intervals.size(); ++i) out.push_back({ items[i], intervals[i].start, intervals[i].end });
            return out;
        }
        void Replace(int start, int end, int newLength) {
            std::vector<int> ni, nh;
            std::vector<Interval> nv;
            for (std::size_t i = 0; i < intervals.size(); ++i) {
                const Interval& iv = intervals[i];
                const int offset = end - start;
                auto after = [&](int x) { return x < start ? x : x < end ? start : x - offset; };
                const int s = after(iv.start), e = after(iv.end);
                if (s < e) {
                    const int ns = s < start ? s : (s == start && iv.startExpands) ? start : s + newLength;
                    const int ne = e < start ? e : (e == start && !iv.endExpands) ? start : e + newLength;
                    nv.push_back({ ns, ne, iv.startExpands, iv.endExpands });
                    ni.push_back(items[i]);
                    nh.push_back(handles[i]);
                }
            }
            intervals = nv;
            items = ni;
            handles = nh;
            textLength += newLength - (end - start);
        }
    };
}

TEST(TextStyleBufferReplaceTextFollowsExpandPolicies) {
    {
        TextStyleBuffer b;   // overlapping start: the start moves to the deletion's end
        b.AddStyle(Item(0), { 10, 20 });
        b.ReplaceText(8, 12, 4);
        CHECK((All(b) == std::vector<R>{ { 0, 12, 20 } }));
    }
    {
        TextStyleBuffer b;   // overlapping end with the end expanding
        b.AddStyle(Item(0), { 10, 20, false, true });
        b.ReplaceText(18, 22, 4);
        CHECK((All(b) == std::vector<R>{ { 0, 10, 22 } }));
    }
    {
        TextStyleBuffer b;   // covered: removed
        b.AddStyle(Item(0), { 10, 20 });
        b.ReplaceText(5, 25, 5);
        CHECK(All(b).empty());
    }
    {
        TextStyleBuffer b;   // inserting at the ends with InsideOnly
        b.AddStyle(Item(0), { 10, 20, false, false });
        b.ReplaceText(10, 10, 5);
        CHECK((All(b) == std::vector<R>{ { 0, 15, 25 } }));
        b.ReplaceText(25, 25, 5);
        CHECK((All(b) == std::vector<R>{ { 0, 15, 25 } }));
    }
    {
        TextStyleBuffer b;   // AtBoth
        b.AddStyle(Item(0), { 10, 20, true, true });
        b.ReplaceText(10, 10, 5);
        CHECK((All(b) == std::vector<R>{ { 0, 10, 25 } }));
        b.ReplaceText(25, 25, 5);
        CHECK((All(b) == std::vector<R>{ { 0, 10, 30 } }));
    }
    {
        TextStyleBuffer b;   // multiple styles, in addition order
        b.AddStyle(Item(0), { 10, 20 });
        b.AddStyle(Item(1), { 15, 25 });
        b.ReplaceText(12, 18, 2);
        CHECK((All(b) == std::vector<R>{ { 0, 10, 16 }, { 1, 14, 21 } }));
    }
    {
        TextStyleBuffer b;   // transitions around the gap
        b.AddStyle(Item(0), { 0, 10 });
        b.AddStyle(Item(1), { 20, 30 });
        b.ReplaceText(15, 15, 10);
        CHECK_EQ(b.NextTransition(0, 50), 10);
        CHECK_EQ(b.NextTransition(10, 50), 30);
        CHECK_EQ(b.NextTransition(30, 50), 40);
        CHECK_EQ(b.PreviousTransition(50, 0), 40);
        CHECK_EQ(b.PreviousTransition(30, 0), 10);
        CHECK_EQ(b.PreviousTransition(10, 0), 0);
    }
}

TEST(TextStyleBufferMatchesTheReferenceModel) {
    std::mt19937 random(42);
    auto nextInt = [&](int bound) { return int(random() % std::uint32_t(bound)); };
    TextStyleBuffer buffer;
    Reference reference{ 1000, {}, {}, {} };
    int mismatches = 0;
    for (int step = 0; step < 5000; ++step) {
        const int op = nextInt(3);
        const int start = reference.textLength == 0 ? 0 : nextInt(reference.textLength + 1);
        const int end = reference.textLength == start ? start : start + nextInt(reference.textLength + 1 - start);
        if (op == 0) {
            if (start < end) {
                const int style = nextInt(100);
                const Interval iv{ start, end, nextInt(2) == 1, nextInt(2) == 1 };
                const int h = buffer.AddStyle(Item(style), iv);
                reference.items.push_back(style);
                reference.intervals.push_back(iv);
                reference.handles.push_back(h);
            }
        } else if (op == 1) {
            if (!reference.items.empty()) {
                const std::size_t index = std::size_t(nextInt(int(reference.items.size())));
                if (!buffer.RemoveStyle(buffer.Id(), reference.handles[index])) ++mismatches;
                reference.items.erase(reference.items.begin() + std::ptrdiff_t(index));
                reference.intervals.erase(reference.intervals.begin() + std::ptrdiff_t(index));
                reference.handles.erase(reference.handles.begin() + std::ptrdiff_t(index));
            }
        } else {
            const int newLength = nextInt(20);
            buffer.ReplaceText(start, end, newLength);
            reference.Replace(start, end, newLength);
            // Right at the gap, where gap bugs show.
            const int gapIndex = start + newLength;
            std::vector<R> actual;
            for (int h : buffer.GetStyles<text::SpanStyle>(gapIndex, gapIndex)) {
                const auto [s, e] = buffer.GetRange(h);
                actual.push_back({ ValueOf(*buffer.GetItem(h)), s, e });
            }
            if (actual != reference.Styles(gapIndex, gapIndex)) ++mismatches;
        }
        if (All(buffer) != reference.All()) ++mismatches;
    }
    CHECK_EQ(mismatches, 0);
}

TEST(FieldStylesFollowTypingAndTrackedRangesChange) {
    TextFieldState state("hello world");
    TrackedRange<text::SpanStyle> bold;
    state.Edit([&](TextFieldBuffer& b) {
        text::SpanStyle s;
        s.fontWeight = text::FontWeight::Bold();
        bold = b.AddStyle(s, TextRange(0, 5), ExpandPolicy::AtEnd());
        CHECK(b.IsValid(bold));
        CHECK(b.GetTextRange(bold) == TextRange(0, 5));
    });
    auto spans = state.SpanStyles(TextRange(0, 11));
    CHECK(spans.size() == 1 && spans[0].start == 0 && spans[0].end == 5);
    // Typing at the end of "hello" joins the bold range (AtEnd); at its start it does not.
    state.EditAsUser(nullptr, [](TextFieldBuffer& b) { b.Replace(5, 5, u"!"); });
    state.EditAsUser(nullptr, [](TextFieldBuffer& b) { b.Replace(0, 0, u">"); });
    spans = state.SpanStyles(TextRange(0, 13));
    CHECK(spans.size() == 1 && spans[0].start == 1 && spans[0].end == 7);
    // A later edit sees the styles (with new tracked ranges) and can change them.
    state.Edit([&](TextFieldBuffer& b) {
        CHECK(!b.IsValid(bold));   // another buffer's range
        const auto found = b.GetSpanStyles(TextRange(2, 3));
        CHECK(found.size() == 1);
        if (found.size() == 1) {
            b.SetTextRange(found[0], TextRange(1, 3));
            b.SetExpandPolicy(found[0], ExpandPolicy::InsideOnly());
        }
    });
    spans = state.SpanStyles(TextRange(0, 13));
    CHECK(spans.size() == 1 && spans[0].start == 1 && spans[0].end == 3);
    // Removing it.
    state.Edit([&](TextFieldBuffer& b) {
        const auto found = b.GetSpanStyles(TextRange(0, 13));
        if (!found.empty()) b.RemoveStyle(found[0]);
    });
    CHECK(state.SpanStyles(TextRange(0, 13)).empty());
}
