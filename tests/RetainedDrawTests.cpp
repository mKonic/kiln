// Retained drawing: a node records its drawing once and replays it until something it draws
// from changes; its children draw live from the recording.
#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Layouts.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Observation::ApplyChanges();
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 400, 400 });
            }
        }
        void Draw() { owner.Draw(DrawTarget{}); }
    };

    // Counts how often its draw lambda runs, and where the replay puts it.
    struct Probe {
        int draws = 0;
        float x = 0;
        Modifier On(Modifier m) {
            return DrawBehind(std::move(m), [this](DrawScope& s) {
                ++draws;
                s.Record([this](DrawScope& d) { x = d.CurrentTransform().e; });
            });
        }
    };
}

TEST(RetainedDrawRecordsOnceAndReplays) {
    Harness h;
    Probe a;
    h.Run([&] { Box(a.On(SizeMod(Modifier{}, Dp(10), Dp(10)))); });
    h.Draw();
    h.Draw();
    h.Draw();
    CHECK_EQ(a.draws, 1);
}

TEST(RetainedDrawReRecordsOnlyTheNodeWhoseDrawReadChanged) {
    Harness h;
    Probe parent, other;
    rt::State<float> tint(1);
    int tinted = 0;
    h.Run([&] {
        Column(parent.On(Modifier{}), [&] {
            Box(DrawBehind(SizeMod(Modifier{}, Dp(10), Dp(10)), [&](DrawScope&) { ++tinted; (void)tint.Get(); }));
            Box(other.On(SizeMod(Modifier{}, Dp(10), Dp(10))));
        });
    });
    h.Draw();
    CHECK_EQ(tinted, 1);
    tint.Set(2);
    h.Pump();
    h.Draw();
    CHECK_EQ(tinted, 2);        // the reader recorded again
    CHECK_EQ(parent.draws, 1);  // its parent and sibling replayed
    CHECK_EQ(other.draws, 1);
}

TEST(RetainedDrawMovesChildrenWithoutRecording) {
    Harness h;
    Probe child;
    rt::State<int> x(0);
    h.Run([&] {
        // Read in placement only: the move needs no recomposition.
        Box(OffsetMod(Modifier{}, [&](const Density&) { return IntOffset{ x.Get(), 0 }; }), Alignment::TopStart(), false, [&] {
            Box(child.On(SizeMod(Modifier{}, Dp(10), Dp(10))));
        });
    });
    h.Draw();
    CHECK_EQ(child.x, 0.0f);
    x.Set(30);
    h.Pump();
    h.Draw();
    CHECK_EQ(child.draws, 1);   // moved, not recorded again
    CHECK_EQ(child.x, 30.0f);   // drawn where it is now
}

TEST(RetainedDrawRecordsAgainWhenTheSizeChanges) {
    Harness h;
    Probe p;
    rt::State<float> w(10);
    float seen = 0;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {   // loose constraints: the size is its own
            Box(DrawBehind(p.On(SizeMod(Modifier{}, Dp(w.Get()), Dp(10))), [&](DrawScope& s) { seen = s.GetSize().width; }));
        });
    });
    h.Draw();
    w.Set(25);
    h.Pump();
    h.Draw();
    CHECK_EQ(p.draws, 2);
    CHECK_EQ(seen, 25.0f);
}

TEST(RetainedDrawCanBeTurnedOff) {
    Harness h;
    Probe p;
    h.Run([&] { Box(p.On(SizeMod(Modifier{}, Dp(10), Dp(10)))); });
    Owner::SetDrawRetention(false);
    h.Draw();
    h.Draw();
    Owner::SetDrawRetention(true);
    CHECK_EQ(p.draws, 2);
}

#include "kiln/ui/InsetsRulers.h"

TEST(RetainedDrawFollowsAPlacementReadFromAnInsetsRuler) {
    // A child placed at the status bar ruler (an immersive layout): the insets animating moves
    // it frame by frame, replayed where it is now without recording again.
    Harness h;
    Probe child;
    float y = -1;
    auto& insets = h.owner.Insets();
    insets.Set(InsetsType::StatusBars, { 0, 0, 0, 0 });
    insets.SetHasInsets(true);
    auto policy = std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
        std::vector<Placeable*> placed;
        for (auto* m : children) placed.push_back(&m->Measure(c.CopyMaxDimensions()));
        return s.Layout(c.maxWidth, c.maxHeight, [placed](PlacementScope& ps) {
            const float top = ps.Current(WindowInsetsRulers::StatusBars().Current().top, 0);
            for (auto* p : placed) ps.Place(*p, 0, int(top));
        });
    });
    h.Run([&] {
        Layout(Modifier{}, policy, [&] {
            Box(DrawBehind(child.On(SizeMod(Modifier{}, Dp(10), Dp(10))), [&](DrawScope& s) {
                s.Record([&](DrawScope& d) { y = d.CurrentTransform().f; });
            }));
        });
    });
    h.Draw();
    CHECK_EQ(y, 0.0f);
    for (int top : { 12, 40, 107 }) {
        insets.Set(InsetsType::StatusBars, { 0, top, 0, 0 });
        h.Pump();
        h.Draw();
        CHECK_EQ(y, float(top));
    }
    CHECK_EQ(child.draws, 1);
}
