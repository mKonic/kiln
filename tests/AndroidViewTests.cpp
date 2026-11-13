// AndroidView against a fake host: the View is made on attach, measured, placed, handed the
// touches on its element, updated when what update reads changes, and released when it leaves
// (or kept and reset when its lazy item is reused, with an onReset).
#include "Check.h"
#include <kiln/Function.h>

#include "kiln/foundation/AndroidView.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Lazy.h"

#include <map>
#include <string>
#include <vector>

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::ui;

namespace {
    struct FakeView { int made = 0; int updates = 0; int resets = 0; bool released = false; };

    struct Harness {
        Owner owner;
        rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
        std::map<int, FakeView> views;
        std::vector<std::string> log;
        std::map<int, IntRect> placed;
        std::map<int, bool> visible;
        bool takeTouches = true;
        std::vector<std::pair<Offset, bool>> touches;
        int made = 0;
        Harness() {
            owner.SetDensity({ 1, 1 });
            InteropViewHost host;
            host.create = [this](int id, const InteropViewFactory& f) {
                f(nullptr, nullptr);
                views[id].made = ++made;
                log.push_back("create " + std::to_string(id));
                return true;
            };
            host.with = [this](int id, const InteropViewBlock& b) { b(&views[id], &views[id]); };
            host.release = [this](int id) { views[id].released = true; log.push_back("release " + std::to_string(id)); };
            host.measure = [](int, const Constraints& c) { return IntSize{ std::min(c.maxWidth, 60), std::min(c.maxHeight, 40) }; };
            host.place = [this](int id, IntRect r, bool v) { placed[id] = r; visible[id] = v; };
            host.touch = [this](int, Offset origin, bool cancel) { touches.push_back({ origin, cancel }); return takeTouches; };
            owner.SetInteropViewHost(std::move(host));
        }
        ~Harness() { composition.SetContent([] {}); Pump(); }
        void Run(kiln::Function<void()> content) { composition.SetContent(std::move(content)); Pump(); }
        void Pump() {
            for (int i = 0; i < 3; ++i) {
                rt::Observation::ApplyChanges();
                rt::Dispatcher::Main().RunPending();
                rt::Composition::RecomposeAll();
                owner.MeasureAndLayout({ 200, 200 });
            }
        }
        void Touch(PointerEventType type, float x, float y, bool down) {
            RawPointerEvent e;
            e.type = type;
            e.pointers.push_back({ 1, { x, y }, down, PointerType::Touch, {} });
            owner.ProcessPointer(e);
            Pump();
        }
    };

    AndroidViewOptions Counting(rt::State<int>* read = nullptr) {
        AndroidViewOptions o;
        o.update = [read](void*, void* v) { ++static_cast<FakeView*>(v)->updates; if (read) (void)read->Get(); };
        return o;
    }
}

TEST(AndroidViewIsMadeMeasuredAndPlaced) {
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            AndroidView([](void*, void*) { return nullptr; }, OffsetMod(Modifier{}, Dp(10), Dp(20)), Counting());
        });
    });
    CHECK(h.log == std::vector<std::string>{ "create 1" });
    CHECK_EQ(h.views[1].updates >= 1, true);
    CHECK(h.visible[1]);
    CHECK_EQ(h.placed[1].left, 10);
    CHECK_EQ(h.placed[1].top, 20);
    CHECK_EQ(h.placed[1].right - h.placed[1].left, 60);   // what the View measured
    CHECK_EQ(h.placed[1].bottom - h.placed[1].top, 40);
}

TEST(AndroidViewUpdatesWhenWhatItReadsChanges) {
    Harness h;
    rt::State<int> url(1);
    h.Run([&] { AndroidView([](void*, void*) { return nullptr; }, Modifier{}, Counting(&url)); });
    const int before = h.views[1].updates;
    url.Set(2);   // read only inside update: no recomposition, the update runs again
    h.Pump();
    CHECK_EQ(h.views[1].updates, before + 1);
}

TEST(AndroidViewGetsTheTouchesOnItsElement) {
    Harness h;
    h.Run([&] {
        Box(Modifier{}, Alignment::TopStart(), false, [&] {
            AndroidView([](void*, void*) { return nullptr; }, OffsetMod(Modifier{}, Dp(10), Dp(20)));
        });
    });
    h.Touch(PointerEventType::Press, 30, 30, true);
    h.Touch(PointerEventType::Move, 35, 35, true);
    h.Touch(PointerEventType::Release, 35, 35, false);
    CHECK_EQ(h.touches.size(), std::size_t(3));
    CHECK_EQ(h.touches[0].first.x, 10.0f);   // the element's origin in the window
    CHECK_EQ(h.touches[0].first.y, 20.0f);
    // A View that declines the down sees nothing more of the gesture.
    h.touches.clear();
    h.takeTouches = false;
    h.Touch(PointerEventType::Press, 30, 30, true);
    h.Touch(PointerEventType::Move, 35, 35, true);
    h.Touch(PointerEventType::Release, 35, 35, false);
    CHECK_EQ(h.touches.size(), std::size_t(1));
}

TEST(AndroidViewIsReleasedWhenItLeaves) {
    Harness h;
    rt::State<bool> shown(true);
    bool released = false;
    h.Run([&] {
        if (shown.Get()) {
            AndroidViewOptions o;
            o.onRelease = [&](void*, void*) { released = true; };
            AndroidView([](void*, void*) { return nullptr; }, Modifier{}, o);
        }
    });
    shown.Set(false);
    h.Pump();
    CHECK(released);
    CHECK(h.views[1].released);
}

TEST(AndroidViewWithOnResetIsKeptThroughLazyReuse) {
    Harness h;
    LazyListState state;
    h.Run([&] {
        LazyColumn(FillMax(Modifier{}, Axis::Both), state, [&](LazyListScope& s) {
            s.Items(100, [&](int) {
                AndroidViewOptions o;
                o.onReset = [](void*, void* v) { ++static_cast<FakeView*>(v)->resets; };
                AndroidView([](void*, void*) { return nullptr; }, Height(FillMax(Modifier{}, Axis::Horizontal), Dp(50)), o);
            });
        });
    });
    const int made = h.made;   // 200 px of 50 px items
    CHECK(made >= 4 && made <= 6);
    for (int i = 0; i < 10; ++i) { state.ConsumeScrollDelta(50); h.Pump(); }
    CHECK(h.made <= made + 2);   // items coming in took the Views of items that left
    int resets = 0;
    for (auto& [id, v] : h.views) resets += v.resets;
    CHECK(resets >= 8);
}
