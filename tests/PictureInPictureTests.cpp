// ui::PictureInPicture: the calls go to the host, and the mode is observable state.
#include "Check.h"

#include "kiln/runtime/Composition.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/PictureInPicture.h"

using namespace kiln;

TEST(PictureInPictureGoesThroughTheHostAndIsState) {
    std::vector<std::pair<ui::PictureInPictureParams, bool>> calls;
    int orientation = -1;
    // No host: nothing can enter.
    ui::SetPictureInPictureHost({});
    CHECK(!ui::EnterPictureInPictureMode());
    ui::SetPictureInPictureHost({
        .apply = [&](const ui::PictureInPictureParams& p, bool enter) { calls.emplace_back(p, enter); return p.aspectWidth <= 239; },
        .setOrientation = [&](int o) { orientation = o; },
        .orientation = [&] { return orientation; },
    });
    CHECK(ui::SetPictureInPictureParams({ .aspectWidth = 16, .aspectHeight = 9, .sourceRectHint = ui::IntRect{ 0, 0, 160, 90 }, .autoEnter = true }));
    CHECK(ui::EnterPictureInPictureMode({ .aspectWidth = 4, .aspectHeight = 3, .title = "Video" }));
    CHECK(!ui::EnterPictureInPictureMode({ .aspectWidth = 1000, .aspectHeight = 1 }));
    CHECK(calls.size() == 3 && !calls[0].second && calls[0].first.autoEnter && calls[0].first.sourceRectHint && calls[1].second && calls[1].first.title == "Video");
    ui::SetRequestedOrientation(ui::ScreenOrientation::SensorLandscape);
    CHECK(orientation == 6 && ui::RequestedOrientation() == ui::ScreenOrientation::SensorLandscape);

    ui::Owner owner;
    rt::Composition composition{ owner.Root(), rt::Dispatcher::Main() };
    std::vector<bool> seen;
    composition.SetContent([&] { seen.push_back(ui::IsInPictureInPictureMode()); });
    composition.Recompose();
    ui::SetInPictureInPictureMode(true);
    rt::Observation::ApplyChanges();
    rt::Composition::RecomposeAll();
    ui::SetInPictureInPictureMode(true);   // unchanged: no recomposition
    rt::Observation::ApplyChanges();
    rt::Composition::RecomposeAll();
    ui::SetInPictureInPictureMode(false);
    rt::Observation::ApplyChanges();
    rt::Composition::RecomposeAll();
    CHECK(seen == (std::vector<bool>{ false, true, false }));
    ui::SetPictureInPictureHost({});
}
