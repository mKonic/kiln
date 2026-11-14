#pragma once

// The biometric prompt (android.hardware.biometrics.BiometricPrompt, BiometricManager): the
// system's own dialog asks for a fingerprint, the face or the device's PIN. Needs
// android.permission.USE_BIOMETRIC.
//
//   auto r = co_await ui::Authenticate({ .title = "Unlock notes", .negativeButtonText = "Cancel" });
//   if (r.succeeded) ...

#include "kiln/runtime/Task.h"
#include <kiln/Function.h>

#include <string>

namespace kiln::ui {

    // BiometricManager.Authenticators.
    inline constexpr int kBiometricStrong = 0x000F, kBiometricWeak = 0x00FF, kDeviceCredential = 0x8000;

    struct BiometricPromptInfo {
        std::string title, subtitle, description;
        // The dismiss button; not shown (and not allowed) when kDeviceCredential is allowed,
        // where the system offers the PIN instead.
        std::string negativeButtonText = "Cancel";
        int allowedAuthenticators = kBiometricWeak;
        bool confirmationRequired = true;   // a face match waits for a tap
    };

    struct BiometricResult {
        bool succeeded = false;
        // BiometricPrompt.BIOMETRIC_ERROR_* (10: the user cancelled, 13: the negative button),
        // when it did not succeed.
        int errorCode = 0;
        std::string errorMessage;
        // BiometricPrompt.AuthenticationResult.getAuthenticationType: 1 the PIN, 2 a biometric.
        int authenticationType = 0;
    };

    // BiometricManager.canAuthenticate: 0 BIOMETRIC_SUCCESS, 11 none enrolled, 12 no hardware,
    // 1 hardware unavailable, 15 a security update is needed, -2 unsupported (off Android).
    int CanAuthenticate(int authenticators = kBiometricWeak);
    // Shows the prompt; the result arrives when it closes. Cancelling the task closes it.
    rt::Task<BiometricResult> Authenticate(BiometricPromptInfo info);

    struct BiometricHost {
        kiln::Function<int(int authenticators)> canAuthenticate;
        // Calls back once; returns an id for cancel.
        kiln::Function<int(const BiometricPromptInfo&, kiln::Function<void(BiometricResult)>)> authenticate;
        kiln::Function<void(int id)> cancel;
    };
    void SetBiometricHost(BiometricHost host);

}
