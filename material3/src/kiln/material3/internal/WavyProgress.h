#pragma once

#include "kiln/graphics/PathMeasure.h"
#include "kiln/graphics/shapes/Morph.h"

#include <optional>
#include <span>

namespace kiln::material3::internal {
    struct WavyProgressState {
        bool circular = false, indeterminate = false, handoff = false, drawSecondLine = true;
        float cycleMillis = 0, handoffFraction = 1;
        float handoffStartSweep = 0, handoffStartRotation = 0;
        void StartIndeterminate();
        void Step(float millis, float progress);
        float EndProgress(float progress) const;
        float Rotation() const;
    private:
        bool m_Exiting = false;
        float m_Elapsed = 0, m_ExitStart = 0, m_ExitDistance = 0, m_HandoffDuration = 500;
        void StartHandoff();
    };
    // Geometry caches from LinearWavyProgressModifiers.kt / CircularWavyProgressModifiers.kt.
    // PathMeasure cuts curves, rather than sampling progress into line segments.
    class LinearWavyDrawingCache {
    public:
        void Update(ui::Size size, float wavelength, std::span<const float> fractions, float amplitude,
                    float waveOffset, float gap, const gfx::Stroke& stroke, const gfx::Stroke& trackStroke);
        gfx::Path track;
        std::vector<gfx::Path> progress;
        float capWidth = 0;
    private:
        ui::Size m_Size{-1, -1};
        float m_Wavelength = -1, m_Amplitude = -1, m_Offset = -1, m_Gap = -1, m_Scale = 1;
        gfx::Stroke m_Stroke, m_TrackStroke;
        std::vector<float> m_Fractions;
        gfx::Path m_Full;
        gfx::PathMeasure m_Measure;
    };
    class CircularWavyDrawingCache {
    public:
        void Update(ui::Size size, float wavelength, float endProgress, float amplitude, float waveOffset,
                    float gap, const gfx::Stroke& stroke, const gfx::Stroke& trackStroke, bool motion);
        int VertexCount(ui::Size size, float wavelength, float strokeWidth) const;
        gfx::Path track, progress;
        float capWidth = 0;
    private:
        ui::Size m_Size{-1, -1};
        float m_Wavelength = -1, m_Amplitude = -1, m_Offset = -1, m_Gap = -1, m_End = -1;
        float m_ProgressLength = 0, m_TrackLength = 0;
        gfx::Stroke m_Stroke, m_TrackStroke;
        bool m_Motion = false;
        int m_Vertices = -1;
        std::optional<gfx::shapes::RoundedPolygon> m_TrackPolygon, m_ActivePolygon;
        std::unique_ptr<gfx::shapes::Morph> m_Morph;
        gfx::Path m_FullProgress, m_FullTrack;
        gfx::PathMeasure m_ProgressMeasure, m_TrackMeasure;
    };
}
