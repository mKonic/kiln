#include "kiln/foundation/Transformable.h"
#include <kiln/Function.h>

#include <algorithm>
#include <optional>

namespace kiln::foundation {

    // --- TransformableState ------------------------------------------------------------------

    std::uint64_t TransformableState::BeginTransform(MutatePriority priority, kiln::Function<void()> stop) {
        // MutatorMutex.mutate: only the same or a higher priority interrupts.
        if (m_Running && priority < m_Priority) return 0;
        if (m_Running) {
            auto previous = std::move(m_Stop);
            m_Stop = nullptr;
            if (previous) previous();
        }
        m_Running = true;
        m_Priority = priority;
        m_Stop = std::move(stop);
        if (!m_InProgress.Peek()) m_InProgress.Set(true);
        return ++m_Token;
    }

    void TransformableState::EndTransform(std::uint64_t token) {
        if (!IsCurrent(token) || !m_Running) return;
        m_Running = false;
        m_Stop = nullptr;
        if (m_InProgress.Peek()) m_InProgress.Set(false);
    }

    TransformableState& RememberTransformableState(TransformableState::OnTransformation onTransformation, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto& s = c.RememberNew<TransformableState>({}, key, onTransformation);
        s.SetOnTransformation(std::move(onTransformation));
        return s;
    }

    // --- gestures ----------------------------------------------------------------------------

    namespace {
        // Every 545 px of scroll is 2x zoom (curve-fitted to ChromeOS's Ctrl + scroll zoom).
        constexpr float kScrollFactor = 545;

        struct TransformBox {
            TransformableState* state = nullptr;
            kiln::Function<bool(Offset)> canPan;
            bool CanPan(Offset d) const { return !canPan || canPan(d); }
        };

        // The gesture's hold on the state, from TransformStarted to TransformStopped. A
        // transformation that takes over meanwhile drops the rest of this gesture's deltas.
        struct Sink {
            std::shared_ptr<TransformBox> box;
            std::optional<TransformSession> session;
            std::shared_ptr<bool> stopped = std::make_shared<bool>(false);
            explicit Sink(std::shared_ptr<TransformBox> b) : box(std::move(b)) {}
            void Start() {
                Stop();
                *stopped = false;
                session.emplace(*box->state, MutatePriority::UserInput, [s = stopped] { *s = true; });
            }
            void Delta(Offset centroid, float zoom, Offset pan, float rotation) {
                if (session && session->token && !*stopped) box->state->TransformByWithCentroid(centroid, zoom, pan, rotation);
            }
            void Stop() { session.reset(); }
        };

        bool IsPanType(PointerEventType t) {
            return t == PointerEventType::PanStart || t == PointerEventType::PanMove || t == PointerEventType::PanEnd;
        }
        bool IsScaleType(PointerEventType t) {
            return t == PointerEventType::ScaleStart || t == PointerEventType::ScaleChange || t == PointerEventType::ScaleEnd;
        }

        // calculateCentroid { true }: every change, pressed or not, at its current position.
        Offset CentroidOfAll(const PointerEvent& e) {
            if (e.changes.empty()) return kUnspecifiedCentroid;
            Offset sum{};
            for (const auto& c : e.changes) sum = sum + c.position;
            return { sum.x / float(e.changes.size()), sum.y / float(e.changes.size()) };
        }

        Offset PanOf(const PointerEvent& e) {
            if (e.changes.empty()) return {};
            const auto& c = e.changes[0];
            Offset d{ -c.panOffset.x, -c.panOffset.y };
            for (const auto& h : c.historical) d = d - h.panOffset;
            return d;
        }

        std::optional<Offset> CtrlScrollOrNull(PointerEvent& e, const ViewConfiguration& cfg) {
            if (!e.keyboardModifiers.IsCtrlPressed() || (e.type != PointerEventType::Scroll && !IsPanType(e.type))) return std::nullopt;
            // calculateMouseWheelScroll: the wheel delta times the view's scroll factors, negated.
            Offset wheel{};
            for (const auto& c : e.changes) wheel = wheel + c.scrollDelta;
            const Offset d = Offset{ -wheel.x * cfg.horizontalScrollFactor, -wheel.y * cfg.verticalScrollFactor } + PanOf(e);
            if (d.x == 0 && d.y == 0) return std::nullopt;
            for (auto& c : e.changes) c.Consume();
            return d;
        }

        std::optional<Offset> PanOrNull(PointerEvent& e) {
            if (!IsPanType(e.type)) return std::nullopt;
            const Offset d = PanOf(e);
            if (d.x == 0 && d.y == 0) return std::nullopt;
            for (auto& c : e.changes) c.Consume();
            return d;
        }

        std::optional<float> ScaleOrNull(PointerEvent& e) {
            if (!IsScaleType(e.type)) return std::nullopt;
            float scale = 1;
            for (const auto& c : e.changes) {
                scale *= c.scaleFactor;
                for (const auto& h : c.historical) scale *= h.scaleFactor;
            }
            if (scale == 1) return std::nullopt;
            for (auto& c : e.changes) c.Consume();
            return scale;
        }

        bool AnyPressed(const PointerEvent& e) {
            return std::any_of(e.changes.begin(), e.changes.end(), [](const auto& c) { return c.pressed; });
        }

        // detectZoom inside awaitEachGesture: touch zoom, rotation and pan past the slop.
        rt::Task<> DetectZoom(PointerInputScope& s, bool panZoomLock, std::shared_ptr<TransformBox> box) {
            for (;;) {
                Sink sink(box);
                // awaitFirstDown(requireUnconsumed = false)
                for (;;) {
                    auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    if (std::any_of(e->changes.begin(), e->changes.end(), [](const auto& c) { return c.ChangedToDownIgnoreConsumed(); })) break;
                }
                float rotation = 0, zoom = 1;
                Offset pan{};
                bool pastSlop = false, locked = false, pressed = true;
                const float slop = s.Config().touchSlop;
                for (;;) {
                    auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    const bool canceled = std::any_of(e->changes.begin(), e->changes.end(), [](const auto& c) { return c.IsConsumed(); }) ||
                                          IsPanType(e->type) || IsScaleType(e->type);
                    if (!canceled) {
                        const float zoomChange = CalculateZoom(*e), rotationChange = CalculateRotation(*e);
                        const Offset panChange = CalculatePan(*e);
                        if (!pastSlop) {
                            zoom *= zoomChange;
                            rotation += rotationChange;
                            pan = pan + panChange;
                            const float size = CalculateCentroidSize(*e, false);
                            const float zoomMotion = std::abs(1 - zoom) * size;
                            const float rotationMotion = std::abs(rotation * 3.14159265358979f * size / 180.0f);
                            const float panMotion = std::hypot(pan.x, pan.y);
                            if (zoomMotion > slop || rotationMotion > slop || (panMotion > slop && box->CanPan(panChange))) {
                                pastSlop = true;
                                locked = panZoomLock && rotationMotion < slop;
                                sink.Start();
                            }
                        }
                        if (pastSlop) {
                            const Offset centroid = CalculateCentroid(*e, false);
                            const float effectiveRotation = locked ? 0 : rotationChange;
                            if (effectiveRotation != 0 || zoomChange != 1 || (!(panChange == Offset{}) && box->CanPan(panChange)))
                                sink.Delta(centroid, zoomChange, panChange, effectiveRotation);
                            for (auto& c : e->changes) if (!(c.position == c.previousPosition)) c.Consume();
                        }
                    } else {
                        sink.Stop();
                    }
                    pressed = AnyPressed(*e);
                    auto finalEvent = co_await s.AwaitPointerEvent(PointerEventPass::Final);
                    // Someone consumed while we waited for the slop.
                    const bool finallyCanceled = !pastSlop &&
                        std::any_of(finalEvent->changes.begin(), finalEvent->changes.end(), [](const auto& c) { return c.IsConsumed(); });
                    if (canceled || finallyCanceled || !pressed) break;
                }
                sink.Stop();
                // awaitEachGesture: the next gesture starts once every pointer is up.
                while (pressed) {
                    auto e = co_await s.AwaitPointerEvent(PointerEventPass::Final);
                    pressed = AnyPressed(*e);
                }
            }
        }

        // detectNonTouchGestures: Ctrl + scroll zooms, a trackpad swipe pans, a pinch zooms.
        rt::Task<> DetectNonTouch(PointerInputScope& s, std::shared_ptr<TransformBox> box) {
            for (;;) {
                std::optional<Offset> zoomOffset, panOffset;
                std::optional<float> scale;
                PointerEvent pointer;
                do {
                    auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    pointer = *e;
                    zoomOffset = CtrlScrollOrNull(pointer, s.Config());
                    panOffset = PanOrNull(pointer);
                    scale = ScaleOrNull(pointer);
                } while (!zoomOffset && !panOffset && !scale);
                Sink sink(box);
                sink.Start();
                if (zoomOffset) {
                    Offset delta = *zoomOffset;
                    for (;;) {
                        sink.Delta(CentroidOfAll(pointer), std::pow(2.0f, delta.y / kScrollFactor), {}, 0);
                        auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                        pointer = *e;
                        auto next = CtrlScrollOrNull(pointer, s.Config());
                        if (!next) break;
                        delta = *next;
                    }
                } else if (panOffset) {
                    Offset delta = *panOffset;
                    for (;;) {
                        sink.Delta(CentroidOfAll(pointer), 1, delta, 0);
                        auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                        pointer = *e;
                        auto next = PanOrNull(pointer);
                        if (!next) break;
                        delta = *next;
                    }
                } else {
                    float delta = *scale;
                    for (;;) {
                        sink.Delta(CentroidOfAll(pointer), delta, {}, 0);
                        auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                        pointer = *e;
                        auto next = ScaleOrNull(pointer);
                        if (!next) break;
                        delta = *next;
                    }
                }
            }
        }
    }

    Modifier Transformable(Modifier m, TransformableState& state, kiln::Function<bool(Offset)> canPan,
                           bool lockRotationOnZoomPan, bool enabled) {
        auto box = std::make_shared<TransformBox>();
        if (auto* c = rt::Composition::Current()) box = c->Remember<std::shared_ptr<TransformBox>>([&] { return box; });
        box->state = &state;
        box->canPan = std::move(canPan);   // read when used, so the latest one counts
        if (!enabled) return m;
        const auto stateKey = reinterpret_cast<std::uintptr_t>(&state);
        m = PointerInput(std::move(m), rt::Keys(stateKey, lockRotationOnZoomPan, 0),
                         [box, lockRotationOnZoomPan](PointerInputScope& s) { return DetectZoom(s, lockRotationOnZoomPan, box); });
        return PointerInput(std::move(m), rt::Keys(stateKey, 1),
                            [box](PointerInputScope& s) { return DetectNonTouch(s, box); });
    }

    // --- programmatic ------------------------------------------------------------------------

    namespace {
        // DelegatingAnimationSpec: zoom (component 0), pan (1-2) and rotation (3), each with its
        // own spec.
        class DelegatingSpec final : public anim::AnimationSpec {
        public:
            DelegatingSpec(anim::SpecPtr zoom, anim::SpecPtr pan, anim::SpecPtr rotation)
                : m_Zoom(std::move(zoom)), m_Pan(std::move(pan)), m_Rotation(std::move(rotation)) {}
            anim::Vec ValueAt(std::int64_t t, const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, int) const override {
                return Each(t, from, to, v0, false);
            }
            anim::Vec VelocityAt(std::int64_t t, const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, int) const override {
                return Each(t, from, to, v0, true);
            }
            std::int64_t DurationNanos(const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, int) const override {
                return std::max({ m_Zoom->DurationNanos(One(from, 0), One(to, 0), One(v0, 0), 1),
                                  m_Pan->DurationNanos(Two(from), Two(to), Two(v0), 2),
                                  m_Rotation->DurationNanos(One(from, 3), One(to, 3), One(v0, 3), 1) });
            }

        private:
            static anim::Vec One(const anim::Vec& v, int i) { return { v[std::size_t(i)], 0, 0, 0 }; }
            static anim::Vec Two(const anim::Vec& v) { return { v[1], v[2], 0, 0 }; }
            anim::Vec Each(std::int64_t t, const anim::Vec& from, const anim::Vec& to, const anim::Vec& v0, bool velocity) const {
                auto at = [&](const anim::SpecPtr& s, const anim::Vec& f, const anim::Vec& g, const anim::Vec& v, int dims) {
                    return velocity ? s->VelocityAt(t, f, g, v, dims) : s->ValueAt(t, f, g, v, dims);
                };
                const anim::Vec z = at(m_Zoom, One(from, 0), One(to, 0), One(v0, 0), 1);
                const anim::Vec p = at(m_Pan, Two(from), Two(to), Two(v0), 2);
                const anim::Vec r = at(m_Rotation, One(from, 3), One(to, 3), One(v0, 3), 1);
                return { z[0], p[0], p[1], r[0] };
            }
            anim::SpecPtr m_Zoom, m_Pan, m_Rotation;
        };
    }

    rt::Task<> AnimateBy(TransformableState& state, float zoomFactor, Offset panOffset, float rotationDegrees,
                         anim::SpecPtr zoomSpec, anim::SpecPtr panSpec, anim::SpecPtr rotationSpec, Offset centroid) {
        if (!(zoomFactor > 0)) co_return;   // zoom value should be greater than 0
        anim::Animatable a(anim::Vec{ 1, 0, 0, 0 }, 4);
        bool stopped = false;
        TransformSession session(state, MutatePriority::Default, [&] { stopped = true; a.Stop(); });
        if (!session.token) co_return;
        anim::Vec previous{ 1, 0, 0, 0 };
        auto spec = std::make_shared<DelegatingSpec>(std::move(zoomSpec), std::move(panSpec), std::move(rotationSpec));
        co_await a.AnimateTo(anim::Vec{ zoomFactor, panOffset.x, panOffset.y, rotationDegrees }, spec, anim::Vec{ 0, 0, 0, 0 },
                             [&](const anim::Vec& v) {
            if (stopped) return;
            state.TransformByWithCentroid(centroid, previous[0] == 0 ? 1 : v[0] / previous[0],
                                          { v[1] - previous[1], v[2] - previous[2] }, v[3] - previous[3]);
            previous = v;
        });
    }

    rt::Task<> AnimateZoomBy(TransformableState& state, float zoomFactor, anim::SpecPtr spec, Offset centroid) {
        if (!(zoomFactor > 0)) co_return;
        anim::Animatable a(1.0f);
        bool stopped = false;
        TransformSession session(state, MutatePriority::Default, [&] { stopped = true; a.Stop(); });
        if (!session.token) co_return;
        float previous = 1;
        co_await a.AnimateTo(anim::Vec{ zoomFactor, 0, 0, 0 }, std::move(spec), {}, [&](const anim::Vec& v) {
            if (stopped) return;
            state.TransformByWithCentroid(centroid, previous == 0 ? 1 : v[0] / previous, {}, 0);
            previous = v[0];
        });
    }

    rt::Task<> AnimateRotateBy(TransformableState& state, float degrees, anim::SpecPtr spec, Offset centroid) {
        anim::Animatable a(0.0f);
        bool stopped = false;
        TransformSession session(state, MutatePriority::Default, [&] { stopped = true; a.Stop(); });
        if (!session.token) co_return;
        float previous = 0;
        co_await a.AnimateTo(anim::Vec{ degrees, 0, 0, 0 }, std::move(spec), {}, [&](const anim::Vec& v) {
            if (stopped) return;
            state.TransformByWithCentroid(centroid, 1, {}, v[0] - previous);
            previous = v[0];
        });
    }

    rt::Task<> AnimatePanBy(TransformableState& state, Offset offset, anim::SpecPtr spec, Offset centroid) {
        anim::Animatable a(anim::Vec{ 0, 0, 0, 0 }, 2);
        bool stopped = false;
        TransformSession session(state, MutatePriority::Default, [&] { stopped = true; a.Stop(); });
        if (!session.token) co_return;
        Offset previous{};
        co_await a.AnimateTo(anim::Vec{ offset.x, offset.y, 0, 0 }, std::move(spec), {}, [&](const anim::Vec& v) {
            if (stopped) return;
            state.TransformByWithCentroid(centroid, 1, { v[0] - previous.x, v[1] - previous.y }, 0);
            previous = { v[0], v[1] };
        });
    }

    void ZoomBy(TransformableState& state, float zoomFactor, Offset centroid) {
        TransformSession session(state, MutatePriority::Default, {});
        if (session.token) state.TransformByWithCentroid(centroid, zoomFactor, {}, 0);
    }
    void RotateBy(TransformableState& state, float degrees, Offset centroid) {
        TransformSession session(state, MutatePriority::Default, {});
        if (session.token) state.TransformByWithCentroid(centroid, 1, {}, degrees);
    }
    void PanBy(TransformableState& state, Offset offset, Offset centroid) {
        TransformSession session(state, MutatePriority::Default, {});
        if (session.token) state.TransformByWithCentroid(centroid, 1, offset, 0);
    }

}
