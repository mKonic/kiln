// constraintlayout-compose's MotionCarousel (MotionCarousel.kt) and the swipeable it drives its
// progress with (carousel/CarouselSwipeable.kt, constraintlayout's copy of material's
// Swipeable): an offset dragged between anchors, settled to one by thresholds, velocity and
// resistance. The swipeable's unused helpers (rememberCarouselSwipeableStateFor and its nested
// scroll connection) are not ported.

#include "kiln/constraintlayout/MotionLayout.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Border.h"
#include "kiln/foundation/Draggable.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/CoroutineScope.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace kiln::constraintlayout {

    namespace {

        using Anchors = std::vector<std::pair<float, std::string>>;   // mapOf: insertion order

        std::optional<float> GetOffset(const Anchors& anchors, const std::string& state) {
            for (const auto& [offset, value] : anchors)
                if (value == state) return offset;
            return std::nullopt;
        }
        const std::string* GetValue(const Anchors& anchors, float offset) {
            for (const auto& [o, value] : anchors)
                if (o == offset) return &value;
            return nullptr;
        }

        // ResistanceConfig.
        struct ResistanceConfig {
            float basis;
            float factorAtMin = 10;
            float factorAtMax = 10;
            float ComputeResistance(float overflow) const {
                const float factor = overflow < 0 ? factorAtMin : factorAtMax;
                if (factor == 0.0f) return 0;
                const float progress = std::clamp(overflow / basis, -1.0f, 1.0f);
                return basis / factor * std::sin(progress * float(M_PI) / 2);
            }
            bool operator==(const ResistanceConfig&) const = default;
        };

        // SwipeableDefaults.resistanceConfig.
        std::optional<ResistanceConfig> DefaultResistance(const Anchors& anchors) {
            if (anchors.size() <= 1) return std::nullopt;
            float lo = anchors[0].first, hi = anchors[0].first;
            for (const auto& a : anchors) {
                lo = std::min(lo, a.first);
                hi = std::max(hi, a.first);
            }
            return ResistanceConfig{ hi - lo };
        }

        std::vector<float> FindBounds(float offset, const Anchors& anchors) {
            std::optional<float> a, b;
            for (const auto& [o, v] : anchors) {
                if (o <= offset + 0.001 && (!a || o > *a)) a = o;
                if (o >= offset - 0.001 && (!b || o < *b)) b = o;
            }
            if (!a) return b ? std::vector<float>{ *b } : std::vector<float>{};
            if (!b) return { *a };
            if (*a == *b) return { *a };
            return { *a, *b };
        }

        float ComputeTarget(float offset, float lastValue, const Anchors& anchors, const kiln::Function<float(float, float)>& thresholds, float velocity,
                            float velocityThreshold) {
            const std::vector<float> bounds = FindBounds(offset, anchors);
            if (bounds.empty()) return lastValue;
            if (bounds.size() == 1) return bounds[0];
            const float lower = bounds[0], upper = bounds[1];
            if (lastValue <= offset) {
                if (velocity >= velocityThreshold) return upper;
                const float threshold = thresholds(lower, upper);
                return offset < threshold ? lower : upper;
            }
            if (velocity <= -velocityThreshold) return lower;
            const float threshold = thresholds(upper, lower);
            return offset > threshold ? upper : lower;
        }

        // CarouselSwipeableState<String>.
        class CarouselSwipeableState {
        public:
            explicit CarouselSwipeableState(std::string initialValue, anim::SpecPtr animationSpec = anim::Spring())
                : animationSpec(std::move(animationSpec)), m_CurrentValue(std::move(initialValue)) {}

            const std::string& CurrentValue() const { return m_CurrentValue.Get(); }
            bool IsAnimationRunning() const { return m_IsAnimationRunning.Get(); }
            float Offset() const { return m_Offset.Get(); }

            void EnsureInit(const Anchors& newAnchors) {
                if (anchors.Peek().empty()) {
                    const float initialOffset = GetOffset(newAnchors, m_CurrentValue.Peek()).value_or(0.0f);
                    m_Offset.Set(initialOffset);
                    m_AbsoluteOffset.Set(initialOffset);
                }
            }

            rt::Task<> ProcessNewAnchors(Anchors oldAnchors, Anchors newAnchors) {
                auto minMax = [](const Anchors& a, bool max) {
                    float v = a[0].first;
                    for (const auto& e : a) v = max ? std::max(v, e.first) : std::min(v, e.first);
                    return v;
                };
                if (oldAnchors.empty()) {
                    m_MinBound = minMax(newAnchors, false);
                    m_MaxBound = minMax(newAnchors, true);
                    co_await SnapInternalToOffset(GetOffset(newAnchors, m_CurrentValue.Peek()).value_or(0.0f));
                } else if (newAnchors != oldAnchors) {
                    m_MinBound = -std::numeric_limits<float>::infinity();
                    m_MaxBound = std::numeric_limits<float>::infinity();
                    auto nearest = [&](float to) {
                        float best = newAnchors[0].first;
                        for (const auto& e : newAnchors)
                            if (std::abs(e.first - to) < std::abs(best - to)) best = e.first;
                        return best;
                    };
                    float targetOffset;
                    if (const std::optional<float> animationTargetValue = m_AnimationTarget.Peek()) {
                        const std::string* oldState = GetValue(oldAnchors, *animationTargetValue);
                        const std::optional<float> newState = oldState ? GetOffset(newAnchors, *oldState) : std::nullopt;
                        targetOffset = newState ? *newState : nearest(*animationTargetValue);
                    } else {
                        const std::string* actualOldValue = GetValue(oldAnchors, m_Offset.Peek());
                        const std::optional<float> o = actualOldValue ? GetOffset(newAnchors, *actualOldValue) : std::nullopt;
                        targetOffset = o ? *o : nearest(m_Offset.Peek());
                    }
                    // finally: the value and bounds of the new anchors (a cancelled animation leaves
                    // the job, as upstream's snap on cancellation would).
                    struct Finally {
                        CarouselSwipeableState* s;
                        Anchors anchors;
                        float target;
                        ~Finally() {
                            if (const std::string* v = GetValue(anchors, target)) s->m_CurrentValue.Set(*v);
                            float lo = anchors[0].first, hi = anchors[0].first;
                            for (const auto& e : anchors) {
                                lo = std::min(lo, e.first);
                                hi = std::max(hi, e.first);
                            }
                            s->m_MinBound = lo;
                            s->m_MaxBound = hi;
                        }
                    } finally{ this, newAnchors, targetOffset };
                    co_await AnimateInternalToOffset(targetOffset, animationSpec);
                }
            }

            rt::Task<> SnapTo(std::string targetValue) {
                const Anchors anchors = co_await LatestNonEmptyAnchors();
                const std::optional<float> targetOffset = GetOffset(anchors, targetValue);
                if (!targetOffset) co_return;   // upstream: "The target value must have an associated anchor."
                co_await SnapInternalToOffset(*targetOffset);
                m_CurrentValue.Set(targetValue);
            }

            rt::Task<> AnimateTo(std::string targetValue, anim::SpecPtr anim) {
                const Anchors anchors = co_await LatestNonEmptyAnchors();
                struct Finally {
                    CarouselSwipeableState* s;
                    const Anchors& anchors;
                    ~Finally() {
                        const float endOffset = s->m_AbsoluteOffset.Peek();
                        for (const auto& [o, v] : anchors) {
                            if (std::abs(o - endOffset) < 0.5f) {
                                s->m_CurrentValue.Set(v);
                                return;
                            }
                        }
                    }
                } finally{ this, anchors };
                const std::optional<float> targetOffset = GetOffset(anchors, targetValue);
                if (targetOffset) co_await AnimateInternalToOffset(*targetOffset, std::move(anim));
            }

            rt::Task<> PerformFling(float velocity) {
                const Anchors anchors = co_await LatestNonEmptyAnchors();
                const float lastAnchor = GetOffset(anchors, m_CurrentValue.Peek()).value_or(m_Offset.Peek());
                const float targetValue = ComputeTarget(m_Offset.Peek(), lastAnchor, anchors, thresholds, velocity, velocityThreshold);
                if (const std::string* targetState = GetValue(anchors, targetValue)) co_await AnimateTo(*targetState, animationSpec);
                else co_await AnimateInternalToOffset(lastAnchor, animationSpec);
            }

            foundation::DraggableState draggableState{ [this](float delta) {
                const float newAbsolute = m_AbsoluteOffset.Peek() + delta;
                const float clamped = std::clamp(newAbsolute, m_MinBound, m_MaxBound);
                const float overflow = newAbsolute - clamped;
                const float resistanceDelta = resistance ? resistance->ComputeResistance(overflow) : 0.0f;
                m_Offset.Set(clamped + resistanceDelta);
                m_Overflow.Set(overflow);
                m_AbsoluteOffset.Set(newAbsolute);
            } };

            const anim::SpecPtr animationSpec;
            rt::State<Anchors> anchors{ Anchors{} };
            kiln::Function<float(float, float)> thresholds = [](float, float) { return 0.0f; };
            float velocityThreshold = 0;
            std::optional<ResistanceConfig> resistance;

        private:
            // latestNonEmptyAnchorsFlow.take(1): the anchors once there are any.
            rt::Task<Anchors> LatestNonEmptyAnchors() {
                while (anchors.Peek().empty()) co_await rt::NextFrame();
                co_return anchors.Peek();
            }

            rt::Task<> SnapInternalToOffset(float target) {
                co_await draggableState.Drag([this, target](const foundation::DragScope& scope) -> rt::Task<> {
                    scope.DragBy(target - m_AbsoluteOffset.Peek());
                    co_return;
                });
            }

            rt::Task<> AnimateInternalToOffset(float target, anim::SpecPtr spec) {
                co_await draggableState.Drag([this, target, spec](const foundation::DragScope& scope) -> rt::Task<> {
                    auto prevValue = std::make_shared<float>(m_AbsoluteOffset.Peek());
                    m_AnimationTarget.Set(target);
                    m_IsAnimationRunning.Set(true);
                    struct Finally {
                        CarouselSwipeableState* s;
                        ~Finally() {
                            s->m_AnimationTarget.Set(std::nullopt);
                            s->m_IsAnimationRunning.Set(false);
                        }
                    } finally{ this };
                    auto animatable = std::make_shared<anim::Animatable>(*prevValue);
                    co_await animatable->AnimateTo(anim::Vec{ target, 0, 0, 0 }, spec, std::nullopt, [scope, prevValue](const anim::Vec& v) {
                        scope.DragBy(v[0] - *prevValue);
                        *prevValue = v[0];
                    });
                });
            }

            rt::State<std::string> m_CurrentValue;
            rt::State<bool> m_IsAnimationRunning{ false };
            rt::State<float> m_Offset{ 0.0f };
            rt::State<float> m_Overflow{ 0.0f };
            rt::State<float> m_AbsoluteOffset{ 0.0f };
            rt::State<std::optional<float>> m_AnimationTarget{ std::nullopt };
            float m_MinBound = -std::numeric_limits<float>::infinity();
            float m_MaxBound = std::numeric_limits<float>::infinity();
        };

        // rememberCarouselSwipeableState: the current value saved.
        CarouselSwipeableState& RememberCarouselSwipeableState(const std::string& initialValue) {
            rt::Saver<std::shared_ptr<CarouselSwipeableState>> saver{
                [](const std::shared_ptr<CarouselSwipeableState>& s) { return rt::SavedValue(s->CurrentValue()); },
                [](const rt::SavedValue& v) -> std::optional<std::shared_ptr<CarouselSwipeableState>> {
                    if (const std::string* s = std::get_if<std::string>(&v.v)) return std::make_shared<CarouselSwipeableState>(*s);
                    return std::nullopt;
                } };
            return *rt::RememberSaveable<std::shared_ptr<CarouselSwipeableState>>(
                [&] { return std::make_shared<CarouselSwipeableState>(initialValue); }, saver);
        }

        // Modifier.carouselSwipeable(state, anchors, orientation, reverseDirection, thresholds:
        // FractionalThreshold(fraction)), with the default resistance and velocity threshold.
        Modifier CarouselSwipeable(Modifier modifier, CarouselSwipeableState& state, const Anchors& anchors, foundation::Orientation orientation,
                                   bool reverseDirection, float thresholdFraction) {
            auto* c = rt::Composition::Current();
            const ui::Density density = c->Read(ui::LocalDensity);
            state.EnsureInit(anchors);
            std::size_t anchorsHash = 0;
            for (const auto& [o, v] : anchors) anchorsHash = anchorsHash * 31 + std::hash<float>{}(o) * 7 + std::hash<std::string>{}(v);
            CarouselSwipeableState* s = &state;
            c->LaunchedEffect(rt::Keys(anchorsHash, s), [s, anchors, thresholdFraction, density]() -> rt::Task<> {
                Anchors oldAnchors = s->anchors.Peek();
                s->anchors.Set(anchors);
                s->resistance = DefaultResistance(anchors);
                // FractionalThreshold.computeThreshold: lerp(from, to, fraction).
                s->thresholds = [thresholdFraction](float a, float b) { return a + (b - a) * thresholdFraction; };
                s->velocityThreshold = density.ToPx(ui::Dp(125));   // SwipeableDefaults.VelocityThreshold
                co_await s->ProcessNewAnchors(std::move(oldAnchors), anchors);
            });
            std::shared_ptr<rt::CoroutineScope> scope = rt::RememberCoroutineScope();
            foundation::DraggableOptions options;
            options.reverseDirection = reverseDirection;
            options.startDragImmediately = state.IsAnimationRunning();
            options.onDragStopped = [scope, s](float velocity) { scope->Launch(s->PerformFling(velocity)); };
            return foundation::Draggable(std::move(modifier), state.draggableState, orientation, std::move(options));
        }

        enum class MotionCarouselDirection { FORWARD, BACKWARD };

        struct CarouselState {
            MotionCarouselDirection direction = MotionCarouselDirection::FORWARD;
            int index = 0;
            int targetIndex = 0;
            bool snapping = false;
            bool animating = false;
        };

        class MotionCarouselScopeImpl final : public MotionCarouselScope {
        public:
            void Items(int count, kiln::Function<void(int)> itemContent) override {
                itemsCount = count;
                itemsProvider = std::make_shared<kiln::Function<void(int)>>(std::move(itemContent));
            }
            void ItemsWithProperties(int count, kiln::Function<void(int, const MotionLayoutScope::MotionProperties&)> itemContent) override {
                itemsCount = count;
                itemsProviderWithProperties = std::make_shared<kiln::Function<void(int, const MotionLayoutScope::MotionProperties&)>>(std::move(itemContent));
            }
            int itemsCount = 0;
            std::shared_ptr<kiln::Function<void(int)>> itemsProvider;
            std::shared_ptr<kiln::Function<void(int, const MotionLayoutScope::MotionProperties&)>> itemsProviderWithProperties;
        };

    }

    void ItemHolder(int i, const std::string& slotPrefix, bool showSlot, kiln::Function<void()> function) {
        Modifier modifier = foundation::LayoutId({}, slotPrefix + std::to_string(i));
        if (showSlot) {
            modifier = foundation::Border(foundation::Clip(std::move(modifier), foundation::CornerShape::Rounded(ui::Dp(20))), ui::Dp(2),
                                          ui::Color(0x3C000000u),   // Color(0, 0, 0, 60)
                                          foundation::RoundedCorner(ui::Dp(20)));
        }
        foundation::Box(modifier, foundation::Alignment::Center(), false, std::move(function));
    }

    void MotionCarousel(std::shared_ptr<MotionScene> motionScene, int initialSlotIndex, int numSlots, kiln::Function<void(MotionCarouselScope&)> content,
                        const MotionCarouselOptions& options, rt::Key key) {
        const std::string swipeStateStart = "start";
        const std::string swipeStateForward = "next";
        const std::string swipeStateBackward = "previous";
        auto* c = rt::Composition::Current();
        // rememberStateOfItemsProvider: the latest content's items.
        auto provider = std::make_shared<MotionCarouselScopeImpl>();
        content(*provider);
        struct Holder {
            rt::State<float> componentWidth{ 1000.0f };
            CarouselState state;
            rt::State<int> currentIndex{ 0 };
            rt::State<std::string> transitionName;
        };
        auto& holderRef = c->Remember<std::shared_ptr<Holder>>([&] {
            auto h = std::make_shared<Holder>();
            h->transitionName.Set(options.forwardTransition);
            return h;
        }, {}, key);
        std::shared_ptr<Holder> h = holderRef;
        CarouselSwipeableState& swipeableState = RememberCarouselSwipeableState(swipeStateStart);
        const float componentWidth = h->componentWidth.Get();
        float mprogress = swipeableState.Offset() / componentWidth;
        const int count = provider->itemsCount;
        Anchors anchors;
        if (h->currentIndex.Get() == 0) anchors = { { 0.0f, swipeStateStart }, { componentWidth, swipeStateForward } };
        else if (h->currentIndex.Get() == count - 1) anchors = { { -componentWidth, swipeStateBackward }, { 0.0f, swipeStateStart } };
        else anchors = { { -componentWidth, swipeStateBackward }, { 0.0f, swipeStateStart }, { componentWidth, swipeStateForward } };
        CarouselState& state = h->state;
        if (mprogress < 0 && state.index > 0) {
            state.direction = MotionCarouselDirection::BACKWARD;
            h->transitionName.Set(options.backwardTransition);
            mprogress *= -1;
        } else {
            state.direction = MotionCarouselDirection::FORWARD;
            h->transitionName.Set(options.forwardTransition);
        }
        if (!swipeableState.IsAnimationRunning()) {
            CarouselSwipeableState* swipeable = &swipeableState;
            if (state.direction == MotionCarouselDirection::FORWARD && swipeableState.CurrentValue() == swipeStateForward) {
                c->LaunchedEffect(rt::Keys(true), [h, swipeable, count, swipeStateStart]() -> rt::Task<> {
                    if (h->state.index + 1 < count) {
                        h->state.index++;
                        co_await swipeable->SnapTo(swipeStateStart);
                        h->state.direction = MotionCarouselDirection::FORWARD;
                    }
                });
            } else if (state.direction == MotionCarouselDirection::BACKWARD && swipeableState.CurrentValue() == swipeStateBackward) {
                c->LaunchedEffect(rt::Keys(true), [h, swipeable, swipeStateStart]() -> rt::Task<> {
                    if (h->state.index > 0) h->state.index--;
                    co_await swipeable->SnapTo(swipeStateStart);
                    h->state.direction = MotionCarouselDirection::FORWARD;
                });
            }
            h->currentIndex.Set(state.index);
        }
        Modifier modifier = foundation::Background(foundation::FillMax({}, foundation::Axis::Both), ui::Color::White());
        modifier = CarouselSwipeable(std::move(modifier), swipeableState, anchors, foundation::Orientation::Horizontal, true, 0.3f);
        modifier = foundation::OnSizeChanged(std::move(modifier), [h](ui::IntSize size) { h->componentWidth.Set(float(size.width)); });
        const int currentIndex = h->currentIndex.Peek();
        const std::string slotPrefix = options.slotPrefix;
        const bool showSlots = options.showSlots;
        MotionLayout(
            motionScene, mprogress, modifier,
            [provider, numSlots, currentIndex, initialSlotIndex, slotPrefix, showSlots](MotionLayoutScope& scope) {
                MotionLayoutScope* sp = &scope;
                for (int i = 0; i < numSlots; i++) {
                    const int idx = i + currentIndex - initialSlotIndex;
                    const bool visible = idx >= 0 && idx < provider->itemsCount;
                    ItemHolder(i, slotPrefix, showSlots, [sp, provider, idx, visible, slotPrefix, i] {
                        if (!visible) return;
                        if (provider->itemsProviderWithProperties) {
                            const MotionLayoutScope::MotionProperties properties = sp->GetMotionProperties(slotPrefix + std::to_string(i));
                            (*provider->itemsProviderWithProperties)(idx, properties);
                        } else if (provider->itemsProvider) {
                            (*provider->itemsProvider)(idx);
                        }
                    });
                }
            },
            h->transitionName.Get(), MotionLayoutOptions{}, key);
    }

}
