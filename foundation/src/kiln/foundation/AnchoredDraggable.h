#pragma once

// anchoredDraggable (AnchoredDraggable.kt): a one-axis drag between keyed anchor positions.
// The state tracks the offset, the anchor passed most recently (currentValue), the anchor it is
// settled at (settledValue) and the target; releasing flings to the next anchor past the
// velocity threshold (125 dp/s) or the positional one (half the distance), with a tween.
//
// Mutual exclusion (MutatorMutex in Compose): every operation takes the state over, and the
// operation it replaced stops at its next frame. A user drag is replaced by a programmatic
// animation started meanwhile, and the other way round. Changing the anchors while an
// animation runs restarts it towards the new target.

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Draggable.h"

#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace kiln::foundation {

    class OverscrollEffect;

    template<typename T>
    class DraggableAnchors {
    public:
        DraggableAnchors() = default;
        DraggableAnchors(std::initializer_list<std::pair<T, float>> anchors) {
            for (const auto& [k, v] : anchors) At(k, v);
        }
        // `key at position` (DraggableAnchorsConfig): a later position for the same key wins.
        DraggableAnchors& At(T key, float position) {
            for (std::size_t i = 0; i < m_Keys.size(); ++i)
                if (m_Keys[i] == key) { m_Positions[i] = position; return *this; }
            m_Keys.push_back(std::move(key));
            m_Positions.push_back(position);
            return *this;
        }

        float PositionOf(const T& key) const {
            for (std::size_t i = 0; i < m_Keys.size(); ++i) if (m_Keys[i] == key) return m_Positions[i];
            return std::numeric_limits<float>::quiet_NaN();
        }
        bool HasPositionFor(const T& key) const { return Index(key) >= 0; }
        // The anchor nearest to `position`; ties go to the later anchor.
        std::optional<T> ClosestAnchor(float position) const {
            int best = -1;
            float min = std::numeric_limits<float>::infinity();
            for (std::size_t i = 0; i < m_Positions.size(); ++i) {
                const float d = std::abs(position - m_Positions[i]);
                if (d <= min) { best = int(i); min = d; }
            }
            if (best < 0) return std::nullopt;
            return m_Keys[best];
        }
        // The nearest anchor at or above (searchUpwards) / at or below `position`.
        std::optional<T> ClosestAnchor(float position, bool searchUpwards) const {
            int best = -1;
            float min = std::numeric_limits<float>::infinity();
            for (std::size_t i = 0; i < m_Positions.size(); ++i) {
                const float delta = searchUpwards ? m_Positions[i] - position : position - m_Positions[i];
                const float d = delta < 0 ? std::numeric_limits<float>::infinity() : delta;
                if (d <= min) { best = int(i); min = d; }
            }
            if (best < 0) return std::nullopt;
            return m_Keys[best];
        }
        float MinPosition() const {
            if (m_Positions.empty()) return std::numeric_limits<float>::quiet_NaN();
            float m = m_Positions[0];
            for (float p : m_Positions) m = std::min(m, p);
            return m;
        }
        float MaxPosition() const {
            if (m_Positions.empty()) return std::numeric_limits<float>::quiet_NaN();
            float m = m_Positions[0];
            for (float p : m_Positions) m = std::max(m, p);
            return m;
        }
        std::size_t Size() const { return m_Keys.size(); }
        const T& AnchorAt(std::size_t i) const { return m_Keys[i]; }
        float PositionAt(std::size_t i) const { return m_Positions[i]; }
        bool operator==(const DraggableAnchors& o) const { return m_Keys == o.m_Keys && m_Positions == o.m_Positions; }

    private:
        int Index(const T& key) const {
            for (std::size_t i = 0; i < m_Keys.size(); ++i) if (m_Keys[i] == key) return int(i);
            return -1;
        }
        std::vector<T> m_Keys;
        std::vector<float> m_Positions;
    };

    using PositionalThreshold = std::function<float(float totalDistance)>;

    // AnchoredDraggableDefaults.
    namespace anchored_draggable {
        inline anim::SpecPtr SnapAnimationSpec() { return anim::Tween(); }
        inline float DefaultPositionalThreshold(float distance) { return distance / 2; }
        inline std::shared_ptr<const anim::DecaySpec> DecayAnimationSpec() { return std::make_shared<anim::ExponentialDecay>(); }
        inline constexpr float MinFlingVelocityDp = 125;   // AnchoredDraggableMinFlingVelocity
    }

    // What the modifier drives; AnchoredDraggableState<T> implements it.
    class AnchoredDragTarget {
    public:
        virtual ~AnchoredDragTarget() = default;
        virtual void BeginUserDrag() = 0;
        // Moves by `delta` (clamped to the anchors); returns the part used.
        virtual float UserDragBy(float delta) = 0;
        virtual void EndUserDrag() = 0;
        // The release fling to the target anchor; returns the velocity left over.
        virtual rt::Task<float> Fling(float velocity, float velocityThreshold, PositionalThreshold positional, anim::SpecPtr snap) = 0;
        virtual float MinPosition() const = 0;
        virtual float MaxPosition() const = 0;
        virtual float OffsetValue() const = 0;
        virtual bool AnimationRunning() const = 0;
    };

    template<typename T>
    class AnchoredDraggableState final : public AnchoredDragTarget {
    public:
        explicit AnchoredDraggableState(T initialValue, std::function<bool(const T&)> confirmValueChange = {})
            : m_Confirm(std::move(confirmValueChange)), m_Current(initialValue), m_Settled(initialValue) {}
        AnchoredDraggableState(T initialValue, DraggableAnchors<T> anchors, std::function<bool(const T&)> confirmValueChange = {})
            : AnchoredDraggableState(std::move(initialValue), std::move(confirmValueChange)) {
            m_Anchors.Set(std::move(anchors));
            TrySnapTo(m_Current.Peek());
        }

        // NaN until the anchors are first set.
        float Offset() const { return m_Offset.Get(); }
        float RequireOffset() const { return m_Offset.Get(); }
        const T& CurrentValue() const { return m_Current.Get(); }
        const T& SettledValue() const { return m_Settled.Get(); }
        // dragTarget ?: the closest anchor (preferring currentValue at a shared position).
        T TargetValue() const {
            const auto& dt = m_DragTarget.Get();
            if (dt) return *dt;
            const float off = m_Offset.Get();
            const T& cur = m_Current.Get();
            if (std::isnan(off)) return cur;
            const auto& a = m_Anchors.Get();
            const float curOff = a.PositionOf(cur);
            if (std::isnan(curOff) || off == curOff) return cur;
            return a.ClosestAnchor(off).value_or(cur);
        }
        bool IsAnimationRunning() const { return m_DragTarget.Get().has_value(); }
        float LastVelocity() const { return m_LastVelocity.Get(); }
        const DraggableAnchors<T>& Anchors() const { return m_Anchors.Get(); }

        // Fraction of the way from `from` to `to`, in [0, 1]; 1 when they coincide.
        float Progress(const T& from, const T& to) const {
            const auto& a = m_Anchors.Get();
            const float f = a.PositionOf(from), t = a.PositionOf(to);
            const float o = std::clamp(m_Offset.Get(), std::min(f, t), std::max(f, t));
            const float fraction = (o - f) / (t - f);
            if (std::isnan(fraction)) return 1;
            return fraction < 1e-6f ? 0 : fraction > 1 - 1e-6f ? 1 : std::abs(fraction);
        }

        // Snaps to `newTarget` (default: the closest anchor, or the target) when idle; restarts a
        // running animation towards it otherwise. Call from layout when anchors depend on size.
        void UpdateAnchors(DraggableAnchors<T> anchors, std::optional<T> newTarget = std::nullopt) {
            if (m_Anchors.Peek() == anchors) return;
            T target = newTarget ? *newTarget
                     : !std::isnan(m_Offset.Peek()) ? anchors.ClosestAnchor(m_Offset.Peek()).value_or(TargetValuePeek())
                     : TargetValuePeek();
            m_Anchors.Set(std::move(anchors));
            if (m_Busy) {
                // A running animation restarts towards the new target, a release fling from
                // where it is with its velocity (restartable anchoredDrag); a drag carries on
                // (clamped by the new anchors on its next delta).
                if (m_Owner == Owner::Run) {
                    m_DragTarget.Set(target);
                    m_Restart = true;
                    m_Anim.Stop();
                } else if (m_Owner == Owner::Fling) {
                    m_Restart = true;
                    m_Anim.Stop();
                }
            } else {
                TrySnapTo(target);
            }
        }

        // dispatchRawDelta: moves outside of a drag, clamped to the anchors; returns what was used.
        float DispatchRawDelta(float delta) {
            const float n = NewOffsetForDelta(delta);
            const float used = n - m_Offset.Peek();
            DragTo(n, 0);
            return used;
        }

        rt::Task<> SnapTo(T target) { co_await Run(std::move(target), Op{ Op::Snap }); }
        rt::Task<> AnimateTo(T target, anim::SpecPtr spec = anchored_draggable::SnapAnimationSpec()) {
            Op op{ Op::Animate };
            op.spec = std::move(spec);
            co_await Run(std::move(target), std::move(op));
        }
        // Decays towards `target` when the fling would carry past it, otherwise tweens; returns
        // the velocity consumed.
        rt::Task<float> AnimateToWithDecay(T target, float velocity,
                                           anim::SpecPtr snap = anchored_draggable::SnapAnimationSpec(),
                                           std::shared_ptr<const anim::DecaySpec> decay = anchored_draggable::DecayAnimationSpec()) {
            Op op{ Op::Decay };
            op.spec = std::move(snap);
            op.decay = std::move(decay);
            op.velocity = velocity;
            const float remaining = co_await Run(std::move(target), std::move(op));
            co_return velocity - remaining;
        }
        // settle(animationSpec): to the closest anchor, or back to currentValue if vetoed.
        rt::Task<> Settle(anim::SpecPtr spec = anchored_draggable::SnapAnimationSpec()) {
            const T previous = m_Current.Peek();
            auto t = m_Anchors.Peek().ClosestAnchor(m_Offset.Peek());
            co_await AnimateTo(t && Confirm(*t) ? *t : previous, std::move(spec));
        }

        // AnchoredDragTarget
        void BeginUserDrag() override { m_UserGen = Take(); m_Busy = true; m_Owner = Owner::User; }
        float UserDragBy(float delta) override {
            if (m_UserGen != m_Gen) return 0;   // replaced by an animation mid-drag
            const float n = NewOffsetForDelta(delta);
            const float used = n - (std::isnan(m_Offset.Peek()) ? 0 : m_Offset.Peek());
            DragTo(n, 0);
            return used;
        }
        void EndUserDrag() override {
            if (m_UserGen != m_Gen) return;
            m_Busy = false;
            SettleIfAtAnchor();
        }
        rt::Task<float> Fling(float velocity, float velocityThreshold, PositionalThreshold positional, anim::SpecPtr snap) override {
            if (std::isnan(m_Offset.Peek()) || m_Anchors.Peek().Size() == 0) co_return velocity;
            const auto gen = Take();
            m_Busy = true;
            m_Owner = Owner::Fling;
            for (;;) {
                m_Restart = false;
                const T proposed = ComputeTarget(m_Offset.Peek(), velocity, positional, velocityThreshold);
                const T target = Confirm(proposed) ? proposed : m_Settled.Peek();
                co_await AnimateOffset(m_Anchors.Peek().PositionOf(target), velocity, snap);
                if (gen != m_Gen) co_return 0.0f;
                if (!m_Restart) break;
            }
            m_Busy = false;
            SettleIfAtAnchor();
            co_return 0.0f;
        }
        float MinPosition() const override { return m_Anchors.Peek().MinPosition(); }
        float MaxPosition() const override { return m_Anchors.Peek().MaxPosition(); }
        float OffsetValue() const override { return m_Offset.Peek(); }
        bool AnimationRunning() const override { return IsAnimationRunning(); }

    private:
        struct Op {
            enum Kind { Snap, Animate, Decay } kind;
            anim::SpecPtr spec;
            std::shared_ptr<const anim::DecaySpec> decay;
            float velocity = 0;
        };

        bool Confirm(const T& v) const { return !m_Confirm || m_Confirm(v); }
        T TargetValuePeek() const {
            if (m_DragTarget.Peek()) return *m_DragTarget.Peek();
            const float off = m_Offset.Peek();
            if (std::isnan(off)) return m_Current.Peek();
            return m_Anchors.Peek().ClosestAnchor(off).value_or(m_Current.Peek());
        }
        float NewOffsetForDelta(float delta) const {
            const float base = std::isnan(m_Offset.Peek()) ? 0 : m_Offset.Peek();
            const auto& a = m_Anchors.Peek();
            if (a.Size() == 0) return base + delta;
            return std::clamp(base + delta, a.MinPosition(), a.MaxPosition());
        }
        // Takes the state over: the running operation stops at its next frame.
        std::uint64_t Take() {
            ++m_Gen;
            m_Restart = false;
            m_Anim.Stop();
            if (m_DragTarget.Peek()) m_DragTarget.Set(std::nullopt);
            return m_Gen;
        }

        bool TrySnapTo(const T& target) {
            if (m_Busy) return false;
            const float off = m_Anchors.Peek().PositionOf(target);
            if (!std::isnan(off)) {
                DragTo(off, 0);
                if (m_DragTarget.Peek()) m_DragTarget.Set(std::nullopt);
            }
            m_Current.Set(target);
            m_Settled.Set(target);
            return true;
        }

        void SettleIfAtAnchor() {
            const auto& a = m_Anchors.Peek();
            auto closest = a.ClosestAnchor(m_Offset.Peek());
            if (closest && std::abs(m_Offset.Peek() - a.PositionOf(*closest)) < 0.5f && Confirm(*closest)) {
                m_Settled.Set(*closest);
                m_Current.Set(*closest);
            }
        }

        // anchoredDrag(targetValue) { ... } with the restart-on-new-anchors loop.
        rt::Task<float> Run(T target, Op op) {
            if (!m_Anchors.Peek().HasPositionFor(target)) {
                if (Confirm(target)) { m_Settled.Set(target); m_Current.Set(target); }
                co_return op.velocity;
            }
            const auto gen = Take();
            m_Busy = true;
            m_Owner = Owner::Run;
            m_DragTarget.Set(target);
            float remaining = op.velocity;
            for (;;) {
                m_Restart = false;
                const T latest = *m_DragTarget.Peek();
                remaining = co_await Body(latest, op);
                if (gen != m_Gen) co_return remaining;   // replaced; the new owner has the state
                if (!m_Restart) break;
            }
            m_Busy = false;
            if (Confirm(target)) {
                DragTo(m_Anchors.Peek().PositionOf(target), m_LastVelocity.Peek());
                m_Settled.Set(target);
                m_Current.Set(target);
            }
            m_DragTarget.Set(std::nullopt);
            co_return remaining;
        }

        rt::Task<float> Body(T latest, const Op& op) {
            const float targetOffset = m_Anchors.Peek().PositionOf(latest);
            switch (op.kind) {
                case Op::Snap:
                    if (!std::isnan(targetOffset)) DragTo(targetOffset, 0);
                    co_return 0.0f;
                case Op::Animate:
                    co_await AnimateOffset(targetOffset, m_LastVelocity.Peek(), op.spec);
                    co_return 0.0f;
                case Op::Decay:
                    co_return co_await DecayTo(targetOffset, op);
            }
            co_return 0.0f;
        }

        rt::Task<> AnimateOffset(float targetOffset, float velocity, anim::SpecPtr spec) {
            const float prev = std::isnan(m_Offset.Peek()) ? 0 : m_Offset.Peek();
            if (std::isnan(targetOffset) || prev == targetOffset) co_return;
            m_Anim.SnapTo(prev);
            co_await m_Anim.AnimateTo(anim::Vec{ targetOffset, 0, 0, 0 }, std::move(spec), anim::Vec{ velocity, 0, 0, 0 },
                                      [this](const anim::Vec& v) { DragTo(v[0], m_Anim.Velocity()[0]); });
        }

        rt::Task<float> DecayTo(float targetOffset, const Op& op) {
            const float velocity = op.velocity;
            if (std::isnan(targetOffset)) co_return velocity;
            float prev = std::isnan(m_Offset.Peek()) ? 0 : m_Offset.Peek();
            if (prev == targetOffset) co_return velocity;
            if (velocity * (targetOffset - prev) < 0 || velocity == 0) {
                co_await AnimateOffset(targetOffset, velocity, op.spec);
                co_return 0.0f;
            }
            const float projected = op.decay->TargetValue(prev, velocity);
            const bool canDecay = velocity > 0 ? projected >= targetOffset : projected <= targetOffset;
            if (!canDecay) {
                co_await AnimateOffset(targetOffset, velocity, op.spec);
                co_return 0.0f;
            }
            float remaining = velocity;
            m_Anim.SnapTo(prev);
            co_await m_Anim.AnimateDecay(velocity, op.decay, [&, targetOffset](float value) {
                const float v = m_Anim.Velocity()[0];
                if ((value < targetOffset && prev > targetOffset) || (value > targetOffset && prev < targetOffset)) {
                    const float finalValue = targetOffset == 0 ? 0 : targetOffset > 0 ? std::min(value, targetOffset) : std::max(value, targetOffset);
                    DragTo(finalValue, v);
                    remaining = std::isnan(v) ? 0 : v;
                    prev = finalValue;
                    m_Anim.Stop();
                } else {
                    DragTo(value, v);
                    remaining = v;
                    prev = value;
                }
            });
            co_return remaining;
        }

        T ComputeTarget(float offset, float velocity, const PositionalThreshold& positional, float velocityThreshold) const {
            const auto& a = m_Anchors.Peek();
            const bool moving = std::abs(velocity) > 0;
            const bool forward = moving && velocity > 0;
            if (!moving) return *a.ClosestAnchor(offset);
            if (std::abs(velocity) >= std::abs(velocityThreshold)) return *a.ClosestAnchor(offset, forward);
            const T left = *a.ClosestAnchor(offset, false), right = *a.ClosestAnchor(offset, true);
            const float lp = a.PositionOf(left), rp = a.PositionOf(right);
            const float threshold = std::abs(positional(std::abs(lp - rp)));
            const float fromStart = forward ? lp : rp;
            const bool crossed = std::abs(fromStart - offset) >= threshold;
            return crossed ? (forward ? right : left) : (forward ? left : right);
        }

        // AnchoredDragScope.dragTo: moves and updates currentValue once past half the distance
        // between the anchors around the offset.
        void DragTo(float newOffset, float velocity) {
            const float previous = m_Offset.Peek();
            m_Offset.Set(newOffset);
            if (m_LastVelocity.Peek() != velocity) m_LastVelocity.Set(velocity);
            if (std::isnan(previous)) return;
            const bool forward = newOffset >= previous;
            const auto& a = m_Anchors.Peek();
            if (a.Size() == 0) return;
            // updateBounds
            const T cur = m_Current.Peek();
            std::optional<T> left, right;
            if (newOffset == a.PositionOf(cur)) {
                const T next = a.ClosestAnchor(newOffset + (forward ? 1.0f : -1.0f), forward).value_or(cur);
                if (forward) { left = cur; right = next; } else { left = next; right = cur; }
            } else {
                left = a.ClosestAnchor(newOffset, false).value_or(cur);
                right = a.ClosestAnchor(newOffset, true).value_or(cur);
            }
            const float distance = std::abs(a.PositionOf(*left) - a.PositionOf(*right));
            if (std::abs(newOffset - a.PositionOf(cur)) >= distance / 2) {
                const T closest = forward ? *right : *left;
                if (Confirm(closest) && !(closest == cur)) m_Current.Set(closest);
            }
        }

        std::function<bool(const T&)> m_Confirm;
        rt::State<T> m_Current, m_Settled;
        rt::State<float> m_Offset{ std::numeric_limits<float>::quiet_NaN() };
        rt::State<float> m_LastVelocity{ 0 };
        rt::State<std::optional<T>> m_DragTarget{ std::nullopt };
        rt::State<DraggableAnchors<T>> m_Anchors{ DraggableAnchors<T>{} };
        anim::Animatable m_Anim{ 0.0f };
        std::uint64_t m_Gen = 0, m_UserGen = 0;
        enum class Owner : std::uint8_t { User, Fling, Run };
        Owner m_Owner = Owner::Run;
        bool m_Busy = false;
        bool m_Restart = false;
    };

    struct AnchoredDraggableOptions {
        bool enabled = true;
        // nullopt: reversed for horizontal drags in RTL.
        std::optional<bool> reverseDirection;
        std::shared_ptr<MutableInteractionSource> interactionSource;
        std::shared_ptr<OverscrollEffect> overscroll;
        PositionalThreshold positionalThreshold = anchored_draggable::DefaultPositionalThreshold;
        anim::SpecPtr snapAnimationSpec = anchored_draggable::SnapAnimationSpec();
        // Runs once a release fling has settled, as a FlingBehavior that wraps the default one
        // and continues after it (not when the fling is cancelled).
        std::function<rt::Task<>()> afterFling;
    };

    Modifier AnchoredDraggable(Modifier m, AnchoredDragTarget& state, Orientation orientation, AnchoredDraggableOptions options = {});

}
