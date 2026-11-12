#include "Check.h"

#include "kiln/foundation/AndroidExternalSurface.h"
#include "kiln/foundation/Layouts.h"

#include <map>

using namespace kiln;
using namespace kiln::foundation;

TEST(ExternalSurfaceFollowsItsElementAndLifecycle) {
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    rt::Composition comp(owner.Root(), rt::Dispatcher::Main());
    std::map<int, ui::ExternalSurfaceConfig> surfaces;
    std::vector<int> released;
    ui::ExternalSurfaceHost host;
    host.create = [&](int id, const ui::ExternalSurfaceConfig& c) { surfaces[id] = c; };
    host.update = [&](int id, const ui::ExternalSurfaceConfig& c) { surfaces[id] = c; };
    host.release = [&](int id) { released.push_back(id); surfaces.erase(id); };
    owner.SetExternalSurfaceHost(host);

    rt::State<bool> shown{ true };
    rt::State<float> top{ 10 };
    int inits = 0, started = 0, changed = 0, destroyed = 0;
    bool ended = false;
    comp.SetContent([&] {
        Column(Modifier{}, [&] {
            foundation::Box(SizeMod(Modifier{}, ui::Dp(10), ui::Dp(top.Get())));
            if (shown.Get())
                AndroidExternalSurface(SizeMod(Modifier{}, ui::Dp(100), ui::Dp(50)), false, {}, AndroidExternalSurfaceZOrder::MediaOverlay, true,
                                       [&](AndroidExternalSurfaceScope& s) {
                                           ++inits;
                                           s.OnSurface([&](SurfaceCoroutineScope& scope, Surface, int w, int h) -> rt::Task<> {
                                               ++started;
                                               CHECK(w == 100 && h == 50 && scope.IsActive());
                                               scope.OnChanged([&](Surface, int, int) { ++changed; });
                                               scope.OnDestroyed([&](Surface) { ++destroyed; });
                                               struct End { bool* e; ~End() { *e = true; } } end{ &ended };
                                               co_await rt::Delay(3600 * 1000);   // until cancelled
                                           });
                                       });
        });
    });
    auto pump = [&] {
        rt::Observation::ApplyChanges();
        rt::Dispatcher::Main().RunPending();
        comp.Recompose();
        owner.MeasureAndLayout({ 300, 300 });
        rt::Dispatcher::Main().RunPending();
    };
    pump();
    CHECK(surfaces.size() == 1 && inits == 1);
    const int id = surfaces.begin()->first;
    const auto& c = surfaces.begin()->second;
    CHECK(c.bounds == (ui::IntRect{ 0, 10, 100, 60 }) && c.visible && !c.opaque && c.zOrder == 1 && c.secure && !c.embedded);

    // The host makes the surface: onSurface runs; a same-size change is not reported.
    int fakeWindow = 0;
    owner.DispatchExternalSurfaceCreated(id, ui::PlatformSurface{ &fakeWindow }, 100, 50);
    rt::Dispatcher::Main().RunPending();
    CHECK_EQ(started, 1);
    owner.DispatchExternalSurfaceChanged(id, ui::PlatformSurface{ &fakeWindow }, 100, 50);
    owner.DispatchExternalSurfaceChanged(id, ui::PlatformSurface{ &fakeWindow }, 120, 50);
    CHECK_EQ(changed, 1);

    // Moving the element moves the surface.
    top.Set(20);
    pump();
    CHECK(surfaces[id].bounds == (ui::IntRect{ 0, 20, 100, 70 }));

    // Leaving: destroyed (the task cancelled) and released; onInit ran once.
    shown.Set(false);
    pump();
    CHECK(destroyed == 1 && ended && released == std::vector<int>{ id } && surfaces.empty() && inits == 1);
}
