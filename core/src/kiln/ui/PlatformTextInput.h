#pragma once

// Platform text input for nodes of one's own (ui PlatformTextInputModifierNode.kt,
// PlatformTextInputInterceptor.kt): a node establishes a session with the window's input
// method, and while it runs, starts the input method with a request (kiln's TextInputSession,
// what the host turns into EditorInfo and an InputConnection). Establishing a session cancels
// the window's previous one; leaving it (cancelling the task) stops the input method.
// InterceptPlatformTextInput lets content above see and change the requests below it.

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/TextInput.h"

#include <functional>
#include <memory>
#include <vector>

namespace kiln::ui {

    class ModifierNode;

    // PlatformTextInputMethodRequest: what the input method is started with.
    using PlatformTextInputMethodRequest = TextInputSession;

    // PlatformTextInputModifierNode: marks a node that may establish text input sessions.
    class PlatformTextInputModifierNode {
    public:
        virtual ~PlatformTextInputModifierNode() = default;
    };

    // PlatformTextInputSession / PlatformTextInputSessionScope: starts the input method. The task
    // never ends by itself: the input method runs until the session's task is cancelled.
    class PlatformTextInputSession {
    public:
        virtual ~PlatformTextInputSession() = default;
        virtual rt::Task<> StartInputMethod(PlatformTextInputMethodRequest& request) = 0;
    };
    using PlatformTextInputSessionScope = PlatformTextInputSession;

    // PlatformTextInputInterceptor: sees the requests of sessions below it; it passes them (or
    // a request of its own) on to `nextHandler`, or does not start the input method at all.
    class PlatformTextInputInterceptor {
    public:
        virtual ~PlatformTextInputInterceptor() = default;
        virtual rt::Task<> InterceptStartInputMethod(PlatformTextInputMethodRequest& request, PlatformTextInputSession& nextHandler) = 0;
    };

    // The interceptors around a place, outermost first.
    inline rt::Local<std::vector<std::shared_ptr<PlatformTextInputInterceptor>>> LocalPlatformTextInputInterceptors{ {} };

    // InterceptPlatformTextInput(interceptor, content).
    void InterceptPlatformTextInput(std::shared_ptr<PlatformTextInputInterceptor> interceptor, std::function<void()> content, rt::Key key = {});

    // establishTextInputSession: runs `block` with the window's input method for `node` (which
    // must be attached). Returns when `block` returns; cancelled when another session starts.
    rt::Task<> EstablishTextInputSession(ModifierNode& node, std::function<rt::Task<>(PlatformTextInputSessionScope&)> block);

}
