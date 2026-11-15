// ComponentActivity's listeners as kiln exposes them: trim memory, multi-window mode and the
// picture-in-picture UI state, the last two as observable state.
#include "Check.h"

#include "kiln/runtime/State.h"
#include "kiln/ui/ActivityCallbacks.h"
#include "kiln/ui/PictureInPicture.h"

#include <vector>

using namespace kiln;

TEST(TrimMemoryListenersHearEachLevel) {
    std::vector<int> a, b;
    const int ida = ui::AddOnTrimMemoryListener([&](int level) { a.push_back(level); });
    int idb = 0;
    idb = ui::AddOnTrimMemoryListener([&](int level) {
        b.push_back(level);
        ui::RemoveOnTrimMemoryListener(idb);   // removing itself while called
    });
    ui::DispatchTrimMemory(ui::kTrimMemoryUiHidden);
    ui::DispatchTrimMemory(ui::kTrimMemoryBackground);
    CHECK((a == std::vector<int>{ 20, 40 }) && (b == std::vector<int>{ 20 }));
    ui::RemoveOnTrimMemoryListener(ida);
    ui::DispatchTrimMemory(ui::kTrimMemoryComplete);
    CHECK(a.size() == 2);
}

TEST(MultiWindowAndPipUiStateAreObservable) {
    struct Reader : rt::Subscriber {
        int changes = 0;
        void OnStateChanged() override { ++changes; }
    } reader;
    bool multi = false;
    ui::PictureInPictureUiState pip;
    rt::Observation::Track(reader, [&] { multi = ui::IsInMultiWindowMode(); pip = ui::GetPictureInPictureUiState(); });
    CHECK(!multi && !pip.stashed);
    ui::SetInMultiWindowMode(true);
    ui::SetPictureInPictureUiState({ .stashed = true });
    rt::Observation::ApplyChanges();
    CHECK(reader.changes == 1 && ui::IsInMultiWindowMode() && ui::GetPictureInPictureUiState().stashed);
    ui::SetInMultiWindowMode(false);
    ui::SetPictureInPictureUiState({});
}
