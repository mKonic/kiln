// M5 sample: a LazyColumn of 100 sections of 100 rows with sticky headers; only the visible
// rows exist. Tap a row to select it; the buttons animate the scroll to an item.
#include <kiln/kiln.hpp>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/runtime/Task.h>

using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    void ListScreen() {
        auto& c = *kiln::rt::Composition::Current();
        auto& state = c.RememberNew<LazyListState>({}, {});
        auto& selected = RememberState(-1);
        auto& flipped = RememberState(false);   // section 0 in reverse order (animateItem moves the rows)
        Column(Background(FillMax(Modifier{}, Axis::Both), Color(0xFFFEF7FFu)), [&] {
            Spacer(Height(Modifier{}, 40_dp));
            auto& jump = c.Remember<kiln::rt::Job>([] { return kiln::rt::Job{}; });
            Row(Padding(Modifier{}, 16_dp, 8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [&] {
                BasicText("First visible: " + std::to_string(state.FirstVisibleItemIndex()), Weight(Modifier{}, 1), { 18, Color(0xFF1D1B20u) });
                auto& flippedRef = flipped;
                Box(Clickable(Background(Modifier{}, Color(0xFF7D5260u), CornerShape::Pill()), [&flippedRef] { flippedRef.Set(!flippedRef.Peek()); }),
                    Alignment::Center(), false, [] {
                    BasicText("Flip", Padding(Modifier{}, 16_dp, 8_dp), { 14, Color(0xFFFFFFFFu) });
                });
                for (int target : { 0, 5050 }) {
                    Box(Clickable(Background(Modifier{}, Color(0xFF6750A4u), CornerShape::Pill()), [&state, &jump, target] {
                            jump = kiln::rt::Launch(kiln::rt::Dispatcher::Main(), state.AnimateScrollToItem(target));
                        }), Alignment::Center(), false, [target] {
                        BasicText(target == 0 ? "Top" : "Item 5050", Padding(Modifier{}, 16_dp, 8_dp), { 14, Color(0xFFFFFFFFu) });
                    });
                }
            });
            LazyListOptions opt;
            opt.contentPadding = { 16_dp, 0_dp, 16_dp, 16_dp };
            opt.arrangement = Arrangement::SpacedBy(8_dp);
            LazyColumn(FillMax(Weight(Modifier{}, 1), Axis::Horizontal), state, [&](LazyListScope& s) {
                for (int section = 0; section < 100; ++section) {
                    s.StickyHeader([section](int) {
                        Box(Background(Height(FillMax(Modifier{}, Axis::Horizontal), 40_dp), Color(0xFFEADDFFu)), Alignment::CenterStart(), false, [section] {
                            BasicText("Section " + std::to_string(section), Padding(Modifier{}, 16_dp, 0_dp), { 16, Color(0xFF21005Du) });
                        });
                    }, kiln::rt::Key(-1 - section));
                    const bool flip = section == 0 && flipped.Get();
                    s.Items(100, [&, section, flip](int k) {
                        const int i = section * 100 + (flip ? 99 - k : k);
                        const bool on = selected.Get() == i;
                        const Color bg = AnimateColorAsState(on ? Color(0xFF6750A4u) : Color(0xFFF3EDF7u));
                        Box(Clickable(Background(AnimateItem(Height(FillMax(Modifier{}, Axis::Horizontal), 56_dp)), bg, CornerShape::Rounded(12_dp)),
                                      [&selected, i] { selected.Set(i); }),
                            Alignment::CenterStart(), false, [&, i, on] {
                            BasicText("Item " + std::to_string(i), Padding(Modifier{}, 16_dp, 0_dp),
                                      { 16, on ? Color(0xFFFFFFFFu) : Color(0xFF1D1B20u) });
                        });
                    }, [section, flip](int k) { return kiln::rt::Key(section * 100 + (flip ? 99 - k : k)); });
                }
            }, opt);
        });
    }
}

extern "C" void kiln_main(kiln_app* app) { kiln::SetContent(app, [] { ListScreen(); }); }
