#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Subcompose.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        Harness() { owner.SetDensity({ 1, 1 }); }
        void Run(std::function<void()> content) {
            composition.SetContent(std::move(content));
            Frame();
        }
        void Frame() {
            rt::Composition::RecomposeAll();
            owner.MeasureAndLayout({ 400, 400 });
            rt::Composition::RecomposeAll();
            owner.MeasureAndLayout({ 400, 400 });
        }
    };

    // A fixed-size layout that records whether each measure was the lookahead pass.
    std::shared_ptr<MeasurePolicy> Recording(std::vector<bool>& passes, std::function<IntSize()> size) {
        return std::make_shared<LambdaMeasurePolicy>([&passes, size](MeasureScope& s, std::span<Measurable* const>, const Constraints&) {
            passes.push_back(s.IsLookingAhead());
            const IntSize z = size();
            return s.Layout(z.width, z.height);
        });
    }

    // Approaches the lookahead size linearly with `progress` (0: `from`, 1: the lookahead size),
    // placing at the lookahead offset moved back by (1 - progress) of the change.
    struct Approach {
        rt::State<float>* progress;
        IntSize from;
        IntSize* lastLookaheadSize = nullptr;
        Modifier Apply(Modifier m) const {
            const Approach self = *this;
            return ApproachLayout(
                std::move(m), [self](IntSize) { return self.progress->Get() < 1; }, {},
                [self](ApproachMeasureScope& s, Measurable& wrapped, const Constraints&) {
                    if (self.lastLookaheadSize) *self.lastLookaheadSize = s.lookaheadSize;
                    const float t = self.progress->Get();
                    const int w = int(float(self.from.width) + (float(s.lookaheadSize.width - self.from.width)) * t);
                    const int h = int(float(self.from.height) + (float(s.lookaheadSize.height - self.from.height)) * t);
                    Placeable& p = wrapped.Measure(Constraints::Fixed(w, h));
                    return s.Layout(w, h, [&p](PlacementScope& place) { place.Place(p, 0, 0); });
                });
        }
    };
}

TEST(LookaheadScopeChildrenFoldIntoTheParent) {
    Harness h;
    LayoutNode* row = nullptr;
    h.Run([&] {
        row = &Layout(Modifier{}, std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) {
            std::vector<Placeable*> ps;
            int x = 0;
            for (Measurable* m : ch) { ps.push_back(&m->Measure(Constraints{ 0, c.maxWidth, 0, c.maxHeight })); }
            return s.Layout(200, 50, [ps, x](PlacementScope& place) mutable {
                for (Placeable* p : ps) { place.Place(*p, x, 0); x += p->Width(); }
            });
        }), [] {
            foundation::LookaheadScope([](ui::LookaheadScope&) {
                Box(SizeMod(Modifier{}, Dp(10), Dp(10)));
                Box(SizeMod(Modifier{}, Dp(20), Dp(20)));
            });
            Box(SizeMod(Modifier{}, Dp(5), Dp(5)));
        });
    });
    CHECK_EQ(int(row->Children().size()), 3);
    CHECK_EQ(row->Children()[1]->Position().x, 10);
    CHECK_EQ(row->Children()[2]->Position().x, 30);
    // The scope's layouts are lookahead roots' subtrees; the one beside them is not.
    CHECK(row->Children()[0]->LookaheadRoot() != nullptr);
    CHECK(row->Children()[0]->IsOutMostLookaheadRoot());
    CHECK(row->Children()[2]->LookaheadRoot() == nullptr);
}

TEST(LookaheadPassMeasuresBeforeTheMainPass) {
    Harness h;
    std::vector<bool> inside, outside;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            foundation::LookaheadScope([&](ui::LookaheadScope&) {
                Box(Modifier{}, Alignment::TopStart(), false, [&] { Layout(Modifier{}, Recording(inside, [] { return IntSize{ 10, 10 }; })); });
            });
            Layout(Modifier{}, Recording(outside, [] { return IntSize{ 10, 10 }; }));
        });
    });
    CHECK_EQ(int(inside.size()), 2);
    CHECK(inside[0]);
    CHECK(!inside[1]);
    CHECK_EQ(int(outside.size()), 1);
    CHECK(!outside[0]);
}

TEST(ApproachMeasureMovesTowardTheLookaheadSize) {
    Harness h;
    rt::State<float> progress(0);
    rt::State<int> target(100);
    IntSize lookaheadSize;
    LayoutNode* box = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            foundation::LookaheadScope([&](ui::LookaheadScope&) {
                Approach a{ &progress, { 20, 20 }, &lookaheadSize };
                box = &Layout(SizeMod(a.Apply(Modifier{}), Dp(float(target.Get())), Dp(float(target.Get()))),
                              std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const>, const Constraints& c) {
                                  return s.Layout(c.minWidth, c.minHeight);
                              }));
            });
        });
    });
    CHECK_EQ(lookaheadSize.width, 100);
    CHECK_EQ(box->Size().width, 20);
    CHECK_EQ(box->LookaheadSize()->width, 100);
    progress.Set(0.5f);
    h.Frame();
    CHECK_EQ(box->Size().width, 60);
    // Done: the approach is complete and the size is the lookahead one.
    progress.Set(1);
    h.Frame();
    CHECK_EQ(box->Size().width, 100);
    // A new target: lookahead jumps, the main pass follows the approach.
    target.Set(200);
    progress.Set(0);
    h.Frame();
    CHECK_EQ(box->LookaheadSize()->width, 200);
    CHECK_EQ(box->Size().width, 20);
}

TEST(ApproachModifierOutsideAScopeMakesItsNodeALookaheadRoot) {
    Harness h;
    rt::State<float> progress(0);
    LayoutNode* box = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            Approach a{ &progress, { 10, 10 } };
            box = &Layout(SizeMod(a.Apply(Modifier{}), Dp(50), Dp(50)), std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const>, const Constraints& c) {
                return s.Layout(c.minWidth, c.minHeight);
            }));
        });
    });
    CHECK(box->LookaheadRoot() == box);
    CHECK_EQ(box->Size().width, 10);
    CHECK_EQ(box->LookaheadSize()->width, 50);
}

TEST(LookaheadOffsetsAreTheDestinationPositions) {
    // A column whose first item grows: in lookahead the second item is already below it; the
    // approach keeps it where it was until the animation runs.
    Harness h;
    rt::State<int> first(30);
    rt::State<float> progress(1);
    Offset lookaheadOffset, currentOffset;
    int placements = 0;
    h.Run([&] {
        foundation::LookaheadScope([&](ui::LookaheadScope& scope) {
            Column(Modifier{}, [&] {
                Box(SizeMod(Modifier{}, Dp(10), Dp(float(first.Get()))));
                Layout(ApproachLayout(
                           SizeMod(Modifier{}, Dp(10), Dp(10)), [&](IntSize) { return progress.Get() < 1; },
                           [&](PlacementScope&, const LayoutCoordinates&) { return progress.Get() < 1; },
                           [&](ApproachMeasureScope& s, Measurable& m, const Constraints& c) {
                               Placeable& p = m.Measure(c);
                               return s.Layout(p.Width(), p.Height(), [&, &p](PlacementScope& place) {
                                   ++placements;
                                   const LayoutCoordinates scopeCoords = scope.LookaheadScopeCoordinates(place);
                                   lookaheadOffset = scope.LocalLookaheadPositionOf(scopeCoords, *place.Coordinates());
                                   currentOffset = scopeCoords.LocalPositionOf(*place.Coordinates());
                                   place.Place(p, 0, 0);
                               });
                           }),
                       std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const>, const Constraints& c) {
                           return s.Layout(c.minWidth, c.minHeight);
                       }));
            });
        });
    });
    CHECK_EQ(lookaheadOffset.y, 30.f);
    CHECK_EQ(currentOffset.y, 30.f);
    first.Set(80);
    progress.Set(0);
    h.Frame();
    CHECK_EQ(lookaheadOffset.y, 80.f);
    CHECK_EQ(currentOffset.y, 80.f);
    CHECK(placements >= 2);
}

TEST(CompleteApproachPlacesAtTheLookaheadOffset) {
    // An approach placing its content off by 7 px snaps to the lookahead offset once complete.
    Harness h;
    rt::State<float> progress(0);
    LayoutNode* inner = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            foundation::LookaheadScope([&](ui::LookaheadScope&) {
                Box(ApproachLayout(
                        Modifier{}, [&](IntSize) { return progress.Get() < 1; }, {},
                        [&](ApproachMeasureScope& s, Measurable& m, const Constraints& c) {
                            Placeable& p = m.Measure(c);
                            return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.Place(p, 7, 0); });
                        }),
                    Alignment::TopStart(), false, [&] {
                        inner = &Layout(SizeMod(Modifier{}, Dp(10), Dp(10)), std::make_shared<LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const>, const Constraints& c) {
                            return s.Layout(c.minWidth, c.minHeight);
                        }));
                    });
            });
        });
    });
    CHECK_EQ(int(inner->PositionInRoot().x), 7);
    progress.Set(1);
    h.Frame();
    CHECK_EQ(int(inner->PositionInRoot().x), 0);
}

TEST(LookaheadScopeComesAndGoes) {
    Harness h;
    rt::State<bool> shown(true);
    rt::State<int> count(2);
    LayoutNode* column = nullptr;
    h.Run([&] {
        Column(Modifier{}, [&] {
            if (shown.Get())
                foundation::LookaheadScope([&](ui::LookaheadScope&) {
                    for (int i = 0; i < count.Get(); ++i) Box(SizeMod(Modifier{}, Dp(10), Dp(10)), Alignment::TopStart(), false, {}, rt::Key(i));
                });
            Box(SizeMod(Modifier{}, Dp(10), Dp(5)));
        });
    });
    column = h.owner.Root().Children().front();
    CHECK_EQ(int(column->Children().size()), 3);
    CHECK_EQ(column->Children()[2]->Position().y, 20);
    count.Set(3);
    h.Frame();
    CHECK_EQ(int(column->Children().size()), 4);
    CHECK_EQ(column->Children()[3]->Position().y, 30);
    CHECK(column->Children()[2]->LookaheadRoot() != nullptr);
    shown.Set(false);
    h.Frame();
    CHECK_EQ(int(column->Children().size()), 1);
    CHECK_EQ(column->Children()[0]->Position().y, 0);
    CHECK(column->Children()[0]->LookaheadRoot() == nullptr);
    shown.Set(true);
    h.Frame();
    CHECK_EQ(int(column->Children().size()), 4);
    CHECK(column->Children()[0]->IsOutMostLookaheadRoot());
}

TEST(SubcomposeInLookaheadKeepsApproachOnlySlotsUntilUnused) {
    // Lookahead composes slot 1; the approach also wants slot 2 (an exiting item, say). Slot 2
    // lives while the approach uses it and goes when it stops.
    Harness h;
    rt::State<bool> approachWantsTwo(true);
    int composedTwo = 0, disposedTwo = 0;
    LayoutNode* sub = nullptr;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            foundation::LookaheadScope([&](ui::LookaheadScope&) {
                SubcomposeLayout(Modifier{}, [&](SubcomposeMeasureScope& s, const Constraints& c) {
                    std::vector<Placeable*> ps;
                    for (Measurable* m : s.Subcompose(rt::Key(1), [] { Box(SizeMod(Modifier{}, Dp(10), Dp(10))); })) ps.push_back(&m->Measure(c));
                    if (!s.IsLookingAhead() && approachWantsTwo.Get())
                        for (Measurable* m : s.Subcompose(rt::Key(2), [&] {
                                 auto* comp = rt::Composition::Current();
                                 comp->Remember<int>([&] { ++composedTwo; return 0; });
                                 comp->DisposableEffect({}, [&] { return [&] { ++disposedTwo; }; });
                                 Box(SizeMod(Modifier{}, Dp(20), Dp(20)));
                             }))
                            ps.push_back(&m->Measure(c));
                    return s.Layout(50, 50, [ps](PlacementScope& place) { for (Placeable* p : ps) place.Place(*p, 0, 0); });
                });
            });
        });
        sub = nullptr;
    });
    sub = h.owner.Root().Children().front()->Children().front();
    CHECK_EQ(composedTwo, 1);
    CHECK_EQ(disposedTwo, 0);
    CHECK_EQ(int(sub->Children().size()), 2);
    h.Frame();
    CHECK_EQ(composedTwo, 1);
    approachWantsTwo.Set(false);
    h.Frame();
    CHECK_EQ(disposedTwo, 1);
    CHECK_EQ(int(sub->Children().size()), 1);
}
