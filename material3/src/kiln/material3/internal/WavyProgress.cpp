#include "kiln/material3/internal/WavyProgress.h"
#include "kiln/material3/internal/ProgressIndicator.h"
#include "kiln/material3/tokens/MotionTokens.h"

#include "kiln/foundation/Shapes.h"
#include "kiln/graphics/shapes/Shapes.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kiln::material3::internal {
    namespace {
        float CapWidth(ui::Size size, const gfx::Stroke& stroke, const gfx::Stroke& track) {
            return (stroke.cap == gfx::StrokeCap::Butt && track.cap == gfx::StrokeCap::Butt) || size.height > size.width ? 0 : std::max(stroke.width, track.width) / 2;
        }
        void Center(gfx::Path& path, ui::Size size) {
            const auto bounds = path.Bounds();
            path.Translate({ (size.width - bounds.left - bounds.right) / 2, (size.height - bounds.top - bounds.bottom) / 2 });
        }
    }
    void WavyProgressState::StartIndeterminate() {
        indeterminate = true; handoff = false; drawSecondLine = true;
        cycleMillis = 0; handoffFraction = 1; m_Exiting = false; m_Elapsed = 0;
    }
    void WavyProgressState::StartHandoff() {
        indeterminate = false; handoff = true; cycleMillis = 0; handoffFraction = 0; m_Elapsed = 0; m_Exiting = false;
    }
    void WavyProgressState::Step(float millis, float value) {
        if (!std::isfinite(value) && !indeterminate) StartIndeterminate();
        if (indeterminate) {
            if (!std::isfinite(value)) {
                m_Exiting = false;
                const float next = cycleMillis + millis;
                const float duration = circular ? 6000.0f : 1750.0f;
                if (next >= duration) drawSecondLine = true;
                cycleMillis = std::fmod(next, duration);
                return;
            }
            if (circular) {
                const auto current = CircularProgressMotionAt(std::int64_t(ui::RoundJava(cycleMillis)) * 1'000'000);
                handoffStartSweep = current.progress;
                const float rotation = std::fmod(current.globalRotation + current.additionalRotation + 90, 360.0f);
                handoffStartRotation = rotation == 0 ? 360 : rotation;
                m_HandoffDuration = float(ui::RoundJava(300 + 300 * (360 - handoffStartRotation) / 360));
                StartHandoff();
                return;
            }
            if (!m_Exiting) {
                drawSecondLine = drawSecondLine && cycleMillis > 650;
                m_ExitStart = cycleMillis;
                m_ExitDistance = (drawSecondLine ? 1750 : 1250) - cycleMillis;
                m_Elapsed = 0; m_Exiting = true;
            }
            m_Elapsed += millis;
            constexpr float acceleration = 1.5f / 150;
            const float distance = m_Elapsed <= 150 ? m_Elapsed + acceleration / 2 * m_Elapsed * m_Elapsed : 262.5f + 2.5f * (m_Elapsed - 150);
            if (distance < m_ExitDistance) { cycleMillis = m_ExitStart + distance; return; }
            m_HandoffDuration = 500;
            StartHandoff();
            return;
        }
        if (handoff) {
            m_Elapsed += millis;
            const float fraction = std::clamp(m_Elapsed / m_HandoffDuration, 0.0f, 1.0f);
            handoffFraction = circular ? tokens::MotionTokens::EasingEmphasizedDecelerateCubicBezier.ToEasing().Transform(fraction) : fraction;
            if (fraction == 1) { handoff = false; handoffFraction = 1; }
        }
    }
    float WavyProgressState::EndProgress(float value) const {
        const float progress = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0;
        if (indeterminate) return CircularProgressMotionAt(std::int64_t(ui::RoundJava(cycleMillis)) * 1'000'000).progress;
        if (circular && handoff) return handoffStartSweep + (progress - handoffStartSweep) * handoffFraction;
        return progress * handoffFraction;
    }
    float WavyProgressState::Rotation() const {
        if (indeterminate) {
            const auto motion = CircularProgressMotionAt(std::int64_t(ui::RoundJava(cycleMillis)) * 1'000'000);
            return motion.globalRotation + motion.additionalRotation + 90;
        }
        return handoff ? std::fmod(handoffStartRotation + (360 - handoffStartRotation) * handoffFraction, 360.0f) : 0;
    }

    void LinearWavyDrawingCache::Update(ui::Size size, float wavelength, std::span<const float> fractions, float amplitude, float offset, float gap, const gfx::Stroke& stroke, const gfx::Stroke& trackStroke) {
        if (amplitude != 0 && (!(wavelength > 0) || !std::isfinite(wavelength))) throw std::invalid_argument("Wavelength should be greater than zero for a wave");
        if (fractions.empty() || fractions.size() % 2) throw std::invalid_argument("Progress fractions must contain start/end pairs");
        const bool full = size != m_Size || wavelength != m_Wavelength || stroke != m_Stroke || trackStroke != m_TrackStroke || gap != m_Gap || (amplitude == 0) != (m_Amplitude == 0);
        if (full) {
            capWidth = CapWidth(size, stroke, trackStroke);
            m_Full.Reset();
            m_Full.MoveTo(0, 0);
            if (amplitude == 0) m_Full.LineTo(size.width, 0);
            else {
                const float half = wavelength / 2;
                float controlY = size.height - stroke.width;
                for (float anchor = half; anchor <= size.width + wavelength * 2; anchor += half) {
                    m_Full.QuadraticTo(anchor - half / 2, controlY, anchor, 0);
                    controlY = -controlY;
                }
            }
            m_Full.Translate({ 0, size.height / 2 });
            m_Measure.SetPath(&m_Full, false);
            m_Scale = m_Measure.Length() / (m_Full.Bounds().Width() + 0.00000001f);
            m_Size = size; m_Wavelength = wavelength; m_Stroke = stroke; m_TrackStroke = trackStroke; m_Gap = gap;
        }
        const bool resized = m_Fractions.size() != fractions.size();
        if (!full && !resized && std::equal(m_Fractions.begin(), m_Fractions.end(), fractions.begin()) && m_Amplitude == amplitude && m_Offset == offset) return;
        progress.resize(fractions.size() / 2);
        track.Reset();
        const float halfHeight = size.height / 2;
        float nextTrackEnd = size.width - capWidth, adjustedGap = gap;
        bool visible = false;
        track.MoveTo(nextTrackEnd, halfHeight);
        for (std::size_t i = 0; i < progress.size(); ++i) {
            progress[i].Reset();
            const float tail = fractions[i * 2] * size.width, head = fractions[i * 2 + 1] * size.width;
            if (i == 0) { adjustedGap = head < capWidth ? 0 : std::min(head - capWidth, gap); visible = head >= capWidth; }
            // The source uses the cap width as both insets, including when the two strokes differ.
            const float adjustedHead = std::max(capWidth, std::min(head, size.width - capWidth));
            const float adjustedTail = std::max(capWidth, std::min(tail, size.width - capWidth));
            if (std::abs(fractions[i * 2 + 1] - fractions[i * 2]) > 0) {
                const float shift = amplitude != 0 ? offset * wavelength : 0;
                m_Measure.GetSegment((adjustedTail + shift) * m_Scale, (adjustedHead + shift) * m_Scale, progress[i]);
                progress[i].Transform({ 1, 0, 0, amplitude, shift > 0 ? -shift : 0, (1 - amplitude) * halfHeight });
            }
            const float spacing = visible ? adjustedGap + capWidth * 2 : adjustedGap;
            if (nextTrackEnd > adjustedHead + spacing) track.LineTo(std::max(capWidth, adjustedHead + spacing), halfHeight);
            if (head > tail) {
                nextTrackEnd = std::max(capWidth, adjustedTail - spacing);
                track.MoveTo(nextTrackEnd, halfHeight);
            }
        }
        if (nextTrackEnd > capWidth) track.LineTo(capWidth, halfHeight);
        m_Fractions.assign(fractions.begin(), fractions.end()); m_Amplitude = amplitude; m_Offset = offset;
    }

    int CircularWavyDrawingCache::VertexCount(ui::Size size, float wavelength, float strokeWidth) const {
        if (!(wavelength > 0) || !std::isfinite(wavelength)) throw std::invalid_argument("Wavelength should be greater than zero");
        const float radius = size.MinDimension() / 2 - strokeWidth / 2;
        return std::max(5, ui::RoundJava(2 * std::numbers::pi_v<float> * radius / wavelength));
    }
    void CircularWavyDrawingCache::Update(ui::Size size, float wavelength, float endProgress, float amplitude, float offset, float gap, const gfx::Stroke& stroke, const gfx::Stroke& trackStroke, bool motion) {
        const int vertices = VertexCount(size, wavelength, stroke.width);
        const bool full = size != m_Size || wavelength != m_Wavelength || amplitude != m_Amplitude || stroke != m_Stroke || trackStroke != m_TrackStroke || gap != m_Gap || motion != m_Motion;
        if (vertices != m_Vertices) {
            m_TrackPolygon = gfx::shapes::Circle(vertices).Normalized();
            m_ActivePolygon = gfx::shapes::Star(vertices, 1, 0.75f, { 0.35f, 0.4f }, gfx::shapes::CornerRounding{ 0.5f, 0 }).Normalized();
            m_Morph.reset(); m_Vertices = vertices;
        }
        if (full) {
            if (amplitude > 0 && amplitude < 1 && !m_Morph) m_Morph = std::make_unique<gfx::shapes::Morph>(*m_TrackPolygon, *m_ActivePolygon);
            capWidth = CapWidth(size, stroke, trackStroke);
            m_FullProgress = m_Morph ? foundation::MorphToPath(*m_Morph, amplitude, 270, motion, true, 0.5f, 0.5f) : foundation::PolygonToPath(amplitude == 1 ? *m_ActivePolygon : *m_TrackPolygon, 270, motion);
            m_FullProgress.Transform(gfx::Matrix::Scale(size.width - stroke.width, size.height - stroke.width));
            Center(m_FullProgress, size);
            m_ProgressMeasure.SetPath(&m_FullProgress, true);
            m_ProgressLength = m_ProgressMeasure.Length() / (motion ? 2 : 1);
            m_FullTrack = foundation::PolygonToPath(*m_TrackPolygon);
            m_FullTrack.Transform(gfx::Matrix::Scale(size.width - stroke.width, size.height - stroke.width));
            Center(m_FullTrack, size);
            m_TrackMeasure.SetPath(&m_FullTrack, true);
            m_TrackLength = m_TrackMeasure.Length();
            m_Size = size; m_Wavelength = wavelength; m_Amplitude = amplitude; m_Stroke = stroke; m_TrackStroke = trackStroke; m_Gap = gap; m_Motion = motion;
        }
        if (!full && m_End == endProgress && m_Offset == offset) return;
        progress.Reset(); track.Reset();
        const float stop = endProgress * m_ProgressLength;
        const float spacing = std::min(stop, capWidth) * 2 + std::min(stop, gap);
        if (motion) {
            const float shift = std::clamp(offset, 0.0f, 1.0f) * m_ProgressLength;
            m_ProgressMeasure.GetSegment(shift, stop + shift, progress);
            const float angle = -std::fmod(std::clamp(offset, 0.0f, 1.0f) * 360, 360.0f) * std::numbers::pi_v<float> / 180;
            const auto bounds = m_FullProgress.Bounds();
            const float cx = (bounds.left + bounds.right) / 2, cy = (bounds.top + bounds.bottom) / 2;
            const float c = std::cos(angle), s = std::sin(angle);
            progress.Transform({ c, s, -s, c, cx - c * cx + s * cy, cy - s * cx - c * cy });
        } else m_ProgressMeasure.GetSegment(0, stop, progress);
        if (m_TrackLength > 0) m_TrackMeasure.GetSegment(endProgress * m_TrackLength + spacing, m_TrackLength - spacing, track);
        m_End = endProgress; m_Offset = offset;
    }
}
