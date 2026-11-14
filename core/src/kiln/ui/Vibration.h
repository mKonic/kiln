#pragma once

// Vibration patterns (android.os.VibrationEffect through the default Vibrator). HapticFeedback
// covers the UI's own feedback; this is for patterns an app designs. Needs
// android.permission.VIBRATE.
//
//   ui::Vibrate(ui::VibrationEffect::Waveform({ 0, 40, 60, 40 }, { 0, 255, 0, 128 }));

#include <kiln/Function.h>

#include <cstdint>
#include <vector>

namespace kiln::ui {

    struct VibrationEffect {
        enum class Kind : std::uint8_t { OneShot, Waveform, Predefined, Composition };
        // VibrationEffect.EFFECT_* and Composition.PRIMITIVE_*.
        enum Predefined : int { EffectClick = 0, EffectDoubleClick = 1, EffectTick = 2, EffectHeavyClick = 5 };
        enum Primitive : int { PrimitiveClick = 1, PrimitiveThud = 2, PrimitiveSpin = 3, PrimitiveQuickRise = 4, PrimitiveSlowRise = 5,
                               PrimitiveQuickFall = 6, PrimitiveTick = 7, PrimitiveLowTick = 8 };
        static constexpr int kDefaultAmplitude = -1;
        struct PrimitiveStep { int primitive = PrimitiveClick; float scale = 1; int delayMs = 0; bool operator==(const PrimitiveStep&) const = default; };

        Kind kind = Kind::OneShot;
        std::vector<std::int64_t> timings;   // OneShot: { ms }; Waveform: off / on durations, starting off
        std::vector<int> amplitudes;         // 0-255, or kDefaultAmplitude
        int repeat = -1;                     // Waveform: the index to repeat from, -1 once
        int effect = EffectClick;            // Predefined
        std::vector<PrimitiveStep> primitives;   // Composition (API 30; the device may lack some)
        bool operator==(const VibrationEffect&) const = default;

        static VibrationEffect OneShot(std::int64_t ms, int amplitude = kDefaultAmplitude) { return { Kind::OneShot, { ms }, { amplitude } }; }
        static VibrationEffect Waveform(std::vector<std::int64_t> timings, std::vector<int> amplitudes = {}, int repeat = -1) {
            VibrationEffect e;
            e.kind = Kind::Waveform;
            e.timings = std::move(timings);
            e.amplitudes = std::move(amplitudes);
            e.repeat = repeat;
            return e;
        }
        static VibrationEffect CreatePredefined(int effect) { VibrationEffect e; e.kind = Kind::Predefined; e.effect = effect; return e; }
        static VibrationEffect Compose(std::vector<PrimitiveStep> steps) { VibrationEffect e; e.kind = Kind::Composition; e.primitives = std::move(steps); return e; }
    };

    // False when there is no vibrator, the effect is not supported, or off Android.
    bool Vibrate(const VibrationEffect& effect);
    void CancelVibration();
    bool HasVibrator();
    bool HasAmplitudeControl();

    struct VibrationHost {
        kiln::Function<bool(const VibrationEffect&)> vibrate;
        kiln::Function<void()> cancel;
        kiln::Function<bool()> hasVibrator, hasAmplitudeControl;
    };
    void SetVibrationHost(VibrationHost host);

}
