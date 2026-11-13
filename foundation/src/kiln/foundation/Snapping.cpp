#include "kiln/foundation/Snapping.h"
#include <kiln/Function.h>

#include <cmath>
#include <limits>

namespace kiln::foundation {

    namespace {
        constexpr float kMinFlingVelocityDp = 400;   // MinFlingVelocityDp

        float CoerceToTarget(float v, float target) {
            if (target == 0) return 0;
            return target > 0 ? std::min(v, target) : std::max(v, target);
        }
        float Sign(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }

        struct Result { float remaining; float velocity; };

        // animateWithTarget: spring to targetOffset, never past cancelOffset.
        rt::Task<Result> AnimateWithTarget(const kiln::Function<float(float)>& scrollBy, float target, float cancel,
                                           float velocity, anim::SpecPtr spec) {
            anim::Animatable a(0.0f);
            float consumedUpToNow = 0;
            // The animation state's velocity: cancelling keeps it, stopping an Animatable does not.
            float lastVelocity = velocity;
            co_await a.AnimateTo(anim::Vec{ target, 0, 0, 0 }, std::move(spec), anim::Vec{ velocity, 0, 0, 0 }, [&](const anim::Vec& v) {
                const float real = CoerceToTarget(v[0], cancel);
                const float delta = real - consumedUpToNow;
                const float consumed = scrollBy(delta);
                lastVelocity = a.Velocity()[0];
                if (std::abs(delta - consumed) > 0.5f || real != v[0]) a.Stop();
                consumedUpToNow += consumed;
            });
            co_return Result{ target - consumedUpToNow, CoerceToTarget(lastVelocity, velocity) };
        }

        // animateDecay: decay until targetOffset.
        rt::Task<Result> AnimateDecayTo(const kiln::Function<float(float)>& scrollBy, float target, float velocity,
                                        std::shared_ptr<const anim::DecaySpec> decay) {
            anim::Animatable a(0.0f);
            float previous = 0;
            float lastVelocity = velocity;
            co_await a.AnimateDecay(velocity, decay, [&](float value) {
                lastVelocity = a.Velocity()[0];
                auto consume = [&](float delta) {
                    const float consumed = scrollBy(delta);
                    if (std::abs(delta - consumed) > 0.5f) a.Stop();
                };
                if (std::abs(value) >= std::abs(target)) {
                    const float final = CoerceToTarget(value, target);
                    consume(final - previous);
                    a.Stop();
                    previous = final;
                } else {
                    consume(value - previous);
                    previous = value;
                }
            });
            co_return Result{ target - previous, lastVelocity };
        }

        rt::Task<float> SnapFling(std::shared_ptr<SnapLayoutInfoProvider> provider, std::shared_ptr<const anim::DecaySpec> decay,
                                  anim::SpecPtr snap, kiln::Function<float(float)> scrollBy, float velocity,
                                  kiln::Function<void(float)> onRemaining = {}) {
            const float decayOffset = decay->TargetValue(0, velocity);
            const float initial = provider->CalculateApproachOffset(velocity, decayOffset);
            float remaining = std::abs(initial) * Sign(velocity);
            float approachVelocity = velocity;
            // onRemainingDistanceUpdated: the target of the phase running, minus what it scrolled.
            float phaseLeft = remaining;
            if (onRemaining) {
                onRemaining(phaseLeft);
                scrollBy = [inner = std::move(scrollBy), onRemaining, &phaseLeft](float d) {
                    const float used = inner(d);
                    phaseLeft -= used;
                    onRemaining(phaseLeft);
                    return used;
                };
            }
            if (remaining != 0 && velocity != 0) {
                // A decay when it reaches the approach target, else a spring there.
                if (std::abs(decay->TargetValue(0, velocity)) >= std::abs(remaining)) {
                    const Result r = co_await AnimateDecayTo(scrollBy, remaining, velocity, decay);
                    approachVelocity = r.velocity;
                } else {
                    const Result r = co_await AnimateWithTarget(scrollBy, std::abs(remaining) * Sign(velocity), remaining, velocity, snap);
                    approachVelocity = r.velocity;
                }
            }
            const float snapOffset = provider->CalculateSnapOffset(approachVelocity);
            phaseLeft = snapOffset;
            if (onRemaining) onRemaining(phaseLeft);
            const Result r = co_await AnimateWithTarget(scrollBy, snapOffset, snapOffset, approachVelocity, snap);
            if (onRemaining) onRemaining(0);
            co_return r.remaining == 0 ? 0.0f : r.velocity;
        }

        // calculateFinalOffset over the visible items' distances to the snap position.
        float FinalOffset(const std::vector<std::pair<int, int>>& items /* offset, size */, int viewport, int before, int after,
                          SnapPosition position, float velocity, float density) {
            float lower = -std::numeric_limits<float>::infinity(), upper = std::numeric_limits<float>::infinity();
            for (const auto& [offset, size] : items) {
                const float d = float(offset - position.Position(viewport, size, before, after));
                if (d <= 0 && d > lower) lower = d;
                if (d >= 0 && d < upper) upper = d;
            }
            float result;
            if (std::abs(velocity) < kMinFlingVelocityDp * density) result = std::abs(upper) <= std::abs(lower) ? upper : lower;
            else result = velocity > 0 ? upper : lower;
            return std::isfinite(result) ? result : 0.0f;
        }

        class ListProvider final : public SnapLayoutInfoProvider {
        public:
            ListProvider(LazyListState& s, SnapPosition p) : state(s), position(p) {}
            float CalculateApproachOffset(float, float decayOffset) override {
                const auto& items = state.LayoutInfo().visibleItemsInfo;
                int average = 0;
                if (!items.empty()) { for (const auto& it : items) average += it.size; average /= int(items.size()); }
                return std::max(std::abs(decayOffset) - float(average), 0.0f) * Sign(decayOffset);
            }
            float CalculateSnapOffset(float velocity) override {
                const auto& info = state.LayoutInfo();
                std::vector<std::pair<int, int>> items;
                for (const auto& it : info.visibleItemsInfo) items.push_back({ it.offset, it.size });
                const int viewport = info.orientation == Orientation::Vertical ? info.viewportSize.height : info.viewportSize.width;
                return FinalOffset(items, viewport, info.beforeContentPadding, info.afterContentPadding, position, velocity, state.Density());
            }
            LazyListState& state;
            SnapPosition position;
        };

        class GridProvider final : public SnapLayoutInfoProvider {
        public:
            GridProvider(LazyGridState& s, SnapPosition p) : state(s), position(p) {}
            bool Vertical() const { return state.LayoutInfo().orientation == Orientation::Vertical; }
            float CalculateApproachOffset(float, float decayOffset) override {
                // LazyGridSnapLayoutInfoProvider: the average line size.
                const auto& items = state.LayoutInfo().visibleItemsInfo;
                std::map<int, int> lines;
                for (const auto& it : items) {
                    const int line = Vertical() ? it.row : it.column;
                    lines[line] = std::max(lines[line], Vertical() ? it.size.height : it.size.width);
                }
                int average = 0;
                if (!lines.empty()) { for (const auto& [l, s] : lines) average += s; average /= int(lines.size()); }
                return std::max(std::abs(decayOffset) - float(average), 0.0f) * Sign(decayOffset);
            }
            float CalculateSnapOffset(float velocity) override {
                const auto& info = state.LayoutInfo();
                std::vector<std::pair<int, int>> items;
                for (const auto& it : info.visibleItemsInfo)
                    items.push_back(Vertical() ? std::pair{ it.offset.y, it.size.height } : std::pair{ it.offset.x, it.size.width });
                const int viewport = Vertical() ? info.viewportSize.height : info.viewportSize.width;
                return FinalOffset(items, viewport, info.beforeContentPadding, info.afterContentPadding, position, velocity, state.Density());
            }
            LazyGridState& state;
            SnapPosition position;
        };

        FlingBehavior Remember(LazyStateBase& state, std::shared_ptr<SnapLayoutInfoProvider> provider) {
            LazyStateBase* sp = &state;
            return SnapFlingBehavior(std::move(provider), [sp] {
                // rememberSplineBasedDecay: the platform scroller's curve at this density.
                return std::make_shared<anim::SplineDecay>(sp->Density());
            });
        }
    }

    int SnapPosition::Position(int layoutSize, int itemSize, int before, int after, int index, int count) const {
        if (custom) return custom(layoutSize, itemSize, before, after, index, count);
        const int available = layoutSize - before - after;
        switch (kind) {
            case Kind::Start: return 0;
            case Kind::Center: return available / 2 - itemSize / 2;
            case Kind::End: return available - itemSize;
        }
        return 0;
    }

    FlingBehavior SnapFlingBehavior(std::shared_ptr<SnapLayoutInfoProvider> provider,
                                    kiln::Function<std::shared_ptr<const anim::DecaySpec>()> decay, anim::SpecPtr snap) {
        return [provider, decay, snap](kiln::Function<float(float)> scrollBy, float velocity) {
            return SnapFling(provider, decay(), snap, std::move(scrollBy), velocity);
        };
    }

    TargetedFlingBehavior TargetedSnapFlingBehavior(std::shared_ptr<SnapLayoutInfoProvider> provider,
                                                    kiln::Function<std::shared_ptr<const anim::DecaySpec>()> decay, anim::SpecPtr snap) {
        return [provider, decay, snap](kiln::Function<float(float)> scrollBy, float velocity, kiln::Function<void(float)> onRemaining) {
            return SnapFling(provider, decay(), snap, std::move(scrollBy), velocity, std::move(onRemaining));
        };
    }

    std::shared_ptr<SnapLayoutInfoProvider> MakeSnapLayoutInfoProvider(LazyListState& state, SnapPosition position) {
        return std::make_shared<ListProvider>(state, position);
    }
    std::shared_ptr<SnapLayoutInfoProvider> MakeSnapLayoutInfoProvider(LazyGridState& state, SnapPosition position) {
        return std::make_shared<GridProvider>(state, position);
    }

    FlingBehavior RememberSnapFlingBehavior(LazyListState& state, SnapPosition position, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto provider = c.Remember<std::shared_ptr<SnapLayoutInfoProvider>>([&] { return MakeSnapLayoutInfoProvider(state, position); },
                                                                             rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(position.kind)), key);
        return Remember(state, provider);
    }
    FlingBehavior RememberSnapFlingBehavior(LazyGridState& state, SnapPosition position, rt::Key key) {
        auto& c = *rt::Composition::Current();
        auto provider = c.Remember<std::shared_ptr<SnapLayoutInfoProvider>>([&] { return MakeSnapLayoutInfoProvider(state, position); },
                                                                             rt::Keys(reinterpret_cast<std::uintptr_t>(&state), int(position.kind)), key);
        return Remember(state, provider);
    }

}
