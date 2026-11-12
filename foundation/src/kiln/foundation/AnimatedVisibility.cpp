#include "kiln/foundation/AnimatedVisibility.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/VelocityTracker.h"

#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>

namespace kiln::foundation {

    using namespace ui;

    namespace {
        // EnterTransition.plus: per kind, the right-hand transition's part wins.
        EnterExitParts Combine(const EnterExitParts& a, const EnterExitParts& b) {
            EnterExitParts r = b;
            if (!r.fade) r.fade = a.fade;
            if (!r.scale) r.scale = a.scale;
            if (!r.slide) r.slide = a.slide;
            if (!r.changeSize) r.changeSize = a.changeSize;
            if (!r.veil) r.veil = a.veil;
            return r;
        }
        // Size and offset animations use a 1 px visibility threshold (EnterExitTransition.kt:1615,
        // :1822). They run here as a 0..1 progress whose spec is scaled to the distance in pixels.
        anim::SpecPtr SizeSpring() { return DefaultSpatialEnterExitSpring(); }
    }

    EnterTransition EnterTransition::operator+(const EnterTransition& o) const { return { Combine(parts, o.parts) }; }
    ExitTransition ExitTransition::operator+(const ExitTransition& o) const { return { Combine(parts, o.parts) }; }

    anim::SpecPtr DefaultEnterExitSpring() { return anim::Spring(1.0f, anim::spring::StiffnessMediumLow); }
    anim::SpecPtr DefaultSpatialEnterExitSpring() {
        static const anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 1);
        return spec;
    }

    EnterTransition FadeIn(anim::SpecPtr s, float a) { EnterTransition t; t.parts.fade = { { a, std::move(s) } }; return t; }
    ExitTransition FadeOut(anim::SpecPtr s, float a) { ExitTransition t; t.parts.fade = { { a, std::move(s) } }; return t; }
    EnterTransition ScaleIn(anim::SpecPtr s, float sc, float ox, float oy) { EnterTransition t; t.parts.scale = EnterExitParts::Scale{ sc, ox, oy, std::move(s) }; return t; }
    ExitTransition ScaleOut(anim::SpecPtr s, float sc, float ox, float oy) { ExitTransition t; t.parts.scale = EnterExitParts::Scale{ sc, ox, oy, std::move(s) }; return t; }
    EnterTransition SlideIn(std::function<IntOffset(IntSize)> f, anim::SpecPtr s) { EnterTransition t; t.parts.slide = EnterExitParts::Slide{ std::move(f), s ? s : SizeSpring() }; return t; }
    ExitTransition SlideOut(std::function<IntOffset(IntSize)> f, anim::SpecPtr s) { ExitTransition t; t.parts.slide = EnterExitParts::Slide{ std::move(f), s ? s : SizeSpring() }; return t; }
    EnterTransition SlideInVertically(std::function<int(int)> f, anim::SpecPtr s) { return SlideIn([f](IntSize sz) { return IntOffset{ 0, f(sz.height) }; }, std::move(s)); }
    ExitTransition SlideOutVertically(std::function<int(int)> f, anim::SpecPtr s) { return SlideOut([f](IntSize sz) { return IntOffset{ 0, f(sz.height) }; }, std::move(s)); }
    EnterTransition SlideInHorizontally(std::function<int(int)> f, anim::SpecPtr s) { return SlideIn([f](IntSize sz) { return IntOffset{ f(sz.width), 0 }; }, std::move(s)); }
    ExitTransition SlideOutHorizontally(std::function<int(int)> f, anim::SpecPtr s) { return SlideOut([f](IntSize sz) { return IntOffset{ f(sz.width), 0 }; }, std::move(s)); }
    EnterTransition ExpandIn(anim::SpecPtr s, Alignment a, bool clip, std::function<IntSize(IntSize)> f) {
        EnterTransition t; t.parts.changeSize = EnterExitParts::ChangeSize{ a, std::move(f), clip, s }; return t;
    }
    ExitTransition ShrinkOut(anim::SpecPtr s, Alignment a, bool clip, std::function<IntSize(IntSize)> f) {
        ExitTransition t; t.parts.changeSize = EnterExitParts::ChangeSize{ a, std::move(f), clip, s }; return t;
    }
    EnterTransition ExpandVertically(anim::SpecPtr s, AlignmentV a, bool clip, std::function<int(int)> f) {
        return ExpandIn(std::move(s), Alignment{ 0, a.bias }, clip, [f](IntSize sz) { return IntSize{ sz.width, f(sz.height) }; });
    }
    ExitTransition ShrinkVertically(anim::SpecPtr s, AlignmentV a, bool clip, std::function<int(int)> f) {
        return ShrinkOut(std::move(s), Alignment{ 0, a.bias }, clip, [f](IntSize sz) { return IntSize{ sz.width, f(sz.height) }; });
    }
    EnterTransition ExpandHorizontally(anim::SpecPtr s, AlignmentH a, bool clip, std::function<int(int)> f) {
        return ExpandIn(std::move(s), Alignment{ a.bias, 0 }, clip, [f](IntSize sz) { return IntSize{ f(sz.width), sz.height }; });
    }
    ExitTransition ShrinkHorizontally(anim::SpecPtr s, AlignmentH a, bool clip, std::function<int(int)> f) {
        return ShrinkOut(std::move(s), Alignment{ a.bias, 0 }, clip, [f](IntSize sz) { return IntSize{ f(sz.width), sz.height }; });
    }

    EnterTransition UnveilIn(anim::SpecPtr spec, Color initialColor, bool matchParentSize) {
        EnterTransition t;
        t.parts.veil = EnterExitParts::Veil{ initialColor, initialColor.WithAlpha(0), std::move(spec), matchParentSize };
        return t;
    }
    ExitTransition VeilOut(anim::SpecPtr spec, Color targetColor, bool matchParentSize) {
        ExitTransition t;
        t.parts.veil = EnterExitParts::Veil{ targetColor.WithAlpha(0), targetColor, std::move(spec), matchParentSize };
        return t;
    }

    void TransformScope::Reset() {
        m_Alpha.Set(1);
        m_AlphaMutated.Set(false);
        m_Scale.Set(1);
        m_ScaleMutated.Set(false);
        m_Offset = {};
        m_OffsetMutated = false;
        m_Origin.Set(TransformOrigin::Center());
        m_OriginMutated.Set(false);
        m_Veil.Set(Color::Transparent());
        m_VeilMutated.Set(false);
    }

    std::shared_ptr<MutableContentTransform> MakeMutableContentTransform(bool initialVeilMatchParentSize, bool targetVeilMatchParentSize,
                                                                         std::function<ui::Offset()> initialOffsetVelocityProvider,
                                                                         std::function<ui::Offset()> targetOffsetVelocityProvider,
                                                                         std::function<void(MutableContentTransform&)> block) {
        auto t = std::make_shared<MutableContentTransform>();
        t->initialTransform->veilMatchParentSize = initialVeilMatchParentSize;
        t->initialTransform->offsetVelocityProvider = std::move(initialOffsetVelocityProvider);
        t->targetTransform->veilMatchParentSize = targetVeilMatchParentSize;
        t->targetTransform->offsetVelocityProvider = std::move(targetOffsetVelocityProvider);
        if (block) block(*t);
        return t;
    }

    namespace {
        // --- SharedMutableTransformState (DeferredEnterExitTransition.kt) ----------------------

        enum class MutationPhase : std::uint8_t { Idle, MutatingPendingCatchUp, MutatingCatchingUp, Mutating, Handoff };

        anim::Vec OklabVec(Color c) { const auto v = ui::ToOklab(c); return { v[0], v[1], v[2], v[3] }; }
        Color FromVec(const anim::Vec& v) { return ui::FromOklab({ v[0], v[1], v[2], v[3] }); }
        std::int64_t NowMillis() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        class SharedMutableTransformState {
        public:
            rt::State<MutationPhase> mutationPhase{ MutationPhase::Idle };
            TransformScope transformScope;
            std::shared_ptr<MutableTransform> mutableData, lastMutableData;

            bool IsMutating() const {
                const MutationPhase p = mutationPhase.Peek();
                return p == MutationPhase::MutatingPendingCatchUp || p == MutationPhase::MutatingCatchingUp || p == MutationPhase::Mutating;
            }
            bool IsHandoffActive() const { return mutationPhase.Peek() == MutationPhase::Handoff; }

            void SetMutableData(std::shared_ptr<MutableTransform> v) {
                if (v) lastMutableData = v;
                mutableData = std::move(v);
            }

            void UpdateMutationState(bool isMutating, bool isSettled) {
                const MutationPhase current = mutationPhase.Peek();
                if (isMutating) {
                    if (current == MutationPhase::Idle || current == MutationPhase::Handoff) {
                        transformScope.Reset();
                        mutationPhase.Set(isSettled ? MutationPhase::Mutating : MutationPhase::MutatingPendingCatchUp);
                        m_ScaleTracker.Reset();
                        m_OffsetTracker.Reset();
                        m_ProgressTracker.Reset();
                        m_LastScaleMillis = m_LastSlideMillis = m_LastProgressMillis = std::numeric_limits<std::int64_t>::min();
                        scaleHandoffVelocity.reset();
                        slideHandoffVelocity.reset();
                        sharedElementProgressHandoffVelocity = 0;
                    }
                } else if (IsMutating()) {
                    mutationPhase.Set(MutationPhase::Handoff);
                    CalculateHandoffVelocities();
                }
            }

            float ResolveSharedElementProgress() {
                if (!mutableData || !mutableData->sharedElementProgressProvider) return 0;
                const float progress = std::clamp(mutableData->sharedElementProgressProvider(), 0.0f, 1.0f);
                if (IsMutating()) Track(m_ProgressTracker, m_LastProgressMillis, { progress, 0 });
                return progress;
            }

            // The values last shown, and those the transition had when the phase began.
            Color lastVeil = Color::Transparent();
            float lastAlpha = 1, lastScale = 1;
            TransformOrigin lastTransformOrigin;
            IntOffset lastSlide{};
            float activeTransitionAlpha = 1, activeTransitionScale = 1;
            IntOffset activeTransitionSlide{};
            Color activeTransitionVeil = Color::Transparent();
            TransformOrigin activeTransitionTransformOrigin;
            float initialManualAlpha = 1, initialManualScale = 1;
            IntOffset initialManualSlide{};
            Color initialManualVeil = Color::Transparent();
            TransformOrigin initialManualTransformOrigin;
            anim::Animatable catchUpAlpha{ 1.0f }, catchUpScale{ 1.0f };
            anim::Animatable catchUpSlide{ anim::Vec{}, 2 }, catchUpVeil{ OklabVec(Color::Transparent()), 4 };
            anim::Animatable catchUpTransformOrigin{ anim::Vec{ 0.5f, 0.5f, 0, 0 }, 2 };
            std::optional<float> scaleHandoffVelocity;
            std::optional<ui::Offset> slideHandoffVelocity;
            float sharedElementProgressHandoffVelocity = 0;
            IntSize lastFullSize{};   // the content's size at the last layout

            // startCatchUp: an interrupted transition catches up with the hand-set values with
            // springs, the hand-set changes applied on top.
            static rt::Task<> StartCatchUp(std::shared_ptr<SharedMutableTransformState> self) {
                auto& s = *self;
                const float initialAlpha = s.activeTransitionAlpha, initialScale = s.activeTransitionScale;
                const IntOffset initialSlide = s.activeTransitionSlide;
                const Color initialVeil = s.activeTransitionVeil;
                const TransformOrigin initialOrigin = s.activeTransitionTransformOrigin;
                s.initialManualAlpha = s.transformScope.Alpha();
                s.initialManualScale = s.transformScope.Scale();
                s.initialManualSlide = s.transformScope.Offset();
                s.initialManualVeil = s.transformScope.VeilMutated() ? s.transformScope.Veil() : initialVeil.WithAlpha(0);
                s.initialManualTransformOrigin = s.transformScope.GetTransformOrigin();
                s.catchUpAlpha.SnapTo(initialAlpha);
                s.catchUpScale.SnapTo(initialScale);
                s.catchUpSlide.SnapTo(anim::Vec{ float(initialSlide.x), float(initialSlide.y), 0, 0 });
                s.catchUpVeil.SnapTo(OklabVec(initialVeil));
                s.catchUpTransformOrigin.SnapTo(anim::Vec{ initialOrigin.pivotFractionX, initialOrigin.pivotFractionY, 0, 0 });
                if (s.mutationPhase.Peek() != MutationPhase::MutatingPendingCatchUp) co_return;
                s.mutationPhase.Set(MutationPhase::MutatingCatchingUp);
                std::vector<rt::Job> jobs;
                auto run = [](anim::Animatable* a, anim::Vec to) -> rt::Task<> { co_await a->AnimateTo(to); };
                if (initialAlpha != s.initialManualAlpha) jobs.push_back(rt::Launch(rt::Dispatcher::Main(), run(&s.catchUpAlpha, { s.initialManualAlpha, 0, 0, 0 })));
                if (initialScale != s.initialManualScale) jobs.push_back(rt::Launch(rt::Dispatcher::Main(), run(&s.catchUpScale, { s.initialManualScale, 0, 0, 0 })));
                if (!(initialSlide == s.initialManualSlide))
                    jobs.push_back(rt::Launch(rt::Dispatcher::Main(), run(&s.catchUpSlide, { float(s.initialManualSlide.x), float(s.initialManualSlide.y), 0, 0 })));
                if (!(initialVeil == s.initialManualVeil)) jobs.push_back(rt::Launch(rt::Dispatcher::Main(), run(&s.catchUpVeil, OklabVec(s.initialManualVeil))));
                if (!(initialOrigin == s.initialManualTransformOrigin))
                    jobs.push_back(rt::Launch(rt::Dispatcher::Main(), run(&s.catchUpTransformOrigin,
                                                                           { s.initialManualTransformOrigin.pivotFractionX, s.initialManualTransformOrigin.pivotFractionY, 0, 0 })));
                // coroutineScope { launch ... }: wait for every catch-up.
                while (std::any_of(jobs.begin(), jobs.end(), [](const rt::Job& j) { return !j.Done(); })) co_await rt::NextFrame();
                if (s.mutationPhase.Peek() == MutationPhase::MutatingCatchingUp) s.mutationPhase.Set(MutationPhase::Mutating);
            }

            bool HasBlock() const { return lastMutableData && lastMutableData->block; }
            std::optional<Color> VeilHandoffValue() const { return IsHandoffActive() ? std::optional(lastVeil) : std::nullopt; }
            std::optional<float> AlphaHandoffValue() const { return IsHandoffActive() ? std::optional(lastAlpha) : std::nullopt; }
            std::optional<float> ScaleHandoffValue() const { return IsHandoffActive() ? std::optional(lastScale) : std::nullopt; }
            std::optional<TransformOrigin> TransformOriginHandoffValue() const {
                return IsHandoffActive() ? std::optional(lastTransformOrigin) : std::nullopt;
            }
            std::optional<IntOffset> SlideHandoffValue() const { return IsHandoffActive() ? std::optional(lastSlide) : std::nullopt; }

            void EvaluateTransformBlock(IntSize fullSize) {
                if (IsMutating() && mutableData && mutableData->block) mutableData->block(transformScope, fullSize);
            }

            float CombinedAlpha(float transitionValue) {
                activeTransitionAlpha = transitionValue;
                const MutationPhase p = mutationPhase.Get();
                float combined = transitionValue;
                if (p == MutationPhase::MutatingCatchingUp && catchUpAlpha.IsRunning())
                    combined = catchUpAlpha.Float() + (transformScope.Alpha() - initialManualAlpha);
                else if (p == MutationPhase::MutatingCatchingUp || p == MutationPhase::Mutating) combined = transformScope.Alpha();
                if (IsMutating()) lastAlpha = combined;
                return combined;
            }

            float CombinedScale(float transitionValue) {
                activeTransitionScale = transitionValue;
                const bool mutated = IsMutating() && transformScope.ScaleMutated();
                const MutationPhase p = mutationPhase.Get();
                float combined = transitionValue;
                if (p == MutationPhase::MutatingCatchingUp && catchUpScale.IsRunning())
                    combined = catchUpScale.Float() + (transformScope.Scale() - initialManualScale);
                else if (p == MutationPhase::MutatingCatchingUp || p == MutationPhase::Mutating) combined = transformScope.Scale();
                if (IsMutating()) {
                    lastScale = combined;
                    if (mutated) Track(m_ScaleTracker, m_LastScaleMillis, { combined, 0 });
                }
                return combined;
            }

            TransformOrigin CombinedTransformOrigin(TransformOrigin transitionValue) {
                activeTransitionTransformOrigin = transitionValue;
                const MutationPhase p = mutationPhase.Get();
                TransformOrigin combined = transitionValue;
                if (p == MutationPhase::MutatingCatchingUp && catchUpTransformOrigin.IsRunning()) {
                    const anim::Vec& v = catchUpTransformOrigin.Value();
                    combined = { v[0], v[1] };
                } else if (p == MutationPhase::MutatingCatchingUp || p == MutationPhase::Mutating) {
                    combined = transformScope.GetTransformOrigin();
                }
                if (IsMutating()) lastTransformOrigin = combined;
                return combined;
            }

            IntOffset CombinedSlide(IntOffset transitionValue, IntSize fullSize) {
                lastFullSize = fullSize;
                EvaluateTransformBlock(fullSize);
                activeTransitionSlide = transitionValue;
                const bool mutated = IsMutating() && transformScope.OffsetMutated();
                const MutationPhase p = mutationPhase.Get();
                IntOffset combined = transitionValue;
                if (p == MutationPhase::MutatingCatchingUp && catchUpSlide.IsRunning()) {
                    const anim::Vec& v = catchUpSlide.Value();
                    const IntOffset o = transformScope.Offset();
                    combined = { int(std::lround(v[0])) + (o.x - initialManualSlide.x), int(std::lround(v[1])) + (o.y - initialManualSlide.y) };
                } else if (p == MutationPhase::MutatingCatchingUp || p == MutationPhase::Mutating) {
                    combined = transformScope.Offset();
                }
                if (IsMutating()) {
                    lastSlide = combined;
                    if (mutated) Track(m_OffsetTracker, m_LastSlideMillis, { float(combined.x), float(combined.y) });
                }
                return combined;
            }

            Color CombinedVeil(Color transitionValue) {
                activeTransitionVeil = transitionValue;
                const Color targetVeil = transformScope.VeilMutated() ? transformScope.Veil() : transitionValue.WithAlpha(0);
                const MutationPhase p = mutationPhase.Get();
                Color combined = transitionValue;
                if (p == MutationPhase::MutatingCatchingUp && catchUpVeil.IsRunning()) combined = FromVec(catchUpVeil.Value());
                else if (p == MutationPhase::MutatingCatchingUp || p == MutationPhase::Mutating) combined = targetVeil;
                if (IsMutating()) lastVeil = combined;
                return combined;
            }

            void Clear() {
                if (mutationPhase.Peek() != MutationPhase::Idle) mutationPhase.Set(MutationPhase::Idle);
                transformScope.Reset();
                lastVeil = Color::Transparent();
                lastAlpha = 1;
                lastScale = 1;
                lastTransformOrigin = TransformOrigin::Center();
                lastSlide = {};
                m_ScaleTracker.Reset();
                m_OffsetTracker.Reset();
                m_ProgressTracker.Reset();
                m_LastScaleMillis = m_LastSlideMillis = m_LastProgressMillis = std::numeric_limits<std::int64_t>::min();
                scaleHandoffVelocity.reset();
                slideHandoffVelocity.reset();
                sharedElementProgressHandoffVelocity = 0;
                lastMutableData.reset();
                mutableData.reset();
            }

            // getHandoffExit / getHandoffEnter: transitions that hold (exit) or start from (enter)
            // the last hand-set values of the properties the phase set.
            ExitTransition HandoffExit() const {
                ExitTransition t;
                if (!HasBlock() || !IsHandoffActive()) return t;
                if (transformScope.ScaleMutated()) t = t + ScaleOut(DefaultEnterExitSpring(), lastScale);
                if (transformScope.AlphaMutated()) t = t + FadeOut(DefaultEnterExitSpring(), lastAlpha);
                if (transformScope.OffsetMutated()) { const IntOffset o = lastSlide; t = t + SlideOut([o](IntSize) { return o; }); }
                if (transformScope.VeilMutated()) t = t + VeilOut(DefaultEnterExitSpring(), lastVeil, lastMutableData->veilMatchParentSize);
                return t;
            }
            EnterTransition HandoffEnter() const {
                EnterTransition t;
                if (!HasBlock() || !IsHandoffActive()) return t;
                if (transformScope.ScaleMutated()) t = t + ScaleIn(DefaultEnterExitSpring(), lastScale);
                if (transformScope.AlphaMutated()) t = t + FadeIn(DefaultEnterExitSpring(), lastAlpha);
                if (transformScope.OffsetMutated()) { const IntOffset o = lastSlide; t = t + SlideIn([o](IntSize) { return o; }); }
                if (transformScope.VeilMutated()) {
                    EnterTransition v;
                    v.parts.veil = EnterExitParts::Veil{ lastVeil, lastVeil.WithAlpha(0), DefaultEnterExitSpring(), lastMutableData->veilMatchParentSize };
                    t = t + v;
                }
                return t;
            }

        private:
            void CalculateHandoffVelocities() {
                const float sv = m_ScaleTracker.CalculateVelocity().x;
                scaleHandoffVelocity = std::isnan(sv) ? 0.0f : sv;
                std::optional<ui::Offset> v;
                if (lastMutableData && lastMutableData->offsetVelocityProvider) v = lastMutableData->offsetVelocityProvider();
                if (v && !std::isnan(v->x) && !std::isnan(v->y)) {
                    slideHandoffVelocity = *v;
                } else {
                    const ui::Offset t = m_OffsetTracker.CalculateVelocity();
                    slideHandoffVelocity = ui::Offset{ std::isnan(t.x) ? 0.0f : t.x, std::isnan(t.y) ? 0.0f : t.y };
                }
                const float pv = m_ProgressTracker.CalculateVelocity().x;
                sharedElementProgressHandoffVelocity = std::isnan(pv) ? 0.0f : pv;
            }
            static void Track(ui::VelocityTracker& tracker, std::int64_t& last, ui::Offset value) {
                const std::int64_t now = NowMillis();
                if (now <= last) return;
                last = now;
                tracker.AddPosition(now, value);
            }
            ui::VelocityTracker m_ScaleTracker, m_OffsetTracker, m_ProgressTracker;
            std::int64_t m_LastScaleMillis = std::numeric_limits<std::int64_t>::min(), m_LastSlideMillis = m_LastScaleMillis,
                         m_LastProgressMillis = m_LastScaleMillis;
        };

        // EnterExitTransitionTracker: keeps the shared state in step with the transition.
        struct EnterExitTracker {
            std::shared_ptr<SharedMutableTransformState> state = std::make_shared<SharedMutableTransformState>();
            bool wasMutating = false;
            std::optional<EnterExitState> lastTarget;
            bool startCatchUp = false;
            bool hasBeenVisible = false;
            // The enter / exit in use: kept from before a target change so an interrupted
            // animation still knows its ends (trackActiveEnter / trackActiveExit).
            std::optional<EnterTransition> activeEnter;
            std::optional<ExitTransition> activeExit;
            // The distances (px) the slide and size progress stand for, measured by the layout.
            std::shared_ptr<float> slideDistance = std::make_shared<float>(0), sizeDistance = std::make_shared<float>(0);

            void TrackActive(const EnterTransition& enter, const ExitTransition& exit, EnterExitState current, EnterExitState target) {
                if (!activeEnter) activeEnter = enter;
                if (!activeExit) activeExit = exit;
                const bool settledVisible = current == target && current == EnterExitState::Visible;
                if (settledVisible) {
                    activeEnter = EnterTransition{};
                    activeExit = ExitTransition{};
                    return;
                }
                if (target != EnterExitState::PostExit) {
                    // Properties the deferred phase set stay tracked even when the enter has
                    // none, so an interruption does not snap them.
                    activeEnter = *activeEnter + (state->HandoffEnter() + enter);
                }
                if (target != EnterExitState::Visible) {
                    // Exiting again after coming back: the previous exit's parts first animate
                    // to rest, unless hand-set values are being applied on top of them.
                    ExitTransition neutral = *activeExit;
                    if (!state->IsMutating()) {
                        auto& np = neutral.parts;
                        if (np.fade) np.fade->first = 1;
                        if (np.scale) np.scale->scale = 1;
                        if (np.slide) np.slide->offset = [](IntSize) { return IntOffset{}; };
                        if (np.changeSize) np.changeSize->size = [](IntSize full) { return full; };
                        if (np.veil) np.veil->targetColor = np.veil->initialColor;
                    }
                    activeExit = neutral + state->HandoffExit() + exit;
                }
            }

            void Update(std::shared_ptr<MutableTransform> data, std::optional<EnterExitState> pending, EnterExitState current, EnterExitState target) {
                state->SetMutableData(std::move(data));
                const bool isMutating = pending.has_value() && state->mutableData != nullptr;
                const bool isSettled = current == target;
                state->UpdateMutationState(isMutating, isSettled);
                const bool cleanup = lastTarget && ((isSettled && !isMutating) || (*lastTarget != target && !isMutating && !wasMutating));
                if (cleanup) state->Clear();
                if (!isMutating) startCatchUp = false;
                else if (!wasMutating) startCatchUp = !isSettled;
                lastTarget = target;
                wasMutating = isMutating;
            }
        };
    }

    namespace {
        // The enter/exit layout: content measured at full size, reported at the animated size
        // (aligned by the expand alignment), shifted by the slide.
        struct EnterExitParams {
            std::function<IntSize(IntSize)> sizeAt;     // null: full size
            std::function<IntOffset(IntSize)> slideAt;  // null: none
            Alignment alignment;
            std::function<void(IntSize full)> measured;   // the main pass's full size
            bool operator==(const EnterExitParams&) const { return false; }   // new lambdas each frame
        };
        struct EnterExitNode final : ModifierNode, LayoutModifier {
            EnterExitParams params;
            // In a lookahead scope, the size the lookahead pass measured: the full size the
            // main pass animates toward.
            std::optional<IntSize> lookaheadSize;
            explicit EnterExitNode(EnterExitParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            void OnAttach() override { lookaheadSize.reset(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                const IntSize measured{ p.Width(), p.Height() };
                if (s.IsLookingAhead()) {
                    lookaheadSize = measured;
                    return s.Layout(measured.width, measured.height, [&p](PlacementScope& place) { place.Place(p, 0, 0); });
                }
                const IntSize full = lookaheadSize.value_or(measured);
                if (params.measured) params.measured(full);
                const IntSize size = params.sizeAt ? c.Constrain(params.sizeAt(full)) : measured;
                const IntOffset align = params.alignment.Align(full, size, s.Direction());
                const IntOffset slide = params.slideAt ? params.slideAt(full) : IntOffset{};
                return s.Layout(size.width, size.height, [&p, align, slide](PlacementScope& place) {
                    place.Place(p, align.x + slide.x, align.y + slide.y);
                });
            }
        };
        Modifier EnterExitLayout(Modifier m, EnterExitParams p) { return m.With<EnterExitNode>(std::move(p)); }

        // AnimatedVisibilityImpl's layout: in lookahead, content on its way out takes no space
        // (where the layout ends up), while the main pass animates it away.
        struct LookaheadHideParams {
            const Transition<bool>* transition;
            bool operator==(const LookaheadHideParams&) const = default;
        };
        struct LookaheadHideNode final : ModifierNode, LayoutModifier {
            LookaheadHideParams params;
            explicit LookaheadHideNode(LookaheadHideParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            LayoutModifier* AsLayout() override { return this; }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Placeable& p = m.Measure(c);
                const bool hidden = s.IsLookingAhead() && !params.transition->TargetState();
                return s.Layout(hidden ? 0 : p.Width(), hidden ? 0 : p.Height(), [&p](PlacementScope& place) { place.Place(p, 0, 0); });
            }
        };

        IntSize Lerp(IntSize a, IntSize b, float t) {
            return { int(std::lround(float(a.width) + float(b.width - a.width) * t)), int(std::lround(float(a.height) + float(b.height - a.height) * t)) };
        }
    }

    Modifier detail::LookaheadHide(const Modifier& m, const Transition<bool>& t) {
        return Modifier(m).With<LookaheadHideNode>(LookaheadHideParams{ &t });
    }

    void AnimatedVisibility(bool visible, const Modifier& modifier, std::function<void()> content,
                            EnterTransition enter, ExitTransition exit, rt::Key key) {
        auto& t = UpdateTransition(visible, key);
        AnimatedEnterExit(t, detail::LookaheadHide(modifier, t), std::move(content), enter, exit, key);
    }

    void AnimatedVisibility(MutableTransitionState<bool>& state, const Modifier& modifier, std::function<void()> content,
                            EnterTransition enter, ExitTransition exit, rt::Key key) {
        auto& t = UpdateTransition(state, key);
        AnimatedEnterExit(t, detail::LookaheadHide(modifier, t), std::move(content), enter, exit, key);
    }
    void AnimatedVisibilityInRow(bool visible, const Modifier& m, std::function<void()> content, EnterTransition enter, ExitTransition exit, rt::Key key) {
        AnimatedVisibility(visible, m, std::move(content), std::move(enter), std::move(exit), key);
    }
    void AnimatedVisibilityInRow(MutableTransitionState<bool>& s, const Modifier& m, std::function<void()> content, EnterTransition enter, ExitTransition exit, rt::Key key) {
        AnimatedVisibility(s, m, std::move(content), std::move(enter), std::move(exit), key);
    }
    void AnimatedVisibilityInColumn(bool visible, const Modifier& m, std::function<void()> content, EnterTransition enter, ExitTransition exit, rt::Key key) {
        AnimatedVisibility(visible, m, std::move(content), std::move(enter), std::move(exit), key);
    }
    void AnimatedVisibilityInColumn(MutableTransitionState<bool>& s, const Modifier& m, std::function<void()> content, EnterTransition enter, ExitTransition exit, rt::Key key) {
        AnimatedVisibility(s, m, std::move(content), std::move(enter), std::move(exit), key);
    }

    void AnimatedEnterExit(Transition<bool>& t, const Modifier& modifier, std::function<void()> content,
                           const EnterTransition& enterArg, const ExitTransition& exitArg, rt::Key key,
                           std::shared_ptr<MutableTransform> mutableTransform, bool forceVisible) {
        auto& c = *rt::Composition::Current();
        auto& tracker = c.Remember<std::shared_ptr<EnterExitTracker>>([] { return std::make_shared<EnterExitTracker>(); }, {}, key);
        // targetEnterExit: where each of the transition's states puts this content.
        const std::optional<bool> pending = t.PendingTargetState();
        const bool pendingVisible = pending && *pending;
        if (t.CurrentState() || pendingVisible) tracker->hasBeenVisible = true;
        const auto stateOf = [&](bool visible) {
            if (visible) return EnterExitState::Visible;
            if (pendingVisible) return EnterExitState::PreEnter;
            return tracker->hasBeenVisible ? EnterExitState::PostExit : EnterExitState::PreEnter;
        };
        const EnterExitState current = stateOf(t.CurrentState()), target = stateOf(t.TargetState());
        const std::optional<EnterExitState> pendingState = pending ? std::optional(stateOf(*pending)) : std::nullopt;
        tracker->Update(mutableTransform, pendingState, current, target);
        auto state = tracker->state;
        if (tracker->startCatchUp) c.LaunchedEffect({}, [state] { return SharedMutableTransformState::StartCatchUp(state); }, key);

        tracker->TrackActive(enterArg, exitArg, current, target);
        const EnterTransition& enter = *tracker->activeEnter;
        const ExitTransition& exit = *tracker->activeExit;
        const auto specFor = [&](auto part, anim::SpecPtr fallback) -> anim::SpecPtr {
            if (current == EnterExitState::PreEnter && target == EnterExitState::Visible) { auto& e = enter.parts.*part; return e && e->spec ? e->spec : fallback; }
            if (current == EnterExitState::Visible && target == EnterExitState::PostExit) { auto& x = exit.parts.*part; return x && x->spec ? x->spec : fallback; }
            return fallback;
        };
        // Values by enter/exit state: rest when visible, the enter's before entering, the exit's
        // after exiting.
        const auto byState = [&](bool s, auto visibleValue, auto preEnter, auto postExit) {
            const EnterExitState e = stateOf(s);
            return e == EnterExitState::Visible ? visibleValue : e == EnterExitState::PreEnter ? preEnter : postExit;
        };
        using Seg = Transition<bool>::Segment;
        const float alpha = AnimateFloat<bool>(t, [&](bool s) {
            return byState(s, 1.0f, enter.parts.fade ? enter.parts.fade->first : 1.0f, exit.parts.fade ? exit.parts.fade->first : 1.0f);
        }, [&](const Seg&) {
            const auto& f = (current == EnterExitState::Visible && target == EnterExitState::PostExit) ? exit.parts.fade : enter.parts.fade;
            const bool directional = (current == EnterExitState::PreEnter && target == EnterExitState::Visible) ||
                                     (current == EnterExitState::Visible && target == EnterExitState::PostExit);
            return directional && f ? f->second : DefaultEnterExitSpring();
        }, rt::Key("alpha"), state->AlphaHandoffValue() ? std::optional(anim::Vec{ *state->AlphaHandoffValue(), 0, 0, 0 }) : std::nullopt);
        const float scale = AnimateFloat<bool>(t, [&](bool s) {
            return byState(s, 1.0f, enter.parts.scale ? enter.parts.scale->scale : 1.0f, exit.parts.scale ? exit.parts.scale->scale : 1.0f);
        }, [&](const Seg&) { return specFor(&EnterExitParts::scale, anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 0.002f)); }, rt::Key("scale"),
            state->ScaleHandoffValue() ? std::optional(anim::Vec{ *state->ScaleHandoffValue(), 0, 0, 0 }) : std::nullopt,
            state->scaleHandoffVelocity ? std::optional(anim::Vec{ *state->scaleHandoffVelocity, 0, 0, 0 }) : std::nullopt);
        // The scale's pivot: if only one side scales, both use its origin.
        const auto originOf = [](const std::optional<EnterExitParts::Scale>& sc) {
            return sc ? std::optional(TransformOrigin{ sc->originX, sc->originY }) : std::nullopt;
        };
        const std::optional<TransformOrigin> enterOrigin = originOf(enter.parts.scale), exitOrigin = originOf(exit.parts.scale);
        const bool animateScale = enter.parts.scale || exit.parts.scale || (state->HasBlock() && state->transformScope.ScaleMutated()) || state->lastScale != 1;
        const TransformOrigin whenVisible = current == EnterExitState::PreEnter ? enterOrigin.value_or(exitOrigin.value_or(TransformOrigin::Center()))
                                                                               : exitOrigin.value_or(enterOrigin.value_or(TransformOrigin::Center()));
        const anim::Vec originV = detail::AnimateChild<bool>(t, 2, [&](const bool& s) {
            if (!animateScale) return anim::Vec{ 0.5f, 0.5f, 0, 0 };
            const TransformOrigin o = byState(s, whenVisible, enterOrigin.value_or(exitOrigin.value_or(TransformOrigin::Center())),
                                              exitOrigin.value_or(TransformOrigin::Center()));
            return anim::Vec{ o.pivotFractionX, o.pivotFractionY, 0, 0 };
        }, [](const Seg&) { return anim::Spring(); }, rt::Key("origin"),
            state->TransformOriginHandoffValue()
                ? std::optional(anim::Vec{ state->TransformOriginHandoffValue()->pivotFractionX, state->TransformOriginHandoffValue()->pivotFractionY, 0, 0 })
                : std::nullopt);
        const TransformOrigin origin{ originV[0], originV[1] };
        const anim::Vec veilV = detail::AnimateChild<bool>(t, 4, [&](const bool& s) {
            const Color visible = enter.parts.veil ? enter.parts.veil->targetColor : exit.parts.veil ? exit.parts.veil->initialColor : Color::Transparent();
            return OklabVec(byState(s, visible, enter.parts.veil ? enter.parts.veil->initialColor : Color::Transparent(),
                                    exit.parts.veil ? exit.parts.veil->targetColor : Color::Transparent()));
        }, [&](const Seg&) { return specFor(&EnterExitParts::veil, DefaultEnterExitSpring()); }, rt::Key("veil"),
            state->VeilHandoffValue() ? std::optional(OklabVec(*state->VeilHandoffValue())) : std::nullopt);
        const Color veil = FromVec(veilV);
        // Slide and size run as a 0..1 progress towards the side's own offset and size; a part
        // the moving side does not have stays at rest, so it cannot keep the content alive
        // (ExitTransition.None removes the content at once).
        const auto progressOf = [&](auto part) {
            return [&, part](bool s) { return byState(s, 1.0f, (enter.parts.*part) ? 0.0f : 1.0f, (exit.parts.*part) ? 0.0f : 1.0f); };
        };
        const auto slideSpec = DefaultSpatialEnterExitSpring();
        const std::shared_ptr<const float> slideDistance = tracker->slideDistance, sizeDistance = tracker->sizeDistance;
        const float slideP = AnimateFloat<bool>(t, progressOf(&EnterExitParts::slide),
                                                [&](const Seg&) { return anim::ScaledSpec(specFor(&EnterExitParts::slide, slideSpec), slideDistance); },
                                                rt::Key("slide"));
        // A hand-off of a hand-set offset: from the last offset to the side's target offset.
        const std::optional<IntOffset> slideHandoff = state->SlideHandoffValue();
        float slideHandoffVelocity = 0;
        const float slideHandoffP = AnimateFloat<bool>(t, [&](bool) { return slideHandoff ? 1.0f : 0.0f; },
                                                       [&](const Seg&) { return anim::ScaledSpec(specFor(&EnterExitParts::slide, slideSpec), slideDistance); }, rt::Key("slideHandoff"),
                                                       slideHandoff ? std::optional(anim::Vec{}) : std::nullopt,
                                                       slideHandoff ? std::optional(anim::Vec{ slideHandoffVelocity, 0, 0, 0 }) : std::nullopt);
        const float sizeP = AnimateFloat<bool>(t, progressOf(&EnterExitParts::changeSize),
                                               [&](const Seg&) { return anim::ScaledSpec(specFor(&EnterExitParts::changeSize, SizeSpring()), sizeDistance); },
                                               rt::Key("size"));

        // Gone unless visible somewhere, held, or pending to show.
        if (!(forceVisible || t.CurrentState() || t.TargetState() || pendingVisible || t.IsRunning())) return;

        const bool towardsExit = target == EnterExitState::PostExit;
        const auto& change = towardsExit ? exit.parts.changeSize : enter.parts.changeSize;
        const auto& slide = towardsExit ? exit.parts.slide : enter.parts.slide;
        EnterExitParams p;
        p.alignment = change ? change->alignment : Alignment::TopStart();
        if (change) { auto f = change->size; p.sizeAt = [f, sizeP](IntSize full) { return Lerp(f(full), full, sizeP); }; }
        std::function<IntOffset(IntSize)> slideOffset;
        if (slideHandoff) {
            const IntOffset from = *slideHandoff;
            auto to = slide ? slide->offset : std::function<IntOffset(IntSize)>();
            const bool visibleTarget = target == EnterExitState::Visible;
            slideOffset = [from, to, visibleTarget, slideHandoffP](IntSize full) {
                const IntOffset end = visibleTarget || !to ? IntOffset{} : to(full);
                return IntOffset{ int(std::lround(float(from.x) + float(end.x - from.x) * slideHandoffP)),
                                  int(std::lround(float(from.y) + float(end.y - from.y) * slideHandoffP)) };
            };
        } else if (slide) {
            auto f = slide->offset;
            slideOffset = [f, slideP](IntSize full) {
                const IntOffset o = f(full);
                return IntOffset{ int(std::lround(float(o.x) * (1 - slideP))), int(std::lround(float(o.y) * (1 - slideP))) };
            };
        }
        // combinedSlide runs the hand-set block in layout, then the layer reads its values.
        p.slideAt = [slideOffset, state](IntSize full) { return state->CombinedSlide(slideOffset ? slideOffset(full) : IntOffset{}, full); };
        // The distances the progress values stand for, from the full size.
        {
            auto slideTo = slide ? slide->offset : std::function<IntOffset(IntSize)>();
            auto sizeTo = change ? change->size : std::function<IntSize(IntSize)>();
            const std::optional<IntOffset> handoffFrom = slideHandoff;
            const bool visibleTarget = target == EnterExitState::Visible;
            auto sd = tracker->slideDistance, zd = tracker->sizeDistance;
            p.measured = [slideTo, sizeTo, handoffFrom, visibleTarget, sd, zd](IntSize full) {
                IntOffset d{};
                if (handoffFrom) {
                    const IntOffset end = visibleTarget || !slideTo ? IntOffset{} : slideTo(full);
                    d = IntOffset{ end.x - handoffFrom->x, end.y - handoffFrom->y };
                } else if (slideTo) {
                    d = slideTo(full);
                }
                *sd = float(std::max(std::abs(d.x), std::abs(d.y)));
                if (sizeTo) {
                    const IntSize s = sizeTo(full);
                    *zd = float(std::max(std::abs(full.width - s.width), std::abs(full.height - s.height)));
                }
            };
        }

        const bool clip = change && change->clip;
        const auto& veilPart = enter.parts.veil ? enter.parts.veil : exit.parts.veil;
        const bool veilMatchParent = veilPart ? veilPart->matchParentSize : (state->mutableData && state->mutableData->veilMatchParentSize);
        const auto drawVeil = [state, veil](ui::ContentDrawScope& d) {
            d.DrawContent();
            const Color v = state->CombinedVeil(veil);
            if (v.Alpha() != 0) d.DrawRect(v, {}, d.GetSize());
        };
        // [veil (matching the parent)] clip, the layout with the slide, then the layer (alpha,
        // scale and pivot follow the slid content) and [the veil under the transforms].
        Modifier m = modifier;
        if (veilMatchParent) m = DrawWithContent(std::move(m), drawVeil);
        if (clip) m = ClipToBounds(std::move(m));
        m = EnterExitLayout(std::move(m), std::move(p));
        m = GraphicsLayer(std::move(m), [state, alpha, scale, origin](GraphicsLayerParams& l) {
            l.alpha = state->CombinedAlpha(alpha);
            l.scaleX = l.scaleY = state->CombinedScale(scale);
            const TransformOrigin o = state->CombinedTransformOrigin(origin);
            l.originX = o.pivotFractionX;
            l.originY = o.pivotFractionY;
        });
        if (!veilMatchParent) m = DrawWithContent(std::move(m), drawVeil);
        // AnimatedVisibilityScope: the content sees the enter/exit state as a child transition.
        auto& scopeTransition = CreateChildTransition<EnterExitState, bool>(t, stateOf, rt::Key("enterExitScope"));
        auto& scope = c.Remember<std::shared_ptr<AnimatedVisibilityScope>>([] { return std::make_shared<AnimatedVisibilityScope>(); }, {}, key);
        scope->transition = &scopeTransition;
        // AnimatedEnterExitMeasurePolicy measures the content with the incoming constraints,
        // minimums included (a fixed-height parent stretches the content rather than top-aligning it).
        Box(m, Alignment::TopStart(), true, [scope, content = std::move(content)] {
            rt::Composition::Current()->Provide(LocalAnimatedVisibilityScope, scope.get(), content);
        }, key);
    }

    void AnimateEnterExit(const Modifier& modifier, std::function<void()> content, EnterTransition enter, ExitTransition exit, rt::Key key) {
        auto& c = *rt::Composition::Current();
        AnimatedVisibilityScope* scope = c.Read(LocalAnimatedVisibilityScope);
        if (!scope || !scope->transition) throw std::logic_error("AnimateEnterExit needs an AnimatedVisibility (or AnimatedContent) around it");
        // Visible while the scope's content is; the parent removes it after its own exit.
        auto& visible = CreateChildTransition<bool, EnterExitState>(*scope->transition, [](const EnterExitState& s) { return s == EnterExitState::Visible; }, key);
        AnimatedEnterExit(visible, modifier, std::move(content), enter, exit, key, nullptr, /*forceVisible=*/true);
    }

    void Crossfade(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content, anim::SpecPtr spec, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto& t = UpdateTransition<std::uint64_t>(target.value, key);
        auto& items = c.Remember<std::vector<std::uint64_t>>([&] { return std::vector<std::uint64_t>{ target.value }; }, {}, key);
        if (std::find(items.begin(), items.end(), target.value) == items.end()) items.push_back(target.value);
        // Finished: only the target remains.
        if (!t.IsRunning() && t.CurrentState() == t.TargetState()) items = { target.value };
        Box(modifier, Alignment::TopStart(), false, [&c, &t, &items, content, spec] {
            for (std::uint64_t k : items) {
                c.Child([&t, content, k, spec] {
                    const float a = AnimateFloat<std::uint64_t>(t, [k](std::uint64_t s) { return s == k ? 1.0f : 0.0f; },
                                                                [spec](const Transition<std::uint64_t>::Segment&) { return spec; });
                    rt::Key itemKey = rt::Key::Positional();
                    itemKey.value = k;
                    Box(Alpha(Modifier{}, a), Alignment::TopStart(), false, [content, itemKey] { content(itemKey); });
                }, rt::Key(std::int64_t(k)));
            }
        }, key);
    }

    ContentTransform DefaultContentTransform() {
        ContentTransform t;
        t.enter = FadeIn(anim::Tween(220, 90)) + ScaleIn(anim::Tween(220, 90), 0.92f);
        t.exit = FadeOut(anim::Tween(90));
        return t;
    }

    namespace {
        // SizeAnimationModifier: reports an animated size that follows the measured one.
        struct SizeAnimParams { anim::SpecPtr spec; bool operator==(const SizeAnimParams& o) const { return spec == o.spec; } };
        struct SizeAnimNode final : ModifierNode, LayoutModifier {
            SizeAnimParams params;
            std::unique_ptr<anim::Animatable> size;
            IntSize target{ -1, -1 };
            rt::Job job;
            explicit SizeAnimNode(SizeAnimParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            void OnDetach() override { job.Cancel(); }
            LayoutModifier* AsLayout() override { return this; }
            static rt::Task<> Run(anim::Animatable* a, anim::Vec to, anim::SpecPtr spec) { co_await a->AnimateTo(to, spec); }
            // In a lookahead scope: the lookahead pass's constraints (the main pass measures the
            // content with them, so it does not relayout during the animation) and size (the
            // target).
            std::optional<Constraints> lookaheadConstraints;
            std::optional<IntSize> lookaheadSize;
            void OnAttach() override { lookaheadConstraints.reset(); lookaheadSize.reset(); }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                if (s.IsLookingAhead()) {
                    lookaheadConstraints = c;
                    Placeable& p = m.Measure(c);
                    lookaheadSize = IntSize{ p.Width(), p.Height() };
                    return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
                }
                Placeable& p = m.Measure(lookaheadConstraints.value_or(c));
                const IntSize full = lookaheadSize.value_or(IntSize{ p.Width(), p.Height() });
                if (!size) {
                    size = std::make_unique<anim::Animatable>(anim::Vec{ float(full.width), float(full.height), 0, 0 }, 2);
                    target = full;
                } else if (!(full == target)) {
                    target = full;
                    job = rt::Launch(rt::Dispatcher::Main(), Run(size.get(), { float(full.width), float(full.height), 0, 0 }, params.spec));
                }
                const anim::Vec& v = size->Value();   // read: re-measures every frame of the animation
                const IntSize now = c.Constrain(IntSize{ int(std::lround(v[0])), int(std::lround(v[1])) });
                return s.Layout(now.width, now.height, [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
            }
        };
    }

    Modifier AnimateContentSize(Modifier m, anim::SpecPtr spec) {
        return m.With<SizeAnimNode>(SizeAnimParams{ std::move(spec) });
    }

    // --- AnimatedContent ----------------------------------------------------------------------

    namespace {
        bool IsLeft(AnimatedContentTransitionScope::SlideDirection d, ui::LayoutDirection dir) {
            using D = AnimatedContentTransitionScope::SlideDirection;
            return d == D::Left || (d == D::Start && dir == ui::LayoutDirection::Ltr) || (d == D::End && dir == ui::LayoutDirection::Rtl);
        }
        bool IsRight(AnimatedContentTransitionScope::SlideDirection d, ui::LayoutDirection dir) {
            using D = AnimatedContentTransitionScope::SlideDirection;
            return d == D::Right || (d == D::Start && dir == ui::LayoutDirection::Rtl) || (d == D::End && dir == ui::LayoutDirection::Ltr);
        }
        std::function<int(int)> OrIdentity(std::function<int(int)> f) { return f ? f : [](int v) { return v; }; }
    }

    EnterTransition AnimatedContentTransitionScope::SlideIntoContainer(SlideDirection towards, anim::SpecPtr spec,
                                                                       std::function<int(int)> initialOffset) const {
        auto f = OrIdentity(std::move(initialOffset));
        auto sizes = this->sizes;
        const Alignment align = contentAlignment;
        // calculateOffset: where content of the full-slide size sits in the container.
        auto offset = [sizes, align](int it) { return align.Align(IntSize{ it, it }, sizes->container, ui::LayoutDirection::Ltr); };
        if (IsLeft(towards, layoutDirection))
            return SlideInHorizontally([f, sizes, offset](int it) { return f(sizes->container.width - offset(it).x); }, spec);
        if (IsRight(towards, layoutDirection))
            return SlideInHorizontally([f, offset](int it) { return f(-offset(it).x - it); }, spec);
        if (towards == SlideDirection::Up)
            return SlideInVertically([f, sizes, offset](int it) { return f(sizes->container.height - offset(it).y); }, spec);
        if (towards == SlideDirection::Down)
            return SlideInVertically([f, offset](int it) { return f(-offset(it).y - it); }, spec);
        return {};
    }

    ExitTransition AnimatedContentTransitionScope::SlideOutOfContainer(SlideDirection towards, anim::SpecPtr spec,
                                                                       std::function<int(int)> targetOffset) const {
        auto f = OrIdentity(std::move(targetOffset));
        auto sizes = this->sizes;
        const Alignment align = contentAlignment;
        // Against the incoming content's size (it may differ from the container's).
        auto offset = [sizes, align](int it) { return align.Align(IntSize{ it, it }, sizes->target, ui::LayoutDirection::Ltr); };
        if (IsLeft(towards, layoutDirection))
            return SlideOutHorizontally([f, offset](int it) { return f(-offset(it).x - it); }, spec);
        if (IsRight(towards, layoutDirection))
            return SlideOutHorizontally([f, sizes, offset](int it) { return f(-offset(it).x + sizes->target.width); }, spec);
        if (towards == SlideDirection::Up)
            return SlideOutVertically([f, offset](int it) { return f(-offset(it).y - it); }, spec);
        if (towards == SlideDirection::Down)
            return SlideOutVertically([f, sizes, offset](int it) { return f(-offset(it).y + sizes->target.height); }, spec);
        return {};
    }

    namespace {
        // MaxInterruptionRetention: the latest interrupted contents stay composed to finish
        // their exits; older ones go.
        constexpr int MaxInterruptionRetention = 3;

        rt::Key StateKey(std::uint64_t v) {
            rt::Key k = rt::Key::Positional();
            k.value = v;
            return k;
        }

        // A scope like `root` seen as going from `initial` to `target` (PendingAnimatedContentTransitionScope).
        std::shared_ptr<AnimatedContentTransitionScope> ScopeFor(const AnimatedContentTransitionScope& root, std::uint64_t initial, std::uint64_t target) {
            auto s = std::make_shared<AnimatedContentTransitionScope>(root);
            s->initialState = StateKey(initial);
            s->targetState = StateKey(target);
            return s;
        }

        using Spec = std::function<ContentTransform(AnimatedContentTransitionScope&)>;
        using MutableSpec = std::function<std::shared_ptr<MutableContentTransform>(AnimatedContentTransitionScope&)>;

        void AnimatedContentImpl(Transition<std::uint64_t>& t, const Modifier& modifier, std::function<void(rt::Key)> content, Spec transitionSpec,
                                 Alignment alignment, MutableSpec mutableTransformSpec, rt::Key key) {
            auto& c = *rt::Composition::Current();
            if (!transitionSpec) transitionSpec = [](AnimatedContentTransitionScope&) { return DefaultContentTransform(); };
            const std::uint64_t current = t.CurrentState(), target = t.TargetState();
            const std::optional<std::uint64_t> pending = t.PendingTargetState();
            const auto segment = t.CurrentSegment();
            auto& root = c.Remember<std::shared_ptr<AnimatedContentTransitionScope>>(
                [] { return std::make_shared<AnimatedContentTransitionScope>(); }, {}, key);
            auto& visible = c.Remember<std::vector<std::uint64_t>>([&] { return std::vector<std::uint64_t>{ current }; }, {}, key);
            if (std::find(visible.begin(), visible.end(), current) == visible.end()) visible = { current };
            if (current == target && !pending) {
                // Settled: only the current content.
                visible = { current };
                root->contentAlignment = alignment;
                root->layoutDirection = c.Read(ui::LocalLayoutDirection);
            }
            if (pending && *pending != current && *pending != target) {
                if (std::find(visible.begin(), visible.end(), *pending) == visible.end()) visible.push_back(*pending);
            }
            // The target goes last (drawn on top unless a zIndex says otherwise).
            if (current != target) {
                std::erase(visible, target);
                visible.push_back(target);
            }
            root->initialState = StateKey(segment.initialState);
            root->targetState = StateKey(segment.targetState);

            // The pending phase's scope and hand-set transforms, made once per pending target.
            struct PendingData {
                std::optional<std::uint64_t> pending;
                std::uint64_t target = 0;
                std::shared_ptr<AnimatedContentTransitionScope> scope;
                std::shared_ptr<MutableContentTransform> transform;
            };
            auto& pd = c.Remember<PendingData>([] { return PendingData{}; }, {}, key);
            if (pd.pending != pending || (pending && pd.target != target)) {
                pd.pending = pending;
                pd.target = target;
                pd.scope = pending ? ScopeFor(*root, target, *pending) : nullptr;
                pd.transform = nullptr;
                if (pending) pd.transform = target != *pending && mutableTransformSpec ? mutableTransformSpec(*pd.scope) : MakeMutableContentTransform();
            }
            auto pendingScope = pd.scope;
            auto mutableData = pd.transform;
            const bool deferred = t.IsDeferred();

            // The container's size follows the segment's (or the pending phase's) size transform.
            struct ContainerCache { std::uint64_t from = 0, to = 0; AnimatedContentTransitionScope* scope = nullptr; bool valid = false; ContentTransform transform; };
            auto& cc = c.Remember<ContainerCache>([] { return ContainerCache{}; }, {}, key);
            if (!cc.valid || cc.from != segment.initialState || cc.to != segment.targetState || cc.scope != pendingScope.get()) {
                cc.transform = transitionSpec(pendingScope ? *pendingScope : *root);
                cc = { segment.initialState, segment.targetState, pendingScope.get(), true, cc.transform };
            }
            const ContentTransform containerTransform = cc.transform;
            auto sizes = root->sizes;
            Modifier m = modifier;
            if (containerTransform.sizeTransform) {
                const anim::SpecPtr spec = containerTransform.sizeTransform->spec ? containerTransform.sizeTransform->spec(sizes->container, sizes->target)
                                                                                 : anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 0.5f);
                if (containerTransform.sizeTransform->clip) m = ClipToBounds(std::move(m));
                m = AnimateContentSize(std::move(m), spec);
            }
            m = OnSizeChanged(std::move(m), [sizes](IntSize s) { sizes->container = s; });

            // AnimatedContentMeasurePolicy: the target measured first; the size of everything
            // but a pending content, each content placed at the alignment (left to right).
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([alignment](ui::MeasureScope& s, std::span<ui::Measurable* const> children,
                                                                                const ui::Constraints& cs) {
                std::vector<ui::Placeable*> placed(children.size(), nullptr);
                std::vector<char> isPending(children.size(), 0);
                for (std::size_t i = 0; i < children.size(); ++i) {
                    const std::string* tag = LayoutIdOf(*children[i]);
                    if (tag && *tag == "target") placed[i] = &children[i]->Measure(cs);
                    isPending[i] = tag && *tag == "pending";
                }
                for (std::size_t i = 0; i < children.size(); ++i)
                    if (!placed[i]) placed[i] = &children[i]->Measure(cs);
                int w = 0, h = 0;
                std::optional<std::size_t> target;
                for (std::size_t i = 0; i < children.size(); ++i) {
                    const std::string* tag = LayoutIdOf(*children[i]);
                    if (tag && *tag == "target") target = i;
                }
                if (s.IsLookingAhead()) {
                    // Lookahead: the container ends up the target's size.
                    if (target) { w = placed[*target]->Width(); h = placed[*target]->Height(); }
                } else {
                    for (std::size_t i = 0; i < children.size(); ++i) {
                        if (isPending[i]) continue;
                        w = std::max(w, placed[i]->Width());
                        h = std::max(h, placed[i]->Height());
                    }
                }
                return s.Layout(w, h, [placed, alignment, w, h](ui::PlacementScope& p) {
                    for (ui::Placeable* pl : placed) {
                        const IntOffset o = alignment.Align(IntSize{ pl->Width(), pl->Height() }, IntSize{ w, h }, ui::LayoutDirection::Ltr);
                        p.Place(*pl, o.x, o.y);
                    }
                });
            });
            const std::vector<std::uint64_t> order = visible;
            Layout(m, policy, [&c, &t, order, content, transitionSpec, root, pendingScope, mutableData, pending, target, current, deferred, sizes] {
                for (std::size_t index = 0; index < order.size(); ++index) {
                    const std::uint64_t k = order[index];
                    c.Child([&t, k, index, order, content, transitionSpec, root, pendingScope, mutableData, pending, target, current, deferred, sizes] {
                        const auto seg = t.CurrentSegment();
                        auto& cc = *rt::Composition::Current();
                        const bool isPendingContent = pending && k == *pending;
                        // The enter spec, chosen when the content's pending role changes.
                        struct Cached { std::optional<bool> roleKey; ContentTransform spec; std::optional<std::pair<bool, bool>> exitKey; ExitTransition exit; };
                        auto& cached = cc.Remember<Cached>([] { return Cached{}; });
                        if (cached.roleKey != isPendingContent) {
                            cached.roleKey = isPendingContent;
                            if (isPendingContent && pendingScope) cached.spec = transitionSpec(*pendingScope);
                            else if (k != seg.initialState && k != seg.targetState) cached.spec = transitionSpec(*ScopeFor(*root, seg.initialState, k));
                            else cached.spec = transitionSpec(*root);
                        }
                        const std::pair<bool, bool> exitKey{ seg.targetState == k, isPendingContent };
                        if (cached.exitKey != exitKey) {
                            cached.exitKey = exitKey;
                            if (seg.targetState == k || (isPendingContent && pendingScope)) cached.exit = ExitTransition{};
                            else if (k != seg.initialState && k != seg.targetState) cached.exit = transitionSpec(*ScopeFor(*root, k, seg.initialState)).exit;
                            else cached.exit = transitionSpec(*root).exit;
                        }
                        auto& visibility = CreateChildTransition<bool, std::uint64_t>(t, [k](const std::uint64_t& s) { return s == k; });
                        std::shared_ptr<MutableTransform> transform;
                        if (mutableData) transform = isPendingContent ? mutableData->targetTransform : k == target ? mutableData->initialTransform : nullptr;
                        const bool forceVisible = deferred && int(index) >= int(order.size()) - MaxInterruptionRetention;
                        const char* role = k == target ? "target" : isPendingContent && k != current ? "pending" : "other";
                        Modifier im = LayoutId(ZIndex(Modifier{}, cached.spec.targetContentZIndex), role);
                        if (k == target) im = OnSizeChanged(std::move(im), [sizes](IntSize s) { sizes->target = s; });
                        AnimatedEnterExit(visibility, im, [content, k] { content(StateKey(k)); }, cached.spec.enter, cached.exit, {}, transform, forceVisible);
                    }, rt::Key(std::int64_t(k)));
                }
            });
        }
    }

    void AnimatedContent(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform(AnimatedContentTransitionScope&)> transitionSpec,
                         Alignment alignment, rt::Key key) {
        auto& t = UpdateTransition<std::uint64_t>(target.value, key);
        AnimatedContentImpl(t, modifier, std::move(content), std::move(transitionSpec), alignment, {}, key);
    }

    void DeferredAnimatedContent(Transition<std::uint64_t>& transition, const Modifier& modifier, std::function<void(rt::Key)> content,
                                 std::function<ContentTransform(AnimatedContentTransitionScope&)> transitionSpec, Alignment alignment,
                                 std::function<std::shared_ptr<MutableContentTransform>(AnimatedContentTransitionScope&)> mutableTransformSpec, rt::Key key) {
        AnimatedContentImpl(transition, modifier, std::move(content), std::move(transitionSpec), alignment, std::move(mutableTransformSpec), key);
    }

    void AnimatedContent(Transition<std::uint64_t>& transition, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform(AnimatedContentTransitionScope&)> transitionSpec, Alignment alignment, rt::Key key) {
        AnimatedContentImpl(transition, modifier, std::move(content), std::move(transitionSpec), alignment, {}, key);
    }

    void AnimatedContent(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform()> transformFn, Alignment alignment, rt::Key key) {
        std::function<ContentTransform(AnimatedContentTransitionScope&)> spec;
        if (transformFn) spec = [transformFn](AnimatedContentTransitionScope&) { return transformFn(); };
        AnimatedContent(target, modifier, std::move(content), std::move(spec), alignment, key);
    }

}
