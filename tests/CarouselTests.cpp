#include "Check.h"

// Carousel: the keyline model (ports of Compose's androidHostTest carousel tests).

#include "kiln/material3/Carousel.h"
#include "kiln/material3/internal/CarouselKeylines.h"

#include <cmath>

using namespace kiln;
using namespace kiln::material3;
using namespace kiln::material3::internal::carousel;

namespace {
    bool Feq(float a, float b, float eps = 0.001f) { return std::abs(a - b) <= eps; }

    std::vector<float> Offsets(const KeylineList& l) {
        std::vector<float> v;
        for (const auto& k : l.Items()) v.push_back(k.offset);
        return v;
    }
    std::vector<float> Unadjusted(const KeylineList& l) {
        std::vector<float> v;
        for (const auto& k : l.Items()) v.push_back(k.unadjustedOffset);
        return v;
    }
    bool AllNear(const std::vector<float>& a, const std::vector<float>& b, float eps = 0.01f) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) if (!Feq(a[i], b[i], eps)) return false;
        return true;
    }
    bool KeylineNear(const Keyline& a, const Keyline& b, float eps = 0.01f) {
        return Feq(a.size, b.size, eps) && Feq(a.offset, b.offset, eps) && Feq(a.unadjustedOffset, b.unadjustedOffset, eps) &&
               a.isFocal == b.isFocal && a.isAnchor == b.isAnchor && a.isPivot == b.isPivot && Feq(a.cutoff, b.cutoff, eps);
    }
    bool ListNear(const KeylineList& a, const KeylineList& b) {
        if (a.Size() != b.Size()) return false;
        for (int i = 0; i < a.Size(); ++i) if (!KeylineNear(a[i], b[i])) return false;
        return true;
    }

    constexpr float MinSmall = 40, MaxSmall = 56;
    KeylineList MultiBrowse(float carousel, float preferred, float spacing, int count) {
        return MultiBrowseKeylineList(1, carousel, preferred, spacing, count, MinSmall, MaxSmall);
    }
    KeylineList Hero(float carousel, std::optional<float> preferred, float spacing, int count) {
        return HeroKeylineList(1, carousel, preferred, spacing, count, true, MinSmall, MaxSmall);
    }
    Strategy Make(KeylineList k, float space, float spacing = 0, float before = 0, float after = 0) {
        return Strategy(std::move(k), space, spacing, before, after);
    }

    // StrategyTest's companion.
    constexpr float L = 100, S = 20, M = (L + S) / 2, XS = 5;
    KeylineList CenterAligned() {   // [xs | s s m l l m s s | xs]
        return KeylineListOf(S * 2 + M + L * 2 + M + S * 2, 0, CarouselAlignment::Center,
                             { { XS, true }, { S }, { S }, { M }, { L }, { L }, { M }, { S }, { S }, { XS, true } });
    }
    KeylineList StartAligned() {   // [xs | l m s | xs]
        return KeylineListOf(L + M + S, 0, CarouselAlignment::Start, { { XS, true }, { L }, { M }, { S }, { XS, true } });
    }
}

// --- ArrangementTest ------------------------------------------------------------------------------

TEST(CarouselArrangementFitsByResizingSmallThenMediumItems) {
    const auto find = [](float space, float targetSmall, int small, int medium, int large) {
        const float targetLarge = targetSmall * 3, targetMedium = (targetLarge + targetSmall) / 2;
        return Arrangement::FindLowestCostArrangement(space, 0, targetSmall, 40, 56, { small }, targetMedium, { medium }, targetLarge, { large });
    };
    {   // 1L1M1S: no adjustments.
        const float s = 56, l = s * 3, m = (l + s) / 2;
        const auto a = find(l + m + s, s, 1, 1, 1);
        CHECK(a && a->largeSize == l && a->mediumSize == m && a->smallSize == s);
    }
    {   // Smaller space: the small item shrinks.
        const float s = 56, l = s * 3, m = (l + s) / 2;
        const auto a = find(l + m + s - 10, s, 1, 1, 1);
        CHECK(a && a->largeSize == l && std::lround(a->mediumSize) == std::lround(m) && a->smallSize == s - 10);
    }
    {   // Larger: it grows.
        const float s = 40, l = s * 3, m = (l + s) / 2;
        const auto a = find(l + m + s + 10, s, 1, 1, 1);
        CHECK(a && a->largeSize == l && std::lround(a->mediumSize) == std::lround(m) && a->smallSize == s + 10);
    }
    {   // Small at its minimum: the medium shrinks.
        const float s = 40, l = s * 3, m = (l + s) / 2, adj = m * 0.05f;
        const auto a = find(l + m + s - adj, s, 1, 1, 1);
        CHECK(a && a->largeSize == l && std::lround(a->mediumSize) == std::lround(m - adj) && a->smallSize == s);
    }
    {   // Small at its maximum: the medium grows.
        const float s = 56, l = s * 3, m = (l + s) / 2, adj = m * 0.05f;
        const auto a = find(l + m + s + adj, s, 1, 1, 1);
        CHECK(a && a->largeSize == l && std::lround(a->mediumSize) == std::lround(m + adj) && a->smallSize == s);
    }
    {   // 1L1M2S.
        const float s = 40, l = s * 3, m = (l + s) / 2;
        const auto a = find(l + m + s * 2 + 20, s, 2, 1, 1);
        CHECK(a && a->largeSize == l && std::lround(a->mediumSize) == std::lround(m) && a->smallSize == s + 10);
        const float s2 = 56, l2 = s2 * 3, m2 = (l2 + s2) / 2;
        const auto b = find(l2 + m2 + s2 * 2 - 20, s2, 2, 1, 1);
        CHECK(b && b->largeSize == l2 && std::lround(b->mediumSize) == std::lround(m2) && b->smallSize == s2 - 10);
    }
    {   // 2L2M2S.
        const float s = 56, l = s * 3, m = (l + s) / 2, adj = m * 0.05f;
        const auto a = find(l * 2 + m * 2 + s * 2 + adj * 2, s, 2, 2, 2);
        CHECK(a && a->largeSize == l && std::lround(a->mediumSize) == std::lround(m + adj) && a->smallSize == s);
        const float s2 = 40, l2 = s2 * 3, m2 = (l2 + s2) / 2, adj2 = m2 * 0.05f;
        const auto b = find(l2 * 2 + m2 * 2 + s2 * 2 - adj2 * 2, s2, 2, 2, 2);
        CHECK(b && b->largeSize == l2 && std::lround(b->mediumSize) == std::lround(m2 - adj2) && b->smallSize == s2);
    }
}

// --- MultiBrowseTest ------------------------------------------------------------------------------

TEST(CarouselMultiBrowseKeylines) {
    // Enough room: the large size is kept.
    CHECK(Make(MultiBrowse(500, 120, 0, 10), 500).ItemMainAxisSize() == 120);
    {   // Larger than the container: [xSmall-Large-Small-xSmall].
        const Strategy s = Make(MultiBrowse(100, 200, 0, 10), 100);
        const KeylineList& k = s.defaultKeylines;
        CHECK(s.ItemMainAxisSize() <= 100);
        CHECK(k.Size() == 4 && k[0].unadjustedOffset < 0 && k[k.LastIndex()].unadjustedOffset > 100);
        CHECK(k[1].isFocal && k[2].size == MinSmall);
    }
    {   // No room for small items.
        const Strategy s = Make(MultiBrowse(MinSmall, 200, 0, 10), MinSmall);
        CHECK(s.ItemMainAxisSize() == MinSmall);
        CHECK(s.defaultKeylines.FirstFocal() == s.defaultKeylines.FirstNonAnchor());
        CHECK(s.defaultKeylines.LastFocal() == s.defaultKeylines.LastNonAnchor());
    }
    CHECK(MultiBrowse(0, 200, 0, 10).Empty());
    CHECK(MultiBrowse(-10, 200, 0, 10).Empty());
    {   // [xSmall-Large-Large-Medium-Small-xSmall], the medium between large and small.
        const float size = 200 * 2 + MaxSmall * 2;
        const KeylineList k = Make(MultiBrowse(size, 200, 0, 10), size).defaultKeylines;
        CHECK(k.Size() == 6 && k[1].isFocal && k[2].isFocal && k[3].size < k[2].size && k[4].size < k[3].size);
        // Three items: [xSmall-Large-Large-Small-xSmall].
        const KeylineList k3 = Make(MultiBrowse(size, 200, 0, 3), size).defaultKeylines;
        CHECK(k3.Size() == 5 && k3[1].isFocal && k3[2].isFocal && k3[3].size < k3[2].size);
    }
    {   // Item spacing.
        const KeylineList k = MultiBrowse(380, 186, 8, 10);
        const Strategy s = Make(k, 380, 8);
        CHECK(k.FirstFocal().size == 186 && k.FirstFocal().offset == 186.0f / 2);
        CHECK(k.LastNonAnchor().offset + k.LastNonAnchor().size / 2 == 380);
        CHECK(s.ItemMainAxisSize() == 186);
        CHECK(s.defaultKeylines[3].size == 56 && s.defaultKeylines[3].offset == 380 - 56.0f / 2);
        const float maxScroll = 186.0f * 10 + 8.0f * 10 - 380;
        CHECK(AllNear(Unadjusted(s.KeylineListForScrollOffset(0, maxScroll)), { -101, 93, 287, 481, 675 }));
    }
}

// --- UncontainedTest ------------------------------------------------------------------------------

TEST(CarouselUncontainedKeylines) {
    constexpr float Anchor = AnchorSizeDp;
    for (float itemSize : { 500.0f, 600.0f }) {   // the full width, or more: [xSmall-large-xSmall]
        const float carousel = itemSize == 500 ? 500 : 400;
        const KeylineList k = Make(UncontainedKeylineList(1, carousel, itemSize, 0), carousel).defaultKeylines;
        CHECK(k.Size() == 3 && k[0].offset == -Anchor / 2 && k[1].size == carousel && k[2].offset == carousel + Anchor / 2);
    }
    {   // 501 in 1000: two large items, the second cut off.
        const KeylineList k = UncontainedKeylineList(1, 1000, 501, 0);
        CHECK(k.Size() == 4 && Feq(k[0].offset, -125.25f) && k[1].size == 501 && k[2].size == 501);
        CHECK(k[3].size == Anchor && k[3].offset == 501 * 2 + Anchor / 2);
    }
    {   // 125 in 400: a medium item a third cut off.
        const KeylineList k = UncontainedKeylineList(1, 400, 125, 0);
        CHECK(k.Size() == 6 && k[1].size == 125 && k[2].size == 125 && k[3].size == 125);
        CHECK(k[4].size == 25 * 1.5f && k[4].offset == 393.75f);
        CHECK(k[0].size == 18.75f && k[0].offset == -9.375f);
        CHECK(k[5].size == Anchor && k[5].offset == 125 * 3 + 25 * 1.5f + Anchor / 2);
    }
    {   // 105 in 400: the medium item at 120% of the remaining space.
        const KeylineList k = UncontainedKeylineList(1, 400, 105, 0);
        CHECK(k.Size() == 6 && k[1].size == 105 && k[3].size == 105);
        CHECK(Feq(k[4].size, 85 * 1.2f) && Feq(k[0].size, 85 * 1.2f * 0.5f) && k[5].size == Anchor);
        CHECK(Feq(k[0].offset, -(85 * 1.2f * 0.5f) / 2) && Feq(k[5].offset, 105 * 3 + 85 * 1.2f + Anchor / 2));
    }
    CHECK(UncontainedKeylineList(1, -10, 100, 0).Empty());
}

// --- CenteredHeroTest -----------------------------------------------------------------------------

TEST(CarouselCenteredHeroKeylines) {
    CHECK(Make(Hero(180, std::nullopt, 0, 6), 180).ItemMainAxisSize() == 100);
    {   // Fewer than three items: start-aligned.
        const Strategy s = Make(Hero(180, std::nullopt, 0, 2), 180);
        CHECK(s.ItemMainAxisSize() > 100 && s.defaultKeylines.firstFocalIndex == 1);
    }
    CHECK(Make(Hero(120, std::nullopt, 0, 1), 120).ItemMainAxisSize() == 120);   // one item: full screen
    CHECK(Make(Hero(120, std::nullopt, 0, 6), 120).ItemMainAxisSize() == 120);   // below the threshold
    const float size = 300 * 3 + 40 * 2;
    for (auto [spacing, count] : { std::pair{ 0.0f, 7 }, std::pair{ 12.0f, 7 }, std::pair{ 12.0f, 10 } }) {
        // The small items sit flush with the container's edges.
        const KeylineList k = Make(Hero(size, 300, spacing, count), size, spacing).defaultKeylines;
        CHECK(Feq(k.FirstNonAnchor().offset, 20) && Feq(k.LastNonAnchor().offset, size - 20));
    }
    CHECK(Hero(-10, 100, 0, 5).Empty());
}

// --- StrategyTest ---------------------------------------------------------------------------------

TEST(CarouselStrategyShiftsKeylines) {
    {   // Start aligned: shifts at the end.
        const float size = L + M + S, maxScroll = 10 * L - size;
        const Strategy s = Make(StartAligned(), size);
        CHECK(s.KeylineListForScrollOffset(0, maxScroll) == StartAligned());
        const KeylineList end = s.KeylineListForScrollOffset(maxScroll, maxScroll);
        CHECK(AllNear(Offsets(end), { -2.5f, 10, 50, 130, 182.5f }));
        CHECK(AllNear(Unadjusted(end), { 130 - 300.0f, 130 - 200.0f, 130 - 100.0f, 130, 130 + 100.0f }));
    }
    {   // A cut-off focal item shifts the end with its cut-off.
        const float size = L + M + M, cutoff = 50;
        const KeylineList def = KeylineListOf(size, 0, CarouselAlignment::Start, { { XS, true }, { L + cutoff }, { M }, { M }, { XS, true } });
        const Strategy s = Make(def, size);
        const KeylineList& end = s.endKeylineSteps.back();
        CHECK(def.LastNonAnchor().cutoff == cutoff && def.FirstNonAnchor().offset - def.FirstNonAnchor().size / 2 == 0);
        CHECK(end.FirstNonAnchor().cutoff == cutoff && end.FirstNonAnchor().offset - end.FirstNonAnchor().size / 2 == -cutoff);
        CHECK(end.LastNonAnchor().offset + end.LastNonAnchor().size / 2 == size);
        // End aligned: the start shifts.
        const KeylineList defEnd = KeylineListOf(size, 0, CarouselAlignment::End, { { XS, true }, { M }, { M }, { L + cutoff }, { XS, true } });
        const Strategy se = Make(defEnd, size);
        const KeylineList& start = se.startKeylineSteps.back();
        CHECK(defEnd.FirstNonAnchor().cutoff == cutoff && defEnd.LastNonAnchor().offset + defEnd.LastNonAnchor().size / 2 == size);
        CHECK(start.LastNonAnchor().cutoff == cutoff && start.FirstNonAnchor().offset - start.FirstNonAnchor().size / 2 == 0);
        CHECK(start.LastNonAnchor().offset + start.LastNonAnchor().size / 2 == size + cutoff);
    }
    {   // Overlapping shift ranges go from the last start step straight to the last end step.
        const float size = L + S, maxScroll = L * 2 - size;
        const KeylineList def = KeylineListOf(size, 0, CarouselAlignment::Start, { { XS, true }, { L }, { S }, { XS, true } });
        const Strategy s = Make(def, size, 0, 24, 48);
        const KeylineList start = s.KeylineListForScrollOffset(0, maxScroll);
        CHECK(Feq(start.FirstFocal().offset, 24 + start.FirstFocal().size / 2));
        const KeylineList end = s.KeylineListForScrollOffset(maxScroll, maxScroll);
        CHECK(Feq(end.LastFocal().offset, size - 48 - end.LastFocal().size / 2));
        CHECK(Offsets(s.KeylineListForScrollOffset(maxScroll / 2, maxScroll, true)) != Offsets(def));
    }
    {   // Negative max scroll: the defaults.
        const float size = L + M + S;
        CHECK(Make(StartAligned(), size).KeylineListForScrollOffset(0, L - size) == StartAligned());
    }
}

TEST(CarouselStrategyCenterAlignedSteps) {
    const float size = S * 2 + M + L * 2 + M + S * 2, maxScroll = 12 * L - size;
    const Strategy s = Make(CenterAligned(), size);
    using V = std::vector<KeylineSpec>;
    const std::vector<KeylineList> startSteps{
        CenterAligned(),
        KeylineListOf(size, 0, 3, S + M + L / 2, V{ { XS, true }, { S }, { M }, { L }, { L }, { M }, { S }, { S }, { S }, { XS, true } }),
        KeylineListOf(size, 0, 2, M + L / 2, V{ { XS, true }, { M }, { L }, { L }, { M }, { S }, { S }, { S }, { S }, { XS, true } }),
        KeylineListOf(size, 0, 1, L / 2, V{ { XS, true }, { L }, { L }, { M }, { M }, { S }, { S }, { S }, { S }, { XS, true } }),
    };
    const KeylineList shiftedStart = s.KeylineListForScrollOffset(0, maxScroll);
    CHECK(ListNear(shiftedStart, startSteps.back()));
    CHECK(Feq(shiftedStart.FirstFocal().offset - shiftedStart.FirstFocal().size / 2, 0));
    const float totalStart = startSteps.back().First().unadjustedOffset - startSteps.front().First().unadjustedOffset;
    for (int i = 0; i < 4; ++i) {
        const float scroll = totalStart - (startSteps[std::size_t(i)].First().unadjustedOffset - startSteps[0].First().unadjustedOffset);
        CHECK(ListNear(s.KeylineListForScrollOffset(scroll, maxScroll), startSteps[std::size_t(i)]));
    }

    const std::vector<KeylineList> endSteps{
        CenterAligned(),
        KeylineListOf(size, 0, 5, S * 3 + M + L / 2, V{ { XS, true }, { S }, { S }, { S }, { M }, { L }, { L }, { M }, { S }, { XS, true } }),
        KeylineListOf(size, 0, 6, S * 4 + M + L / 2, V{ { XS, true }, { S }, { S }, { S }, { S }, { M }, { L }, { L }, { M }, { XS, true } }),
        KeylineListOf(size, 0, 7, S * 4 + M * 2 + L / 2, V{ { XS, true }, { S }, { S }, { S }, { S }, { M }, { M }, { L }, { L }, { XS, true } }),
    };
    const KeylineList shiftedEnd = s.KeylineListForScrollOffset(maxScroll, maxScroll);
    CHECK(ListNear(shiftedEnd, endSteps.back()));
    CHECK(Feq(shiftedEnd.LastFocal().offset + shiftedEnd.LastFocal().size / 2, size));
    const float totalEnd = endSteps.front().Last().unadjustedOffset - endSteps.back().Last().unadjustedOffset;
    std::vector<float> scrolls;
    for (int i = 0; i < 4; ++i)
        scrolls.push_back(maxScroll - totalEnd - (endSteps[std::size_t(i)].Last().unadjustedOffset - endSteps[0].Last().unadjustedOffset));
    for (int i = 0; i < 4; ++i) CHECK(ListNear(s.KeylineListForScrollOffset(scrolls[std::size_t(i)], maxScroll), endSteps[std::size_t(i)]));
    // Rounding to the nearest step.
    CHECK(ListNear(s.KeylineListForScrollOffset(scrolls[0] + (scrolls[1] - scrolls[0]) * 0.75f, maxScroll, true), endSteps[1]));
    CHECK(ListNear(s.KeylineListForScrollOffset(scrolls[1] + (scrolls[2] - scrolls[1]) * 0.5f, maxScroll, true), endSteps[2]));
    CHECK(ListNear(s.KeylineListForScrollOffset(scrolls[2] + (scrolls[3] - scrolls[2]) * 0.1f, maxScroll, true), endSteps[2]));
}

TEST(CarouselStrategyEquality) {
    const auto make = [](float space) { return Make(MultiBrowse(space, L, 0, 10), space); };
    CHECK(make(500) == make(500));
    CHECK(!(make(500) == make(501)));
    CHECK(!(make(500) == Make(KeylineList{}, 500)));
}

TEST(CarouselStrategyStepsAccountForSpacingAndPadding) {
    {   // Start aligned, with item spacing.
        const Strategy s = Make(KeylineListOf(380, 8, CarouselAlignment::Start, { { 10, true }, { 186 }, { 122 }, { 56 }, { 10, true } }), 380, 8);
        CHECK(AllNear(Offsets(s.endKeylineSteps[1]), { -13, 28, 157, 319, 393 }));
        CHECK(AllNear(Unadjusted(s.endKeylineSteps[1]), { -231, -37, 157, 351, 545 }));
        CHECK(AllNear(Offsets(s.endKeylineSteps[2]), { -13, 28, 125, 287, 393 }));
        CHECK(AllNear(Unadjusted(s.endKeylineSteps[2]), { -295, -101, 93, 287, 481 }));
    }
    {   // Centre aligned, with item spacing.
        const Strategy s = Make(KeylineListOf(768, 8, CarouselAlignment::Center,
                                              { { 10, true }, { 56 }, { 122 }, { 186 }, { 186 }, { 122 }, { 56 }, { 10, true } }), 768, 8);
        CHECK(s.startKeylineSteps.size() == 3 && s.endKeylineSteps.size() == 3);
        CHECK(AllNear(Offsets(s.startKeylineSteps[1]), { -13, 61, 223, 417, 579, 676, 740, 781 }));
        CHECK(AllNear(Unadjusted(s.startKeylineSteps[1]), { -165, 29, 223, 417, 611, 805, 999, 1193 }));
        CHECK(AllNear(Offsets(s.startKeylineSteps[2]), { -13, 93, 287, 449, 579, 676, 740, 781 }));
        CHECK(AllNear(Unadjusted(s.startKeylineSteps[2]), { -101, 93, 287, 481, 675, 869, 1063, 1257 }));
        CHECK(AllNear(Offsets(s.endKeylineSteps[1]), { -13, 28, 92, 189, 351, 545, 707, 781 }));
        CHECK(AllNear(Unadjusted(s.endKeylineSteps[1]), { -425, -231, -37, 157, 351, 545, 739, 933 }));
        CHECK(AllNear(Offsets(s.endKeylineSteps[2]), { -13, 28, 92, 189, 319, 481, 675, 781 }));
        CHECK(AllNear(Unadjusted(s.endKeylineSteps[2]), { -489, -295, -101, 93, 287, 481, 675, 869 }));
    }
    {   // Centre aligned, with content padding.
        const Strategy s = Make(KeylineListOf(500, 0, CarouselAlignment::Center,
                                              { { 10, true }, { 50 }, { 100 }, { 200 }, { 100 }, { 50 }, { 10, true } }), 500, 0, 16, 24);
        const KeylineList& st = s.startKeylineSteps.back();
        const KeylineList& en = s.endKeylineSteps.back();
        CHECK(Feq(st.FirstFocal().offset - st.FirstFocal().size / 2, 16));
        CHECK(Feq(st.LastNonAnchor().offset + st.LastNonAnchor().size / 2, 500));
        CHECK(Feq(en.LastFocal().offset + en.LastFocal().size / 2, 500 - 24));
        CHECK(Feq(en.FirstNonAnchor().offset - en.FirstNonAnchor().size / 2, 0, 0.01f));
    }
    {   // Two large and a small, with padding.
        const Strategy s = Make(KeylineListOf(444, 8, CarouselAlignment::Start, { { 10, true }, { 186 }, { 186 }, { 56 }, { 10, true } }), 444, 8, 16, 16);
        CHECK(s.ItemMainAxisSize() == 186);
        const Keyline small = s.startKeylineSteps.back()[3];
        CHECK(Feq(small.offset + small.size / 2, 444));
        const KeylineList& end = s.endKeylineSteps.back();
        CHECK(Feq(end[1].size + 8 + end[2].size + 8 + end[3].size, 444 - 16));
        CHECK(Feq(end[1].offset - end[1].size / 2, 0));
    }
}

// --- KeylineSnapPositionTest ----------------------------------------------------------------------

TEST(CarouselSnapPositions) {
    const auto positions = [](const Strategy& s, int count) {
        std::vector<int> v;
        for (int i = 0; i < count; ++i) v.push_back(SnapPositionOffset(s, i, count));
        return v;
    };
    {   // Centre aligned, one focal item (l=400, m=200, s=100).
        const Strategy s = Make(KeylineListOf(1000, 0, CarouselAlignment::Center,
                                              { { 5, true }, { 100 }, { 200 }, { 400 }, { 200 }, { 100 }, { 5, true } }), 1000);
        CHECK(positions(s, 6) == (std::vector<int>{ 0, 200, 300, 300, 400, 600 }));
    }
    {   // Centre aligned, two focal items (l=100, m=40, s=10).
        const float size = 10 + 40 + 100 + 100 + 40 + 10;
        const Strategy s = Make(KeylineListOf(size, 0, CarouselAlignment::Center,
                                              { { 5, true }, { 10 }, { 40 }, { 100 }, { 100 }, { 40 }, { 10 }, { 5, true } }), size);
        CHECK(positions(s, 10) == (std::vector<int>{ 0, 40, 50, 50, 50, 50, 50, 150, 160, 200 }));
    }
    {   // Start aligned.
        const Strategy s = Make(KeylineListOf(1000, 0, CarouselAlignment::Start,
                                              { { 5, true }, { 400 }, { 200 }, { 200 }, { 100 }, { 100 }, { 5, true } }), 1000);
        CHECK(positions(s, 6) == (std::vector<int>{ 0, 0, 100, 200, 400, 600 }));
    }
    {   // Start aligned, two focal items (l=400, m=125, s=75).
        const Strategy s = Make(KeylineListOf(1000, 0, CarouselAlignment::Start,
                                              { { 5, true }, { 400 }, { 400 }, { 125 }, { 75 }, { 5, true } }), 1000);
        CHECK(positions(s, 5) == (std::vector<int>{ 0, 0, 400, 475, 600 }));
        CHECK(positions(s, s.defaultKeylines.Size() - 2) == (std::vector<int>{ 0, 400, 475, 600 }));
        CHECK(positions(s, 1) == (std::vector<int>{ 0 }));
    }
}

// --- CarouselParallaxScrollEffectItemInfoTest ------------------------------------------------------

TEST(CarouselParallaxItemInfo) {
    const CarouselParallaxScrollEffectItemInfo info{ 20, 80, 0, 0 };
    CHECK(info.Size() == 60);
    CHECK((CarouselParallaxScrollEffectItemInfo{ 80, 20, 0, 0 }).Size() == 0);   // negative sizes clamp
    CHECK((CarouselParallaxScrollEffectItemInfo{ 10, 80, 15, 0.4f }) == (CarouselParallaxScrollEffectItemInfo{ 10, 80, 15, 0.4f }));
    CHECK(!((CarouselParallaxScrollEffectItemInfo{ 10, 80, 15, 0.4f }) == (CarouselParallaxScrollEffectItemInfo{ 10, 80, 15, 0.8f })));

    // A square item (intensity 1/4): half past the viewport start is masked and moved half way.
    using material3::internal::CalculateParallaxItemInfo;
    const auto start = CalculateParallaxItemInfo(true, 100, 100, -50, 0, 400);
    CHECK(Feq(start.maskProgress, 0.5f) && Feq(start.maskStart, 37.5f) && start.maskEnd == 100 && Feq(start.parallax, 12.5f));
    const auto end = CalculateParallaxItemInfo(true, 100, 100, 350, 0, 400);
    CHECK(Feq(end.maskProgress, 0.5f) && end.maskStart == 0 && Feq(end.maskEnd, 62.5f) && Feq(end.parallax, -12.5f));
    CHECK((CalculateParallaxItemInfo(true, 100, 100, 100, 0, 400) == CarouselParallaxScrollEffectItemInfo{ 0, 100, 0, 0 }));
    CHECK((CalculateParallaxItemInfo(false, 100, 100, 100, 0, 400) == CarouselParallaxScrollEffectItemInfo{}));
    // A wide item (16:9 and wider) masks by half.
    CHECK(Feq(CalculateParallaxItemInfo(true, 200, 100, -200, 0, 400).maskStart, 100));
}
