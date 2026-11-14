#include "kiln/ui/Vibration.h"

namespace kiln::ui {

    namespace {
        VibrationHost& Host() { static auto* h = new VibrationHost; return *h; }
    }
    bool Vibrate(const VibrationEffect& e) {
        // VibrationEffect's checks: a waveform's amplitudes match its timings.
        if (e.kind == VibrationEffect::Kind::Waveform && !e.amplitudes.empty() && e.amplitudes.size() != e.timings.size()) return false;
        if (e.kind == VibrationEffect::Kind::Waveform && (e.repeat < -1 || e.repeat >= int(e.timings.size()))) return false;
        return Host().vibrate && Host().vibrate(e);
    }
    void CancelVibration() { if (Host().cancel) Host().cancel(); }
    bool HasVibrator() { return Host().hasVibrator && Host().hasVibrator(); }
    bool HasAmplitudeControl() { return Host().hasAmplitudeControl && Host().hasAmplitudeControl(); }
    void SetVibrationHost(VibrationHost host) { Host() = std::move(host); }

}
