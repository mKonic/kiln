#include "kiln/material3/adaptive/PaneExpansion.h"

#include "kiln/animation/Animatable.h"
#include "kiln/animation/Decay.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/CoroutineScope.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <map>

namespace kiln::material3::adaptive {

    using ui::Density;
    using ui::Dp;
    using ui::LayoutDirection;

    // --- Anchors --------------------------------------------------------------------------------

    int PaneExpansionAnchor::PositionIn(int total, const Density& density, std::optional<LayoutDirection> direction) const {
        int offset = 0;
        switch (type) {
            case Type::Proportion: offset = std::clamp(int(std::lround(float(total) * proportion)), 0, total); break;
            case Type::OffsetFromStart: offset = density.RoundToPx(this->offset); break;
            case Type::OffsetFromEnd: offset = total - density.RoundToPx(this->offset); break;
            case Type::Unspecified: break;
        }
        return direction == LayoutDirection::Rtl ? total - offset : offset;
    }

    std::string PaneExpansionAnchor::Description(const std::vector<std::string>& locales) const {
        switch (type) {
            case Type::Proportion: return GetString(Strings::AdaptiveDefaultPaneExpansionProportionAnchorDescription, locales, { int(proportion * 100) });
            case Type::OffsetFromStart: return GetString(Strings::AdaptiveDefaultPaneExpansionStartOffsetAnchorDescription, locales, { int(offset.value) });
            case Type::OffsetFromEnd: return GetString(Strings::AdaptiveDefaultPaneExpansionEndOffsetAnchorDescription, locales, { int(offset.value) });
            case Type::Unspecified: break;
        }
        return {};
    }

    // --- PaneExpansionState ---------------------------------------------------------------------

    anim::SpecPtr PaneExpansionState::DefaultAnchoringAnimationSpec() {
        static const anim::SpecPtr spec = anim::Spring(0.8f, 380, 1);
        return spec;
    }

    PaneExpansionState::PaneExpansionState(std::shared_ptr<Data> data, std::function<float(float)> consumeDragDelta)
        : m_Data(std::move(data)), m_ConsumeDragDelta(std::move(consumeDragDelta)),
          m_Draggable([this](float delta) {
              // draggableState.dispatchRawDelta: what the consumer leaves moves the handle.
              const float remaining = m_ConsumeDragDelta ? m_ConsumeDragDelta(delta) : delta;
              if (m_CurrentMeasuredDraggingOffset == Unspecified) return;
              SetCurrentDraggingOffset(int(float(m_CurrentMeasuredDraggingOffset) + remaining));
          }) {}

    int PaneExpansionState::FirstPaneWidth() const {
        const int max = m_MaxExpansionWidth.Get();
        const int width = m_Data.Get()->firstPaneWidth.Get();
        if (max == Unspecified || width == Unspecified) return Unspecified;
        return std::clamp(width, 0, max);
    }

    bool PaneExpansionState::IsUnspecified() const {
        return FirstPaneWidth() == Unspecified && std::isnan(FirstPaneProportion()) && CurrentDraggingOffset() == Unspecified;
    }

    void PaneExpansionState::SetCurrentDraggingOffset(int value) {
        const int coerced = std::clamp(value, 0, std::max(0, m_MaxExpansionWidth.Peek()));
        Data& d = *m_Data.Peek();
        if (coerced == d.currentDraggingOffset.Peek()) return;
        d.currentDraggingOffset.Set(coerced);
        m_CurrentMeasuredDraggingOffset = coerced;
    }

    void PaneExpansionState::SetFirstPaneWidth(int width) {
        Data& d = *m_Data.Peek();
        d.firstPaneProportion.Set(NAN);
        d.currentDraggingOffset.Set(Unspecified);
        d.firstPaneWidth.Set(width);
        SetCurrentAnchor(std::nullopt);
    }

    void PaneExpansionState::SetFirstPaneProportion(float proportion) {
        Data& d = *m_Data.Peek();
        d.firstPaneWidth.Set(Unspecified);
        d.currentDraggingOffset.Set(Unspecified);
        d.firstPaneProportion.Set(std::clamp(proportion, 0.0f, 1.0f));
        SetCurrentAnchor(std::nullopt);
    }

    void PaneExpansionState::Clear() {
        Data& d = *m_Data.Peek();
        d.firstPaneWidth.Set(Unspecified);
        d.firstPaneProportion.Set(NAN);
        d.currentDraggingOffset.Set(Unspecified);
    }

    std::optional<PaneExpansionAnchor> PaneExpansionState::NextAnchor() const {
        const auto& anchors = m_Anchors.Get();
        if (m_MaxExpansionWidth.Get() == Unspecified || anchors.empty()) return std::nullopt;
        const int dragging = CurrentDraggingOffset();
        const int current = dragging == Unspecified ? m_CurrentMeasuredDraggingOffset : dragging;
        for (const auto& p : m_AnchorPositions)
            if (current < p.position) return anchors[std::size_t(p.index)];
        return anchors[0];
    }

    void PaneExpansionState::UpdateAnchorPositions() {
        m_AnchorPositions.clear();
        if (!m_MeasuredDensity) return;
        const auto& anchors = m_Anchors.Peek();
        for (std::size_t i = 0; i < anchors.size(); ++i)
            m_AnchorPositions.push_back({ anchors[i].PositionIn(m_MaxExpansionWidth.Peek(), *m_MeasuredDensity, m_MeasuredDirection), int(i) });
        std::sort(m_AnchorPositions.begin(), m_AnchorPositions.end(),
                  [](const IndexedAnchorPosition& a, const IndexedAnchorPosition& b) { return a.position != b.position ? a.position < b.position : a.index < b.index; });
    }

    void PaneExpansionState::Restore(std::shared_ptr<Data> data, std::vector<PaneExpansionAnchor> anchors, anim::SpecPtr spec,
                                     foundation::FlingBehavior fling, std::optional<PaneExpansionAnchor> initialAnchorForCurrentAnchors) {
        ++m_Mutation;   // dragMutex.mutate(PreventUserInput): stops a settle
        m_Settling.Set(false);
        m_Data.Set(std::move(data));
        m_Anchors.Set(std::move(anchors));
        UpdateAnchorPositions();
        const auto current = m_Data.Peek()->currentAnchor.Peek();
        const auto& list = m_Anchors.Peek();
        if (!current || std::find(list.begin(), list.end(), *current) == list.end()) SetCurrentAnchor(initialAnchorForCurrentAnchors);
        m_AnchoringSpec = spec ? std::move(spec) : DefaultAnchoringAnimationSpec();
        m_Fling = std::move(fling);
    }

    void PaneExpansionState::OnMeasured(int width, const Density& density, LayoutDirection direction) {
        if (width == m_MaxExpansionWidth.Peek() && m_MeasuredDensity == density && m_MeasuredDirection == direction) return;
        m_MaxExpansionWidth.Set(width);
        m_MeasuredDensity = density;
        m_MeasuredDirection = direction;
        rt::Observation::Untracked([&] {
            UpdateAnchorPositions();
            const auto anchor = m_Data.Peek()->currentAnchor.Peek();
            if (!IsDraggingOrSettling() && anchor) SetCurrentDraggingOffset(anchor->PositionIn(width, density, direction));
            else if (const int offset = m_Data.Peek()->currentDraggingOffset.Peek(); offset != Unspecified) SetCurrentDraggingOffset(offset);   // re-coerce
        });
    }

    void PaneExpansionState::SnapToAnchor(const PaneExpansionAnchor& anchor) {
        rt::Observation::Untracked([&] {
            if (m_MeasuredDensity) SetCurrentDraggingOffset(anchor.PositionIn(m_MaxExpansionWidth.Peek(), *m_MeasuredDensity, m_MeasuredDirection));
        });
    }

    float PaneExpansionState::ScrollBy(float pixels) {
        // dragScope.scrollBy (for the fling): what moved the handle.
        const int before = m_Data.Peek()->currentDraggingOffset.Peek();
        m_Draggable.DispatchRawDelta(pixels);
        return float(m_Data.Peek()->currentDraggingOffset.Peek() - before);
    }

    rt::Task<> PaneExpansionState::AnimateToInternal(int offset, float initialVelocity, std::uint64_t token) {
        m_Settling.Set(true);
        anim::Animatable a{ float(m_CurrentMeasuredDraggingOffset) };
        co_await a.AnimateTo(anim::Vec{ float(offset), 0, 0, 0 }, m_AnchoringSpec, anim::Vec{ initialVelocity, 0, 0, 0 }, [this, token](const anim::Vec& v) {
            if (m_Mutation == token) SetCurrentDraggingOffset(int(v[0]));
        });
        if (m_Mutation != token) co_return;
        SetCurrentDraggingOffset(offset);
        m_Settling.Set(false);
    }

    rt::Task<> PaneExpansionState::AnimateTo(PaneExpansionAnchor anchor, float initialVelocity) {
        const auto& anchors = m_Anchors.Peek();
        if (std::find(anchors.begin(), anchors.end(), anchor) == anchors.end()) co_return;   // not one of the anchors
        SetCurrentAnchor(anchor);
        if (!m_MeasuredDensity) co_return;
        const std::uint64_t token = ++m_Mutation;
        co_await AnimateToInternal(anchor.PositionIn(m_MaxExpansionWidth.Peek(), *m_MeasuredDensity, m_MeasuredDirection), initialVelocity, token);
    }

    namespace {
        constexpr float AnchoringVelocityThreshold = 200;

        // DefaultFlingBehavior with the platform spline decay.
        rt::Task<float> DecayFling(std::function<float(float)> scrollBy, float velocity, float density) {
            if (std::abs(velocity) <= 1) co_return velocity;
            anim::Animatable a(0.0f);
            float last = 0, velocityLeft = velocity;
            co_await a.AnimateDecay(velocity, std::make_shared<anim::SplineDecay>(density), [&](float value) {
                const float delta = value - last;
                last = value;
                const float consumed = scrollBy(delta);
                velocityLeft = a.Velocity()[0];
                if (std::abs(delta - consumed) > 0.5f) a.Stop();
            });
            co_return velocityLeft;
        }
    }

    rt::Task<> PaneExpansionState::SettleToAnchorIfNeeded(float velocity) {
        if (m_AnchorPositions.empty()) co_return;
        const std::uint64_t token = ++m_Mutation;
        m_Settling.Set(true);
        std::function<float(float)> scrollBy = [this](float d) { return ScrollBy(d); };
        const float density = m_MeasuredDensity ? m_MeasuredDensity->density : 1.0f;
        const float left = m_Fling ? co_await m_Fling(scrollBy, velocity) : co_await DecayFling(scrollBy, velocity, density);
        if (m_Mutation != token) co_return;
        // getPositionOfTheClosestAnchor: in the fling's direction when it is fast enough.
        const int current = m_CurrentMeasuredDraggingOffset;
        const int max = m_MaxExpansionWidth.Peek();
        auto cost = [&](int position) {
            if (left >= AnchoringVelocityThreshold) { const int d = position - current; return d < 0 ? max - d : d; }
            if (left <= -AnchoringVelocityThreshold) { const int d = current - position; return d < 0 ? max - d : d; }
            return std::abs(current - position);
        };
        IndexedAnchorPosition best = m_AnchorPositions[0];
        int bestCost = cost(best.position);
        for (std::size_t i = 1; i < m_AnchorPositions.size(); ++i)
            if (const int c = cost(m_AnchorPositions[i].position); bestCost > c) { best = m_AnchorPositions[i]; bestCost = c; }
        SetCurrentAnchor(m_Anchors.Peek()[std::size_t(best.index)]);
        co_await AnimateToInternal(best.position, left, token);
        if (m_Mutation == token) m_Settling.Set(false);
    }

    // --- remember -------------------------------------------------------------------------------

    namespace {
        using Key = PaneExpansionStateKey;
        struct KeyLess {
            bool operator()(const Key& a, const Key& b) const {
                auto f = [](const Key& k) { return k.expandedPanes ? std::tuple(1, int(k.expandedPanes->first), int(k.expandedPanes->second)) : std::tuple(0, 0, 0); };
                return f(a) < f(b);
            }
        };
        using DataMap = std::map<Key, std::shared_ptr<PaneExpansionState::Data>, KeyLess>;

        // PaneExpansionStateKeySaver / DataSaver, as one list per entry.
        rt::Saver<std::shared_ptr<DataMap>> MapSaver() {
            return {
                [](const std::shared_ptr<DataMap>& map) {
                    rt::SavedValue::List entries;
                    for (const auto& [key, d] : *map) {
                        const auto anchor = d->currentAnchor.Peek();
                        entries.push_back(rt::SavedValue::List{
                            key.expandedPanes ? 1 : 0, key.expandedPanes ? int(key.expandedPanes->first) : 0, key.expandedPanes ? int(key.expandedPanes->second) : 0,
                            d->firstPaneWidth.Peek(), double(d->firstPaneProportion.Peek()), d->currentDraggingOffset.Peek(),
                            anchor ? int(anchor->type) : 0, anchor ? double(anchor->type == PaneExpansionAnchor::Type::Proportion ? anchor->proportion : anchor->offset.value) : 0.0 });
                    }
                    return rt::SavedValue(std::move(entries));
                },
                [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<DataMap>> {
                    const auto* list = std::get_if<rt::SavedValue::List>(&saved.v);
                    if (!list) return std::nullopt;
                    auto map = std::make_shared<DataMap>();
                    auto i64 = [](const rt::SavedValue& v) { const auto* p = std::get_if<std::int64_t>(&v.v); return p ? *p : 0; };
                    auto f64 = [](const rt::SavedValue& v) { const auto* p = std::get_if<double>(&v.v); return p ? *p : 0.0; };
                    for (const auto& entry : *list) {
                        const auto* e = std::get_if<rt::SavedValue::List>(&entry.v);
                        if (!e || e->size() != 8) continue;
                        Key key;
                        if (i64((*e)[0])) key.expandedPanes = std::pair{ ThreePaneScaffoldRole(i64((*e)[1])), ThreePaneScaffoldRole(i64((*e)[2])) };
                        auto d = std::make_shared<PaneExpansionState::Data>();
                        d->firstPaneWidth.Set(int(i64((*e)[3])));
                        d->firstPaneProportion.Set(float(f64((*e)[4])));
                        d->currentDraggingOffset.Set(int(i64((*e)[5])));
                        const auto type = PaneExpansionAnchor::Type(i64((*e)[6]));
                        const float value = float(f64((*e)[7]));
                        if (type == PaneExpansionAnchor::Type::Proportion) d->currentAnchor.Set(PaneExpansionAnchor::Proportion(value));
                        else if (type == PaneExpansionAnchor::Type::OffsetFromStart) d->currentAnchor.Set(PaneExpansionAnchor::OffsetFromStart(Dp(value)));
                        else if (type == PaneExpansionAnchor::Type::OffsetFromEnd) d->currentAnchor.Set(PaneExpansionAnchor::OffsetFromEnd(Dp(value)));
                        (*map)[key] = d;
                    }
                    return map;
                },
            };
        }

        std::size_t AnchorsHash(const std::vector<PaneExpansionAnchor>& anchors) {
            std::size_t h = anchors.size();
            for (const auto& a : anchors)
                h = h * 31 + std::hash<int>{}(int(a.type)) * 7 + std::hash<float>{}(a.type == PaneExpansionAnchor::Type::Proportion ? a.proportion : a.offset.value);
            return h;
        }
    }

    PaneExpansionState& RememberPaneExpansionState(PaneExpansionStateKey key, const PaneExpansionStateOptions& options, rt::Key callKey) {
        auto& c = *rt::Composition::Current();
        const std::size_t anchorsHash = AnchorsHash(options.anchors);
        // The initial anchor, and the one for the current anchors (when the current anchor is no
        // longer among them).
        const std::optional<PaneExpansionAnchor> initialAnchor =
            options.initialAnchoredIndex >= 0 && std::size_t(options.initialAnchoredIndex) < options.anchors.size()
                ? std::optional(options.anchors[std::size_t(options.initialAnchoredIndex)]) : std::nullopt;
        auto& initialForCurrent = c.Remember<std::optional<PaneExpansionAnchor>>([&] { return initialAnchor; }, rt::Keys(anchorsHash), callKey);
        // rememberPersistentlyWithKey: a saved map from key to data.
        auto& map = rt::RememberSaveable<std::shared_ptr<DataMap>>([] { return std::make_shared<DataMap>(); }, MapSaver(), {}, callKey);
        auto& slot = (*map)[key];
        if (!slot) {
            slot = std::make_shared<PaneExpansionState::Data>();
            slot->currentAnchor.Set(initialAnchor);
        }
        auto data = slot;
        // rememberUpdatedRef(consumeDragDelta)
        auto& consumeRef = c.Remember<std::shared_ptr<std::function<float(float)>>>([] { return std::make_shared<std::function<float(float)>>(); }, {}, callKey);
        *consumeRef = options.consumeDragDelta;
        auto consume = consumeRef;
        auto& state = c.RememberNew<PaneExpansionState>({}, callKey, data, [consume](float delta) { return *consume ? (*consume)(delta) : delta; });
        PaneExpansionState* sp = &state;
        const auto anchors = options.anchors;
        const auto spec = options.anchoringAnimationSpec;
        const auto fling = options.flingBehavior;
        const auto initial = initialForCurrent;
        const int keyHash = key.expandedPanes ? 1 + int(key.expandedPanes->first) * 3 + int(key.expandedPanes->second) : 0;
        c.LaunchedEffect(rt::Keys(keyHash, anchorsHash, reinterpret_cast<std::uintptr_t>(spec.get())), [sp, data, anchors, spec, fling, initial]() -> rt::Task<> {
            sp->Restore(data, anchors, spec, fling, initial);
            co_return;
        }, callKey);
        return state;
    }

    PaneExpansionState& RememberDefaultPaneExpansionState(std::function<PaneExpansionStateKey()> keyProvider, bool mutableState, rt::Key callKey) {
        auto& c = *rt::Composition::Current();
        PaneExpansionState* result = nullptr;
        // The two branches are separate groups (Compose's if / else).
        c.Child([&] {
            if (mutableState) result = &RememberPaneExpansionState(keyProvider());
            else result = &c.RememberNew<PaneExpansionState>({}, rt::Key{});   // a stub
        }, rt::Key(mutableState ? 1 : 2));
        (void)callKey;
        return *result;
    }

    // --- DragToResizeState ----------------------------------------------------------------------

    void DragToResizeState::SetSize(float value) {
        const float start = RangeStart(), end = RangeEnd();
        if (start > end || (start == 0 && end == 0)) { m_Size.Set(value); return; }   // sizeRange.isEmpty()
        if (value <= start) { m_State.Set(State::Collapsed); m_Size.Set(start); }
        else if (value >= end) { m_State.Set(State::Expanded); m_Size.Set(end); }
        else m_Size.Set(value);
    }

    void DragToResizeState::DispatchRawDelta(float delta) {
        if (std::isnan(m_Size.Peek())) return;
        m_State.Set(State::Dragged);
        SetSize(m_Size.Peek() + ConvertDelta(delta));
        m_LastDraggedSize = m_Size.Peek();
    }

    float DragToResizeState::TargetSize(State s) const {
        switch (s) {
            case State::Expanded: return RangeEnd();
            case State::Collapsed: return RangeStart();
            case State::Default: return std::isnan(m_LastDraggedSize) ? m_LastMeasuredSize : m_LastDraggedSize;
            case State::Dragged: break;
        }
        return m_Size.Peek();
    }

    namespace {
        rt::Task<> AnimateSize(float from, float to, std::function<void(float)> set) {
            anim::Animatable a(from);
            co_await a.AnimateTo(anim::Vec{ to, 0, 0, 0 }, anim::Spring(), std::nullopt, [set](const anim::Vec& v) { set(v[0]); });
        }
    }

    void DragToResizeState::AnimateTo(State target) {
        if (m_State.Peek() == target) return;
        const float to = TargetSize(target);
        if (m_State.Peek() == State::Default) {
            SetSize(to);
        } else {
            m_Animation = rt::Launch(rt::Dispatcher::Main(), AnimateSize(m_Size.Peek(), to, [this](float v) { SetSize(v); }));
        }
        m_State.Set(target);
    }

    DragToResizeState::State DragToResizeState::NextState() const {
        switch (m_State.Get()) {
            case State::Expanded: return State::Collapsed;
            case State::Collapsed: return State::Default;
            default: return State::Expanded;
        }
    }

    int DragToResizeState::GetAndUpdateDraggedSize(int measuringSize) {
        m_LastMeasuredSize = float(measuringSize);
        SetSize(std::isnan(m_Size.Peek()) ? TargetSize(m_State.Peek()) : m_Size.Peek());   // set again to re-coerce
        return int(m_Size.Peek());
    }

    int DragToResizeState::GetDraggedWidth(int measuringWidth, int defaultMinWidth, int scaffoldWidth) {
        if (!IsHorizontal()) return measuringWidth;
        const int minWidth = minSize == INT_MIN ? defaultMinWidth : minSize;
        m_WidthRange = { float(minWidth), float(std::min(maxSize, scaffoldWidth)) };
        return GetAndUpdateDraggedSize(measuringWidth);
    }

    int DragToResizeState::GetDraggedHeight(int measuringHeight, int defaultMinHeight, int scaffoldHeight) {
        if (IsHorizontal()) return measuringHeight;
        const int minHeight = minSize == INT_MIN ? defaultMinHeight : minSize;
        m_HeightRange = { float(minHeight), float(std::min(maxSize, scaffoldHeight)) };
        return GetAndUpdateDraggedSize(measuringHeight);
    }

    std::shared_ptr<DragToResizeState> RememberDragToResizeState(DockedEdge dockedEdge, std::optional<Dp> minSize, std::optional<Dp> maxSize, rt::Key key) {
        auto& c = *rt::Composition::Current();
        const LayoutDirection dir = c.Read(ui::LocalLayoutDirection);
        const Density density = c.Read(ui::LocalDensity);
        using K = DragToResizeState::Kind;
        const K kind = dockedEdge == DockedEdge::Top ? K::Top : dockedEdge == DockedEdge::Bottom ? K::Bottom
                     : (dockedEdge == DockedEdge::Start) == (dir == LayoutDirection::Ltr) ? K::Left : K::Right;
        auto& state = c.Remember<std::shared_ptr<DragToResizeState>>([&] { return std::make_shared<DragToResizeState>(kind); }, rt::Keys(int(dockedEdge)), key);
        state->minSize = minSize ? density.RoundToPx(*minSize) : INT_MIN;
        state->maxSize = maxSize ? density.RoundToPx(*maxSize) : INT_MAX;
        return state;
    }

    ui::Modifier DragToResize(ui::Modifier m, std::shared_ptr<DragToResizeState> state, bool showIndication) {
        m = foundation::Draggable(std::move(m), state->DraggableState(), state->Orientation());
        // clickToResize
        foundation::ClickableOptions click;
        if (!showIndication) { click.indication = nullptr; click.useLocalIndication = false; }
        m = foundation::Clickable(std::move(m), [state] { state->AnimateTo(state->NextState()); }, click);
        // delegableSemantics (a CompositionLocalConsumerModifierNode): the strings in the
        // composition's locales.
        return foundation::Semantics(std::move(m), [state, locales = CurrentLocales()](ui::SemanticsConfiguration& s) {
            using St = DragToResizeState::State;
            const St next = state->NextState();
            s.onClickLabel = GetString(next == St::Expanded ? Strings::AdaptiveDragToResizeClickToExpandDescription
                                     : next == St::Collapsed ? Strings::AdaptiveDragToResizeClickToCollapseDescription
                                                             : Strings::AdaptiveDragToResizeClickToPartiallyExpandDescription, locales);
            s.onClick = [state, next] { state->AnimateTo(next); return true; };
            const St now = state->CurrentState();
            s.stateDescription = GetString(now == St::Expanded ? Strings::AdaptiveDragToResizeExpandedStateDescription
                                         : now == St::Collapsed ? Strings::AdaptiveDragToResizeCollapsedStateDescription
                                                                : Strings::AdaptiveDragToResizePartiallyExpandedStateDescription, locales);
        });
    }

}
