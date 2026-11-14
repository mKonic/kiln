#include "kiln/foundation/activity/NewIntent.h"

#include "kiln/runtime/Composition.h"

#include <memory>

namespace kiln::foundation {

    void OnNewIntent(kiln::Function<void(const ui::Intent&)> onNewIntent) {
        auto* c = rt::Composition::Current();
        // rememberUpdatedState: the listener added once calls the latest lambda.
        auto& current = c->Remember<std::shared_ptr<kiln::Function<void(const ui::Intent&)>>>(
            [] { return std::make_shared<kiln::Function<void(const ui::Intent&)>>(); });
        *current = std::move(onNewIntent);
        c->DisposableEffect(rt::Keys(), [cur = current]() -> kiln::Function<void()> {
            const int id = ui::AddOnNewIntentListener([cur](const ui::Intent& i) { if (*cur) (*cur)(i); });
            return [id] { ui::RemoveOnNewIntentListener(id); };
        });
    }

}
