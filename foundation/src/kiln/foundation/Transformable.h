#pragma once

// transformable (Transformable.kt, TransformableState.kt): multi-touch zoom, pan and rotation
// past the touch slop, plus the non-touch forms: Ctrl + mouse wheel or Ctrl + trackpad swipe
// zooms (2x per 545 px of scroll), a trackpad swipe pans, a trackpad pinch zooms. Changes go to
// a TransformableState's onTransformation with the centroid they happen around.
//
// One transformation runs at a time (MutatorMutex): a new one at the same or higher priority
// stops the running one; one at lower priority does not start. Gestures run at UserInput.

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/MutatorMutex.h"

#include <cmath>
#include <cstdint>
#include <functional>

namespace kiln::foundation {

    // Offset::Unspecified for a centroid: no particular point.
    inline constexpr Offset kUnspecifiedCentroid{ NAN, NAN };

    class TransformableState {
    public:
        using OnTransformation = std::function<void(Offset centroid, float zoomChange, Offset panChange, float rotationChange)>;
        explicit TransformableState(OnTransformation onTransformation) : m_On(std::move(onTransformation)) {}

        bool IsTransformInProgress() const { return m_InProgress.Get(); }
        // TransformScope: only meaningful inside a transformation (a session).
        void TransformBy(float zoomChange = 1, Offset panChange = {}, float rotationChange = 0) {
            TransformByWithCentroid(kUnspecifiedCentroid, zoomChange, panChange, rotationChange);
        }
        void TransformByWithCentroid(Offset centroid, float zoomChange = 1, Offset panChange = {}, float rotationChange = 0) {
            if (m_On) m_On(centroid, zoomChange, panChange, rotationChange);
        }

        // transform(priority) { }: 0 when a higher-priority transformation holds the state;
        // otherwise it stops the running one and returns a token for EndTransform.
        std::uint64_t BeginTransform(MutatePriority priority, std::function<void()> stop);
        bool IsCurrent(std::uint64_t token) const { return token != 0 && token == m_Token; }
        void EndTransform(std::uint64_t token);
        // stopTransformation(priority): takes the state over and lets go at once.
        void StopTransformation(MutatePriority priority = MutatePriority::Default) { EndTransform(BeginTransform(priority, {})); }

        void SetOnTransformation(OnTransformation f) { m_On = std::move(f); }

    private:
        OnTransformation m_On;
        rt::State<bool> m_InProgress{ false };
        std::function<void()> m_Stop;
        MutatePriority m_Priority = MutatePriority::Default;
        std::uint64_t m_Token = 0;
        bool m_Running = false;
    };

    // Holds a transformation for its scope (the coroutine frame or gesture that owns it).
    struct TransformSession {
        TransformableState& state;
        std::uint64_t token;
        TransformSession(TransformableState& s, MutatePriority priority, std::function<void()> stop)
            : state(s), token(s.BeginTransform(priority, std::move(stop))) {}
        ~TransformSession() { state.EndTransform(token); }
        bool Current() const { return state.IsCurrent(token); }
        TransformSession(const TransformSession&) = delete;
        TransformSession& operator=(const TransformSession&) = delete;
    };

    // rememberTransformableState(onTransformation): the latest onTransformation is used.
    TransformableState& RememberTransformableState(TransformableState::OnTransformation onTransformation);

    // Modifier.transformable. canPan decides whether a pan delta may start or continue the
    // gesture (a parent can scroll otherwise); lockRotationOnZoomPan keeps rotation out once a
    // zoom or pan started first.
    Modifier Transformable(Modifier m, TransformableState& state, std::function<bool(Offset)> canPan = {},
                           bool lockRotationOnZoomPan = false, bool enabled = true);

    // Programmatic transformations (priority Default).
    rt::Task<> AnimateZoomBy(TransformableState& state, float zoomFactor,
                             anim::SpecPtr spec = anim::Spring(anim::spring::DampingRatioNoBouncy, anim::spring::StiffnessLow),
                             Offset centroid = kUnspecifiedCentroid);
    rt::Task<> AnimateRotateBy(TransformableState& state, float degrees,
                               anim::SpecPtr spec = anim::Spring(anim::spring::DampingRatioNoBouncy, anim::spring::StiffnessLow),
                               Offset centroid = kUnspecifiedCentroid);
    rt::Task<> AnimatePanBy(TransformableState& state, Offset offset,
                            anim::SpecPtr spec = anim::Spring(anim::spring::DampingRatioNoBouncy, anim::spring::StiffnessLow),
                            Offset centroid = kUnspecifiedCentroid);
    // All three at once, each with its own spec; ends when the longest does.
    rt::Task<> AnimateBy(TransformableState& state, float zoomFactor, Offset panOffset, float rotationDegrees,
                         anim::SpecPtr zoomSpec = anim::Spring(anim::spring::DampingRatioNoBouncy, anim::spring::StiffnessLow),
                         anim::SpecPtr panSpec = anim::Spring(anim::spring::DampingRatioNoBouncy, anim::spring::StiffnessLow),
                         anim::SpecPtr rotationSpec = anim::Spring(anim::spring::DampingRatioNoBouncy, anim::spring::StiffnessLow),
                         Offset centroid = kUnspecifiedCentroid);
    void ZoomBy(TransformableState& state, float zoomFactor, Offset centroid = kUnspecifiedCentroid);
    void RotateBy(TransformableState& state, float degrees, Offset centroid = kUnspecifiedCentroid);
    void PanBy(TransformableState& state, Offset offset, Offset centroid = kUnspecifiedCentroid);

}
