#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/adaptive/ThreePaneScaffoldNavigator.h"

#include <chrono>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::material3;
using namespace kiln::material3::adaptive;

namespace {
    struct Harness {
        ui::Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        ui::IntSize window;
        std::int64_t frameNanos = 0;
        explicit Harness(ui::IntSize size = { 900, 600 }) : window(size) { owner.SetDensity({ 1, 1 }); }
        void Run(std::function<void()> content) {
            composition.SetContent([this, content = std::move(content)] {
                rt::Composition::Current()->Provide(ui::LocalDensity, owner.GetDensity(), content);
            });
            Pump();
        }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout(window);
            }
        }
        void Frames(int n) {
            auto& d = rt::Dispatcher::Main();
            for (int i = 0; i < n; ++i) {
                d.AdvanceTimeBy(std::chrono::milliseconds(16));
                frameNanos += 16'000'000;
                d.RunPending();
                d.RunFrame(frameNanos);
                Pump();
            }
        }
    };

    PaneScaffoldDirective Directive(int partitions) {
        PaneScaffoldDirective d;
        d.maxHorizontalPartitions = partitions;
        d.horizontalPartitionSpacerSize = ui::Dp(partitions > 1 ? 24.0f : 0.0f);
        return d;
    }

    // A pane's content that records where it is (root pixels) and how wide.
    struct Probe {
        ui::Rect bounds{ -1, -1, -1, -1 };
        bool composed = false;
        Modifier Track() {
            return OnGloballyPositioned(FillMax(Modifier{}, Axis::Both), [this](const LayoutCoordinates& c) { bounds = c.BoundsInRoot(); });
        }
    };
}

TEST(PaneMotionsSlideFromTheSideNothingBlocks) {
    // List-detail order (list, detail, extra), one pane at a time: the list leaves to the left
    // while the detail comes in from the right.
    const ThreePaneScaffoldValue list{ PaneAdaptedValue::Hidden(), PaneAdaptedValue::Expanded(), PaneAdaptedValue::Hidden() };
    const ThreePaneScaffoldValue detail{ PaneAdaptedValue::Expanded(), PaneAdaptedValue::Hidden(), PaneAdaptedValue::Hidden() };
    const auto order = ListDetailPaneScaffoldDefaults::PaneOrder;
    const auto motions = CalculatePaneMotion(list, detail, order);
    CHECK(motions[0] == PaneMotion::ExitToLeft);       // list
    CHECK(motions[1] == PaneMotion::EnterFromRight);   // detail
    CHECK(motions[2] == PaneMotion::NoMotion);
    // Two panes, the extra one joining: the list stays (bounds), the detail shrinks out between
    // panes that stay... here the detail stays and the extra enters from the right.
    const ThreePaneScaffoldValue two{ PaneAdaptedValue::Expanded(), PaneAdaptedValue::Expanded(), PaneAdaptedValue::Hidden() };
    const ThreePaneScaffoldValue withExtra{ PaneAdaptedValue::Expanded(), PaneAdaptedValue::Hidden(), PaneAdaptedValue::Expanded() };
    const auto m2 = CalculatePaneMotion(two, withExtra, order);
    CHECK(m2[0] == PaneMotion::ExitToLeft);
    CHECK(m2[1] == PaneMotion::AnimateBounds);
    CHECK(m2[2] == PaneMotion::EnterFromRight);
}

TEST(ListDetailScaffoldSplitsTheWidthByPreferredWidthsAndPriority) {
    Harness h;
    Probe list, detail;
    const ThreePaneScaffoldValue value{ PaneAdaptedValue::Expanded(), PaneAdaptedValue::Expanded(), PaneAdaptedValue::Hidden() };
    h.Run([&] {
        MaterialTheme([&] {
            ListDetailPaneScaffold(Directive(2), value,
                [&](const ThreePaneScaffoldPaneScope& s) {
                    AnimatedPane(s, s.PreferredWidth(Modifier{}, ui::Dp(200)), {}, [&] { Box(list.Track()); });
                },
                [&](const ThreePaneScaffoldPaneScope& s) { AnimatedPane(s, [&] { Box(detail.Track()); }); });
        });
    });
    // The list keeps its 200; the detail (the higher priority) takes the rest after the spacer.
    CHECK_EQ(list.bounds.left, 0.0f);
    CHECK_EQ(list.bounds.right, 200.0f);
    CHECK_EQ(detail.bounds.left, 224.0f);
    CHECK_EQ(detail.bounds.right, 900.0f);
}

TEST(PaneExpansionSplitsTwoPanesByProportion) {
    Harness h;
    Probe list, detail;
    PaneExpansionState* expansion = nullptr;
    const ThreePaneScaffoldValue value{ PaneAdaptedValue::Expanded(), PaneAdaptedValue::Expanded(), PaneAdaptedValue::Hidden() };
    h.Run([&] {
        MaterialTheme([&] {
            auto& state = RememberPaneExpansionState(value);
            expansion = &state;
            ListDetailPaneScaffoldOptions options;
            options.paneExpansionState = &state;
            ListDetailPaneScaffold(Directive(2), value,
                [&](const ThreePaneScaffoldPaneScope& s) { AnimatedPane(s, [&] { Box(list.Track()); }); },
                [&](const ThreePaneScaffoldPaneScope& s) { AnimatedPane(s, [&] { Box(detail.Track()); }); }, {}, options);
        });
    });
    expansion->SetFirstPaneProportion(0.25f);
    h.Pump();
    // 25% of the width without the spacer: 219; the detail after the spacer.
    CHECK_EQ(list.bounds.right, 219.0f);
    CHECK_EQ(detail.bounds.left, 243.0f);
}

TEST(NavigatorShowsTheDetailThenGoesBack) {
    Harness h(ui::IntSize{ 400, 600 });
    Probe list, detail;
    ThreePaneScaffoldNavigator<int>* navigator = nullptr;
    h.Run([&] {
        MaterialTheme([&] {
            auto& n = RememberListDetailPaneScaffoldNavigator<int>(Directive(1));
            navigator = &n;
            ListDetailPaneScaffold(n.ScaffoldDirective(), n.ScaffoldState(),
                [&](const ThreePaneScaffoldPaneScope& s) { AnimatedPane(s, [&] { list.composed = true; Box(list.Track()); }); },
                [&](const ThreePaneScaffoldPaneScope& s) { AnimatedPane(s, [&] { detail.composed = true; Box(detail.Track()); }); });
        });
    });
    // One pane: the list first.
    CHECK(navigator->ScaffoldValue().secondary.kind == PaneAdaptedValue::Kind::Expanded);
    CHECK(navigator->ScaffoldValue().primary.kind == PaneAdaptedValue::Kind::Hidden);
    CHECK(!navigator->CanNavigateBack());
    CHECK_EQ(list.bounds.right, 400.0f);
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), navigator->NavigateTo(ListDetailPaneScaffoldRole::Detail, 2));
    h.Frames(60);
    CHECK(navigator->CurrentDestination()->contentKey == std::optional<int>(2));
    CHECK(navigator->ScaffoldValue().primary.kind == PaneAdaptedValue::Kind::Expanded);
    CHECK(navigator->ScaffoldState().CurrentState() == navigator->ScaffoldValue());
    CHECK_EQ(detail.bounds.left, 0.0f);
    CHECK_EQ(detail.bounds.right, 400.0f);
    CHECK(navigator->CanNavigateBack());
    job = rt::Launch(rt::Dispatcher::Main(), [](ThreePaneScaffoldNavigator<int>* n) -> rt::Task<> { co_await n->NavigateBack(); }(navigator));
    h.Frames(60);
    CHECK(navigator->ScaffoldValue().secondary.kind == PaneAdaptedValue::Kind::Expanded);
    CHECK(!navigator->CanNavigateBack());
}

TEST(NavigatorSlidesTheDetailInAsTheListLeaves) {
    // One pane: the detail was composed hidden (no motion), so its enter slide only appears with
    // the navigation; mid-way both panes are partly on screen, side by side.
    Harness h(ui::IntSize{ 400, 600 });
    Probe list, detail;
    ThreePaneScaffoldNavigator<int>* navigator = nullptr;
    h.Run([&] {
        MaterialTheme([&] {
            auto& n = RememberListDetailPaneScaffoldNavigator<int>(Directive(1));
            navigator = &n;
            ListDetailPaneScaffold(n.ScaffoldDirective(), n.ScaffoldState(),
                [&](const ThreePaneScaffoldPaneScope& s) { AnimatedPane(s, [&] { Box(list.Track()); }); },
                [&](const ThreePaneScaffoldPaneScope& s) { AnimatedPane(s, [&] { Box(detail.Track()); }); });
        });
    });
    rt::Job job = rt::Launch(rt::Dispatcher::Main(), navigator->NavigateTo(ListDetailPaneScaffoldRole::Detail, 2));
    h.Frames(8);
    CHECK(list.bounds.left < -50.0f);
    CHECK(detail.bounds.left > 50.0f && detail.bounds.left < 350.0f);
    CHECK_EQ(detail.bounds.left, list.bounds.right);
    h.Frames(60);
    CHECK_EQ(detail.bounds.left, 0.0f);
}
