// Ported from constraintlayout-compose MultiMeasureCompositionTest.kt (androidx): the composition
// source pattern MotionLayout measures by (the content recomposing, or the caller), on a layout
// that measures its children twice. Expected values are upstream's.
#include "ConstraintLayoutComposeSupport.h"

#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/Composition.h"

#include <algorithm>

using namespace cltest;

namespace {

    constexpr int HEIGHT_FROM_CONTENT = 40;
    constexpr int HEIGHT_FROM_CALLER = 80;

    enum class CompositionSource { Unknown, Caller, Content };

    struct Tracker {
        rt::State<int, rt::NeverEqual> compTracker{ 0 };
        CompositionSource compSource = CompositionSource::Unknown;
    };

    class MaxWidthPolicy final : public ui::MeasurePolicy {
    public:
        explicit MaxWidthPolicy(std::shared_ptr<Tracker> tracker) : m_Tracker(std::move(tracker)) {}
        ui::MeasureResult Measure(ui::MeasureScope& scope, std::span<ui::Measurable* const> measurables, const ui::Constraints& constraints) override {
            (void)m_Tracker->compTracker.Get();
            int height = 0;
            switch (m_Tracker->compSource) {
                case CompositionSource::Content: height = HEIGHT_FROM_CONTENT; break;
                case CompositionSource::Caller: height = HEIGHT_FROM_CALLER; break;
                case CompositionSource::Unknown: height = 0; break;
            }
            m_Tracker->compSource = CompositionSource::Unknown;
            int maxWrapWidth = 0;
            for (ui::Measurable* m : measurables) {
                ui::Constraints c = constraints;
                c.minWidth = 0;
                c.minHeight = height;
                maxWrapWidth = std::max(maxWrapWidth, m->Measure(c).Width());
            }
            std::vector<ui::Placeable*> placeables;
            for (ui::Measurable* m : measurables) {
                ui::Constraints c = constraints;
                c.minWidth = maxWrapWidth;
                c.minHeight = height;
                placeables.push_back(&m->Measure(c));
            }
            int layoutHeight = 0;
            for (ui::Placeable* p : placeables) layoutHeight += p->Height();
            return scope.Layout(maxWrapWidth, layoutHeight, [placeables](ui::PlacementScope& place) {
                int y = 0;
                for (ui::Placeable* p : placeables) {
                    place.Place(*p, 0, y);
                    y += p->Height();
                }
            });
        }

    private:
        std::shared_ptr<Tracker> m_Tracker;
    };

    // MaxWrapContentWithMultiMeasure: the caller composing marks Caller; the content composing
    // marks Content unless the caller already did.
    void MaxWrapContentWithMultiMeasure(kiln::Function<void()> content) {
        auto* c = rt::Composition::Current();
        std::shared_ptr<Tracker> tracker = c->Remember<std::shared_ptr<Tracker>>([] { return std::make_shared<Tracker>(); });
        std::shared_ptr<MaxWidthPolicy> policy = c->Remember<std::shared_ptr<MaxWidthPolicy>>([&] { return std::make_shared<MaxWidthPolicy>(tracker); });
        tracker->compSource = CompositionSource::Caller;
        foundation::Layout(Modifier{}, policy, [tracker, content = std::move(content)] {
            rt::Observation::Untracked([&] { tracker->compTracker.Set(tracker->compTracker.Get() + 1); });
            if (tracker->compSource == CompositionSource::Unknown) tracker->compSource = CompositionSource::Content;
            content();
        });
    }

}

TEST(ClMultiMeasureComposition_testCustomMultiMeasure_changesFromCompositionSource) {
    test::ComposeUiTest t;
    const Px px{ t.Density() };
    int callerCompositionCount = 0, contentCompositionCount = 0;
    const int minWidth = 40, baseWidth = 10;
    rt::State<int> widthMultiplier(4);
    rt::State<int> unusedValue(0);
    Content(t, [&] {
        ++callerCompositionCount;
        (void)unusedValue.Get();
        MaxWrapContentWithMultiMeasure([&] {
            ++contentCompositionCount;
            Box(foundation::Background(foundation::TestTag(foundation::Width({}, px(float(widthMultiplier.Get() * baseWidth))), "box0"), ui::Color::Red()));
            Box(foundation::Background(foundation::TestTag(foundation::Width({}, px(float(minWidth))), "box1"), ui::Color::Blue()));
        });
    });
    auto checkBoxes = [&](float width, int height) {
        CHECK(PositionIs(t, "box0", 0, 0));
        CHECK(WidthIs(t, "box0", px.dp(width)));
        CHECK(HeightIs(t, "box0", px.dp(float(height))));
        CHECK(PositionIs(t, "box1", 0, px.dp(float(height))));
        CHECK(WidthIs(t, "box1", px.dp(width)));
        CHECK(HeightIs(t, "box1", px.dp(float(height))));
    };
    t.WaitForIdle();
    checkBoxes(float(minWidth), HEIGHT_FROM_CALLER);
    t.RunOnIdle([&] { widthMultiplier.Set(widthMultiplier.Get() + 1); });
    t.WaitForIdle();
    checkBoxes(50, HEIGHT_FROM_CONTENT);
    t.RunOnIdle([&] { widthMultiplier.Set(3); });
    t.WaitForIdle();
    checkBoxes(float(minWidth), HEIGHT_FROM_CONTENT);
    t.RunOnIdle([&] { unusedValue.Set(1); });
    t.WaitForIdle();
    checkBoxes(float(minWidth), HEIGHT_FROM_CALLER);
    t.RunOnIdle([&] {
        CHECK_EQ(2, callerCompositionCount);
        CHECK_EQ(4, contentCompositionCount);
    });
}
