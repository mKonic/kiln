// M5 sample: a LazyColumn of 10,000 rows; only the visible ones exist. Tap a row to select it.
#include <kiln/kiln.hpp>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Lazy.h>

using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    void ListScreen() {
        auto& c = *kiln::rt::Composition::Current();
        auto& state = c.RememberNew<LazyListState>({}, {});
        auto& selected = RememberState(-1);
        Column(Background(FillMax(Modifier{}, Axis::Both), Color(0xFFFEF7FFu)), [&] {
            Spacer(Height(Modifier{}, 40_dp));
            BasicText("First visible: " + std::to_string(state.FirstVisibleItemIndex()),
                      Padding(Modifier{}, 16_dp, 8_dp), { 22, Color(0xFF1D1B20u) });
            LazyListOptions opt;
            opt.contentPadding = { 16_dp, 8_dp, 16_dp, 16_dp };
            opt.spacing = 8_dp;
            LazyColumn(FillMax(Weight(Modifier{}, 1), Axis::Horizontal), state, [&](LazyListScope& s) {
                s.Items(10000, [&](int i) {
                    const bool on = selected.Get() == i;
                    const Color bg = AnimateColorAsState(on ? Color(0xFF6750A4u) : Color(0xFFF3EDF7u));
                    Box(Clickable(Background(Height(FillMax(Modifier{}, Axis::Horizontal), 56_dp), bg, CornerShape::Rounded(12_dp)),
                                  [&selected, i] { selected.Set(i); }),
                        Alignment::CenterStart(), false, [&, i, on] {
                        BasicText("Item " + std::to_string(i), Padding(Modifier{}, 16_dp, 0_dp),
                                  { 16, on ? Color(0xFFFFFFFFu) : Color(0xFF1D1B20u) });
                    });
                }, [](int i) { return kiln::rt::Key(i); });
            }, opt);
        });
    }
}

extern "C" void kiln_main(kiln_app* app) { kiln::SetContent(app, [] { ListScreen(); }); }
