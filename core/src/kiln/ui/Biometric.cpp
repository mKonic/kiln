#include "kiln/ui/Biometric.h"

#include "kiln/runtime/Channel.h"

#include <memory>

namespace kiln::ui {

    namespace {
        BiometricHost& Host() { static auto* h = new BiometricHost; return *h; }
    }

    int CanAuthenticate(int authenticators) { return Host().canAuthenticate ? Host().canAuthenticate(authenticators) : -2; }

    rt::Task<BiometricResult> Authenticate(BiometricPromptInfo info) {
        if (!Host().authenticate) co_return BiometricResult{ false, -2, "unsupported" };
        auto result = std::make_shared<rt::Channel<BiometricResult>>(1);
        struct Prompt {
            int id = -1;
            ~Prompt() { if (id >= 0 && Host().cancel) Host().cancel(id); }
        } prompt;
        prompt.id = Host().authenticate(info, [result](BiometricResult r) { result->TrySend(std::move(r)); });
        if (prompt.id < 0) co_return BiometricResult{ false, 1, "unavailable" };
        auto got = co_await result->Next();
        prompt.id = -1;
        co_return got ? *got : BiometricResult{ false, 5, "cancelled" };
    }

    void SetBiometricHost(BiometricHost host) { Host() = std::move(host); }

}
