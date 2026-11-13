#pragma once

// BackHandler / PredictiveBackHandler (activity-compose). The handler keeps the position of its
// first composition; the last composed enabled handler wins.

#include "kiln/runtime/Channel.h"
#include <kiln/Function.h>
#include "kiln/runtime/Composition.h"
#include "kiln/ui/Back.h"

namespace kiln::foundation {

    using ui::BackEvent;
    using BackProgress = rt::Channel<BackEvent>;

    // The dispatcher handlers register with (a dialog window provides its own).
    inline rt::Local<ui::BackDispatcher*> LocalBackDispatcher{ &ui::BackDispatcher::Main() };

    // onBack runs on completion only (gesture released, back key/button).
    void BackHandler(bool enabled, kiln::Function<void()> onBack);

    // onBack runs once per gesture with the progress events; the channel closes when the gesture
    // completes (also for key/button back, with no events), and the task is cancelled when the
    // gesture is cancelled. It must drain the channel: `while (auto e = co_await p.Next()) {...}`
    // and then commit.
    void PredictiveBackHandler(bool enabled, kiln::Function<rt::Task<>(BackProgress&)> onBack);

}
