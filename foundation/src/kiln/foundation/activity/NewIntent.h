#pragma once

// The activity's intents in content: ActivityIntent() is the one that started it, OnNewIntent
// calls `onNewIntent` with each one it receives while this is composed (a DisposableEffect over
// ComponentActivity.addOnNewIntentListener; the latest lambda is called).

#include "kiln/ui/ActivityIntent.h"

namespace kiln::foundation {

    using ui::ActivityIntent;
    void OnNewIntent(kiln::Function<void(const ui::Intent&)> onNewIntent);

}
