#include "kiln/foundation/SharedTransitionInternal.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    using namespace detail;
    using ui::ContentDrawScope;
    using ui::LayoutCoordinates;

    namespace {
        Rect MakeRect(Offset tl, Size s) { return { tl.x, tl.y, tl.x + s.width, tl.y + s.height }; }
        Offset TopLeft(const Rect& r) { return { r.left, r.top }; }
        Size SizeOf(const Rect& r) { return { r.Width(), r.Height() }; }
        Rect Translate(const Rect& r, Offset d) { return { r.left + d.x, r.top + d.y, r.right + d.x, r.bottom + d.y }; }
        anim::Vec ToVec(const Rect& r) { return { r.left, r.top, r.right, r.bottom }; }
        Rect FromVec(const anim::Vec& v) { return { v[0], v[1], v[2], v[3] }; }
        Size SizeF(IntSize s) { return { float(s.width), float(s.height) }; }
        // calculatePivot: the transform origin over the container's bounds in the scope.
        Offset CalculatePivot(const LayoutCoordinates& parent, const LayoutCoordinates& root, TransformOrigin origin) {
            const Rect b = root.LocalBoundingBoxOf(parent);
            return { b.left + b.Width() * origin.pivotFractionX, b.top + b.Height() * origin.pivotFractionY };
        }
        // Offset.transform(pivot, scale, offset).
        Offset TransformPoint(Offset p, Offset pivot, float scale, IntOffset offset) {
            return { (p.x - pivot.x) * scale + pivot.x + float(offset.x), (p.y - pivot.y) * scale + pivot.y + float(offset.y) };
        }
        Rect Lerp(const Rect& a, const Rect& b, float t) {
            return { a.left + (b.left - a.left) * t, a.top + (b.top - a.top) * t, a.right + (b.right - a.right) * t, a.bottom + (b.bottom - a.bottom) * t };
        }
        // spring(StiffnessMediumLow, visibilityThreshold = Rect.VisibilityThreshold).
        anim::SpecPtr DefaultBoundsSpring() {
            static const anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 1.0f);
            return spec;
        }
        // DefaultMomentumSpring: spring(StiffnessMediumLow, Offset(3, 3)).
        anim::SpecPtr DefaultMomentumSpring() {
            static const anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 3.0f);
            return spec;
        }
    }

    BoundsTransform SharedTransitionDefaults::BoundsTransform() {
        return [](const Rect&, const Rect&) { return DefaultBoundsSpring(); };
    }

    // --- SharedContentState ---------------------------------------------------------------------

    bool SharedContentState::IsEnabledByUser() const {
        const SharedContentConfig& c = Config();
        const bool enabled = !c.isEnabled || c.isEnabled(*this);
        SharedElementEntry* e = m_Internal.Get();
        const bool animating = e && e->boundsAnimation->HasAnimationState();
        return enabled || (animating && c.shouldKeepEnabledForOngoingAnimation);
    }
    bool SharedContentState::IsMatchFound() const {
        SharedElementEntry* e = m_Internal.Get();
        return e && e->sharedElement->FoundMatch();
    }
    void SharedContentState::PrepareTransitionWithInitialVelocity(ui::Offset velocity) {
        SharedElementEntry* e = m_Internal.Get();
        if (!e) throw std::logic_error("SharedContentState has not been added to a sharedElement/sharedBounds modifier yet");
        const SharedContentConfig& c = Config();
        if (c.isEnabled && !c.isEnabled(*this)) return;
        e->sharedElement->UpdateExitVelocity(velocity);
    }
    std::optional<gfx::Path> SharedContentState::ClipPathInOverlay() const {
        SharedElementEntry* e = m_Internal.Get();
        if (!e) throw std::logic_error("SharedContentState has not been added to a sharedElement/sharedBounds modifier yet");
        return e->clipPathInOverlay;
    }
    SharedContentState* SharedContentState::ParentSharedContentState() const {
        SharedElementEntry* e = m_Internal.Get();
        if (!e) throw std::logic_error("SharedContentState has not been added to a sharedElement/sharedBounds modifier yet");
        return e->ParentState() ? e->ParentState()->userState : nullptr;
    }

    // --- the state machine ------------------------------------------------------------------------

    namespace detail {

        std::shared_ptr<MatchState> MatchState::ConfigureActiveMatch(SharedElement&, BoundsProvider&, Size, Offset, Offset) {
            throw std::logic_error("configureActiveMatch is only valid while an active match is found");
        }

        namespace {
            void UpdateTargetData(TargetData& t, Size lookaheadSize, Offset topLeft, Offset structural, bool providerChanged) {
                if (!(t.targetStructuralOffset.Peek() == structural) || !(t.size.Peek() == lookaheadSize) || providerChanged) {
                    t.size.Set(lookaheadSize);
                    t.targetStructuralOffset.Set(structural);
                    if (providerChanged)
                        t.initialMfrOffset.Set((topLeft - structural) - (t.currentMfrOffset.Peek() - t.initialMfrOffset.Peek()));
                }
                t.currentMfrOffset.Set(topLeft - structural);
            }
            std::optional<Rect> BoundsFromLastTarget(SharedElement& e, BoundsProvider* last) {
                if (!last) return std::nullopt;
                for (SharedElementEntry* x : e.AllEntries())
                    if (x->boundsProvider == last) return last->LastBoundsInSharedTransitionScope();
                return std::nullopt;
            }
            BoundsProvider* FirstEnabledProvider(SharedElement& e) {
                for (SharedElementEntry* x : e.AllEntries())
                    if (std::find(e.EnabledEntries().begin(), e.EnabledEntries().end(), x) != e.EnabledEntries().end()) return x->boundsProvider;
                return nullptr;
            }

            class NoMatch;
            class ConfigPending;
            class Configured;
            class RemovedDuringTransition;

            class NoMatch final : public MatchState {
            public:
                std::shared_ptr<MatchState> OnMatchFound(BoundsProvider* previous) override;
                std::shared_ptr<MatchState> OnVisibleContentRemovedDuringTransition() override { return NoMatchFoundState(); }
            };

            class ConfigPending final : public MatchState, public std::enable_shared_from_this<ConfigPending> {
            public:
                ConfigPending(BoundsProvider* before, std::shared_ptr<TargetData> target = nullptr, std::optional<Rect> current = std::nullopt)
                    : m_Before(before), m_Target(std::move(target)), m_Current(current) {}
                TargetData* Target() const override { return m_Target.get(); }
                bool ActiveMatchFound() const override { return true; }
                std::optional<Rect> CurrentBounds() const override { return m_Current.Get(); }
                std::optional<Rect> InitializeCurrentBounds(SharedElement& e) override {
                    if (auto b = m_Current.Peek()) return b;
                    BoundsProvider* last = m_Before ? m_Before : FirstEnabledProvider(e);
                    if (auto b = BoundsFromLastTarget(e, last)) m_Current.Set(b);
                    return m_Current.Peek();
                }
                std::shared_ptr<MatchState> ConfigureActiveMatch(SharedElement& e, BoundsProvider& provider, Size lookaheadSize, Offset topLeft,
                                                                 Offset structural) override;
                std::shared_ptr<MatchState> OnMatchFound(BoundsProvider* previous) override {
                    if (!m_Before) m_Before = previous;
                    return shared_from_this();
                }
                void UpdateBounds(const Rect& b) override { m_Current.Set(b); }
                std::shared_ptr<MatchState> OnVisibleContentRemovedDuringTransition() override { return NoMatchFoundState(); }
            private:
                BoundsProvider* m_Before;
                std::shared_ptr<TargetData> m_Target;
                rt::State<std::optional<Rect>> m_Current;
            };

            class Configured final : public MatchState, public std::enable_shared_from_this<Configured> {
            public:
                Configured(std::shared_ptr<TargetData> target, BoundsProvider* provider, Rect current)
                    : m_Target(std::move(target)), m_Provider(provider), m_Current(current) {}
                TargetData* Target() const override { return m_Target.get(); }
                std::optional<Rect> CurrentBounds() const override { return m_Current.Get(); }
                bool ActiveMatchFound() const override { return true; }
                bool MatchIsOrHasBeenConfigured() const override { return true; }
                std::shared_ptr<MatchState> OnMatchFound(BoundsProvider*) override { return shared_from_this(); }
                std::shared_ptr<MatchState> ConfigureActiveMatch(SharedElement&, BoundsProvider& provider, Size lookaheadSize, Offset topLeft,
                                                                 Offset structural) override {
                    const bool changed = m_Provider.Peek() != &provider;
                    UpdateTargetData(*m_Target, lookaheadSize, topLeft, structural, changed);
                    m_Provider.Set(&provider);
                    return shared_from_this();
                }
                std::shared_ptr<MatchState> OnVisibleContentRemovedDuringTransition() override;
                void UpdateBounds(const Rect& b) override { m_Current.Set(b); }
            private:
                std::shared_ptr<TargetData> m_Target;
                rt::State<BoundsProvider*> m_Provider;
                rt::State<Rect> m_Current;
            };

            class RemovedDuringTransition final : public MatchState, public std::enable_shared_from_this<RemovedDuringTransition> {
            public:
                RemovedDuringTransition(std::shared_ptr<TargetData> target, Rect current) : m_Target(std::move(target)), m_Current(current) {}
                TargetData* Target() const override { return m_Target.get(); }
                std::optional<Rect> CurrentBounds() const override { return m_Current.Get(); }
                bool MatchIsOrHasBeenConfigured() const override { return true; }
                std::shared_ptr<MatchState> OnMatchFound(BoundsProvider* previous) override {
                    return std::make_shared<ConfigPending>(previous, m_Target, m_Current.Peek());
                }
                void UpdateBounds(const Rect& b) override { m_Current.Set(b); }
                std::shared_ptr<MatchState> OnVisibleContentRemovedDuringTransition() override { return shared_from_this(); }
            private:
                std::shared_ptr<TargetData> m_Target;
                rt::State<Rect> m_Current;
            };

            std::shared_ptr<MatchState> NoMatch::OnMatchFound(BoundsProvider* previous) { return std::make_shared<ConfigPending>(previous); }

            std::shared_ptr<MatchState> ConfigPending::ConfigureActiveMatch(SharedElement& e, BoundsProvider& provider, Size lookaheadSize, Offset topLeft,
                                                                           Offset structural) {
                auto target = m_Target ? m_Target : std::make_shared<TargetData>(lookaheadSize, topLeft - structural, structural);
                std::optional<Rect> current = m_Current.Peek();
                if (!current) current = BoundsFromLastTarget(e, m_Before ? m_Before : FirstEnabledProvider(e));
                if (!current) current = MakeRect(topLeft, lookaheadSize);
                UpdateTargetData(*target, lookaheadSize, topLeft, structural, true);
                return std::make_shared<Configured>(target, &provider, *current);
            }

            std::shared_ptr<MatchState> Configured::OnVisibleContentRemovedDuringTransition() {
                const TargetData& t = *m_Target;
                const Rect last = MakeRect(t.currentMfrOffset.Peek() + t.targetStructuralOffset.Peek(), t.size.Peek());
                const std::optional<Rect> next = m_Provider.Peek() ? m_Provider.Peek()->CalculateAlternativeTargetBounds(last) : std::nullopt;
                if (!next) return NoMatchFoundState();
                auto updated = std::make_shared<TargetData>(SizeOf(*next), t.initialMfrOffset.Peek(), TopLeft(*next) - t.currentMfrOffset.Peek());
                updated->currentMfrOffset.Set(t.currentMfrOffset.Peek());
                return std::make_shared<RemovedDuringTransition>(updated, m_Current.Peek());
            }
        }

        std::shared_ptr<MatchState> NoMatchFoundState() {
            static const std::shared_ptr<MatchState> state = std::make_shared<NoMatch>();
            return state;
        }

        void SharedTransitionStateMachine::ResetState() {
            m_Request = StateChangeRequest::NoRequest;
            m_LastHandledRequestId = m_RequestId.Peek();
            m_State.Set(NoMatchFoundState());
        }
        void SharedTransitionStateMachine::DeferRequest(StateChangeRequest r) {
            m_Request = r;
            m_RequestId.Set(m_LastHandledRequestId + 1);
        }
        void SharedTransitionStateMachine::ProcessPendingRequest() {
            if (m_RequestId.Peek() != m_LastHandledRequestId) {
                m_LastHandledRequestId = m_RequestId.Peek();
                std::shared_ptr<MatchState> s = m_State.Peek();
                switch (m_Request) {
                    case StateChangeRequest::NoMatchFound: s = NoMatchFoundState(); break;
                    case StateChangeRequest::NoRequest: break;
                    case StateChangeRequest::MatchFound: s = s->OnMatchFound(m_TargetBoundsProvider); break;
                    case StateChangeRequest::VisibleContentAbsentDuringTransition: {
                        bool providerEnabled = false;
                        for (SharedElementEntry* e : m_Element.EnabledEntries()) if (e->boundsProvider == m_TargetBoundsProvider) providerEnabled = true;
                        s = providerEnabled ? NoMatchFoundState() : s->OnVisibleContentRemovedDuringTransition();
                        break;
                    }
                }
                if (s != m_State.Peek()) m_State.Set(s);
                m_Request = StateChangeRequest::NoRequest;
            }
            UpdateTargetBoundsProvider();
        }
        void SharedTransitionStateMachine::CheckForAndDeferStateUpdates(bool hasVisibleContent) {
            if (m_Element.EnabledEntries().size() > 1 && hasVisibleContent) DeferRequest(StateChangeRequest::MatchFound);
            else if (m_Element.scope.IsTransitionActive()) { if (!hasVisibleContent) DeferRequest(StateChangeRequest::VisibleContentAbsentDuringTransition); }
            else ResetState();
            InvalidateTargetBoundsProvider();
        }
        void SharedTransitionStateMachine::InvalidateTargetBoundsProvider() {
            SharedElementEntry* target = nullptr;
            for (SharedElementEntry* e : m_Element.EnabledEntries()) if (e->Target()) { target = e; break; }
            if (!target && !m_TargetBoundsProvider) return;
            if (target && target->boundsProvider == m_TargetBoundsProvider) return;
            m_TargetProviderUpdateRequestId.Set(m_LastHandledTargetProviderUpdateRequestId + 1);
        }
        void SharedTransitionStateMachine::UpdateTargetBoundsProvider() {
            if (m_TargetProviderUpdateRequestId.Peek() == m_LastHandledTargetProviderUpdateRequestId) return;
            BoundsProvider* next = nullptr;
            const auto& list = m_Element.scope.IsTransitionActive() ? m_Element.EnabledEntries() : m_Element.AllEntries();
            for (SharedElementEntry* e : list) if (e->Target()) { next = e->boundsProvider; break; }
            m_TargetBoundsProvider = next;
            m_LastHandledTargetProviderUpdateRequestId = m_TargetProviderUpdateRequestId.Peek();
        }
        std::optional<Rect> SharedTransitionStateMachine::TryInitializingCurrentBounds() {
            ProcessPendingRequest();
            return m_State.Peek()->InitializeCurrentBounds(m_Element);
        }
        void SharedTransitionStateMachine::ConfigureActiveMatch(Size lookaheadSize, Offset topLeft, Offset structural) {
            if (!m_TargetBoundsProvider) return;
            auto s = m_State.Peek()->ConfigureActiveMatch(m_Element, *m_TargetBoundsProvider, lookaheadSize, topLeft, structural);
            if (s != m_State.Peek()) m_State.Set(s);
        }

        // --- BoundsAnimation ----------------------------------------------------------------------

        BoundsAnimation::BoundsAnimation(SharedTransitionScope& scope, Transition<bool>& transition, DeferredAnimation<bool>& animation,
                                         BoundsTransform transform, std::function<Offset()> momentumOffset)
            : m_Scope(&scope), m_Transition(&transition), m_Animation(&animation), m_Transform(std::move(transform)), m_Spec(DefaultBoundsSpring()),
              m_Momentum(std::move(momentumOffset)) {}

        void BoundsAnimation::UpdateAnimation(DeferredAnimation<bool>& animation, BoundsTransform transform) {
            if (m_Animation != &animation) {
                m_Animation = &animation;
                m_Started.Set(false);
                m_Spec = DefaultBoundsSpring();
            }
            m_Transform = std::move(transform);
        }

        std::optional<Rect> BoundsAnimation::Value() const {
            if (!m_Scope->IsTransitionActive() || !m_Started.Get()) return std::nullopt;
            const anim::Vec* v = m_Animation->Value();
            if (!v) return std::nullopt;
            Rect r = FromVec(*v);
            const Offset o = m_Momentum ? m_Momentum() : Offset{};
            return (o.x != 0 || o.y != 0) ? Translate(r, o) : r;
        }

        void BoundsAnimation::Animate(const Rect& current, const Rect& target, BoundsTransform forced, std::optional<Rect> forcedInitial,
                                      std::optional<anim::Vec> forcedVelocity) {
            if (!m_Scope->IsTransitionActive()) return;
            m_Current = current;
            m_TargetRect = target;
            if (!m_Started.Peek()) {
                const BoundsTransform& t = forced ? forced : m_Transform;
                m_Spec = t ? t(current, target) : DefaultBoundsSpring();
            }
            const Rect cur = m_Current, tgt = m_TargetRect;
            Transition<bool>* tr = m_Transition;
            const anim::SpecPtr spec = m_Spec;
            m_Animation->Animate([spec](const Transition<bool>::Segment&) { return spec; },
                                 [tr, cur, tgt](const bool& s) { return ToVec(s == tr->TargetState() ? tgt : cur); },
                                 forcedInitial ? std::optional(ToVec(*forcedInitial)) : std::nullopt, forcedVelocity);
            if (!m_Started.Peek()) m_Started.Set(true);
        }

        // --- SharedElementEntry ------------------------------------------------------------------

        SharedElementEntry::SharedElementEntry(SharedElement* element, std::shared_ptr<BoundsAnimation> bounds, PlaceholderSize placeholder,
                                               bool renderOnlyWhenVisible, OverlayClipPtr clip, bool renderInOverlay, SharedContentState* user, float zIndex)
            : sharedElement(element), boundsAnimation(std::move(bounds)), placeholderSize(std::move(placeholder)), renderOnlyWhenVisible(renderOnlyWhenVisible),
              overlayClip(std::move(clip)), renderInOverlayDuringTransition(renderInOverlay), userState(user), m_ZIndex(zIndex) {}

        void SharedElementEntry::SetZIndex(float z) {
            if (m_ZIndex.Peek() == z) return;
            m_ZIndex.Set(z);
            sharedElement->scope.Root().ZOrderChanged();
        }
        void SharedElementEntry::SetParentState(SharedElementEntry* p) {
            if (m_Parent == p) return;
            m_Parent = p;
            sharedElement->scope.Root().ZOrderChanged();
        }
        std::optional<Rect> SharedElementEntry::CalculateTargetBounds(const Rect& previous) const {
            const SharedContentConfig& c = userState->Config();
            if (!c.alternativeTargetBoundsAfterRemoval) return std::nullopt;
            auto& root = sharedElement->scope.Root();
            const Size layoutSize = root.lookaheadRoot ? SizeF(root.lookaheadRoot->Size()) : Size{};
            return c.alternativeTargetBoundsAfterRemoval(*userState, previous, layoutSize);
        }
        bool SharedElementEntry::ShouldRenderAtAll() const {
            SharedElementEntry* pending = sharedElement->PendingTargetEntry();
            const bool visible = pending ? pending == this : Target();
            const auto& s = sharedElement->State();
            return visible || (s->MatchIsOrHasBeenConfigured() && !s->ActiveMatchFound()) || !renderOnlyWhenVisible;
        }
        bool SharedElementEntry::ShouldRenderInOverlay() const {
            return ShouldRenderAtAll() && sharedElement->BoundsTransformIsActive() && IsEnabled() && renderInOverlayDuringTransition &&
                   (sharedElement->scope.IsTransitionActive() || IsDeferredEntering() || IsDeferredExiting());
        }
        bool SharedElementEntry::ShouldRenderInPlace() const {
            return !sharedElement->BoundsTransformIsActive() || (!ShouldRenderInOverlay() && ShouldRenderAtAll());
        }
        bool SharedElementEntry::ShouldApplyContainerTransform() const {
            return userState->Config().deferredTransitionBehavior == DeferredTransitionBehavior::FollowContainer || !renderInOverlayDuringTransition;
        }
        bool SharedElementEntry::ShouldSeekDuringDeferredTransition() const {
            return userState->Config().deferredTransitionBehavior == DeferredTransitionBehavior::SeekToTarget && renderInOverlayDuringTransition;
        }
        SharedTransformView* SharedElementEntry::ActiveMutableTransformState() {
            if (!ShouldApplyContainerTransform() && !ShouldSeekDuringDeferredTransition()) return nullptr;
            // Only a deferred transition (or a child of one) is moved by hand.
            if (!boundsAnimation->GetTransition().IsDeferred()) return nullptr;
            SharedTransformView* own = boundsProvider ? boundsProvider->ModifierLocalTransformState() : nullptr;
            if (!own) return nullptr;
            if (own->IsHandoffActive() && m_DeferredTransformState) return m_DeferredTransformState;
            SharedTransformView* resolved = own;
            if (IsDeferredEntering()) {
                for (SharedElementEntry* e : sharedElement->EnabledEntries())
                    if (e->IsDeferredExiting()) {
                        if (SharedTransformView* x = e->boundsProvider ? e->boundsProvider->ModifierLocalTransformState() : nullptr) resolved = x;
                        break;
                    }
            }
            if (own->IsMutating()) m_DeferredTransformState = resolved;
            else if (!own->IsHandoffActive()) m_DeferredTransformState = nullptr;
            return resolved;
        }

        void SharedElementEntry::DrawInOverlay(ContentDrawScope& s) {
            if (ShouldRenderInOverlay() && !layer) layer = layer::GraphicsContext::Default().CreateGraphicsLayer();
            if (!layer) return;
            const std::optional<Rect> current = sharedElement->State()->CurrentBounds();
            if (!current || !ShouldRenderInOverlay()) return;
            s.Save();
            // A container moved by hand moves (and scales) its shared content with it.
            SharedTransformView* t = ActiveMutableTransformState();
            auto& root = sharedElement->scope.Root();
            if (ShouldApplyContainerTransform() && t && t->IsMutating() && t->parentLayoutCoordinates && t->parentLayoutCoordinates->IsAttached() &&
                root.root && root.root->IsAttached()) {
                const IntOffset o = t->ActiveOffset();
                const Offset pivot = CalculatePivot(*t->parentLayoutCoordinates, *root.root, t->ActiveTransformOrigin());
                s.Translate({ float(o.x), float(o.y) });
                s.Scale(t->ActiveScale(), t->ActiveScale(), pivot);
            }
            if (clipPathInOverlay) {
                const Rect b = clipPathInOverlay->Bounds();
                s.BeginLayer(b, 1, &*clipPathInOverlay);
                s.Translate({ current->left, current->top });
                layer::DrawLayer(s, *layer);
                s.EndLayer();
            } else {
                s.Translate({ current->left, current->top });
                layer::DrawLayer(s, *layer);
            }
            s.Restore();
        }
        void SharedElementEntry::ObservationBlock() const {
            (void)Target();
            (void)PendingTarget();
            (void)IsEnabled();
            (void)boundsAnimation->IsRunning();
            if (SharedTransformView* t = const_cast<SharedElementEntry*>(this)->ActiveMutableTransformState()) (void)t->ObserveMutating();
        }
        void SharedElementEntry::UpdateTransitionActiveness() {
            const bool was = m_TransitionActive;
            SharedTransformView* t = ActiveMutableTransformState();
            m_TransitionActive = IsEnabled() && sharedElement->FoundMatch() && (boundsAnimation->IsRunning() || (t && t->IsMutating()));
            if (m_TransitionActive != was) sharedElement->scope.Root().OnNodeTransitionActivenessChanged(m_TransitionActive);
        }
        void SharedElementEntry::OnEntryRemoved() {
            if (!m_TransitionActive) return;
            m_TransitionActive = false;
            sharedElement->scope.Root().OnNodeTransitionActivenessChanged(false);
        }

        // --- SharedElement -----------------------------------------------------------------------

        SharedElement::SharedElement(rt::Key k, SharedTransitionScope& s) : key(k), scope(s), m_Machine(*this) {}

        bool SharedElement::IsAnimating() const {
            for (SharedElementEntry* e : EnabledEntries()) if (e->boundsAnimation->IsRunning()) return true;
            return false;
        }
        void SharedElement::UpdateMatch() {
            std::vector<SharedElementEntry*> enabled;
            bool hasVisible = false;
            for (SharedElementEntry* e : m_All)
                if (e->IsEnabled()) { enabled.push_back(e); if (e->boundsAnimation->Target()) hasVisible = true; }
            if (enabled != m_Enabled.Peek()) m_Enabled.Set(enabled);
            m_Machine.CheckForAndDeferStateUpdates(hasVisible);
            for (SharedElementEntry* e : m_All) e->UpdateTransitionActiveness();
        }
        void SharedElement::UpdateExitVelocity(Offset velocity) {
            m_MomentumJob = rt::Launch(rt::Dispatcher::Main(), [](SharedElement* self, Offset v) -> rt::Task<> {
                co_await self->m_Momentum.AnimateTo(anim::Vec{ 0, 0, 0, 0 }, DefaultMomentumSpring(), anim::Vec{ v.x, v.y, 0, 0 });
                self->m_AnimationSpecFinalized = true;
            }(this, velocity));
        }
        Offset SharedElement::MomentumAnimationOffset() {
            if (!m_AnimationSpecFinalized && scope.IsTransitionActive() && m_Momentum.IsRunning()) {
                for (SharedElementEntry* e : EnabledEntries()) {
                    if (!e->Target()) continue;
                    // The target's spring, with an offset threshold, takes over the momentum.
                    const anim::SpecPtr spec = e->boundsAnimation->Spec();
                    const anim::SpecPtr spring = anim::WithSpringVisibilityThreshold(spec, 1.0f);
                    if (spring != spec)
                        m_MomentumJob = rt::Launch(rt::Dispatcher::Main(), [](SharedElement* self, anim::SpecPtr sp) -> rt::Task<> {
                            co_await self->m_Momentum.AnimateTo(anim::Vec{ 0, 0, 0, 0 }, sp);
                        }(this, spring));
                    m_AnimationSpecFinalized = true;
                    break;
                }
            }
            const anim::Vec& v = m_Momentum.Value();
            return { v[0], v[1] };
        }
        void SharedElement::OnSharedTransitionFinished() {
            m_PendingTargetBounds.Set(std::nullopt);
            bool visible = false;
            for (SharedElementEntry* e : EnabledEntries()) if (e->boundsAnimation->Target()) visible = true;
            if (EnabledEntries().size() <= 1 || !visible) m_Machine.ResetState();
        }
        SharedElementEntry* SharedElement::PendingTargetEntry() const {
            for (SharedElementEntry* e : EnabledEntries()) { const auto p = e->PendingTarget(); if (p && *p) return e; }
            return nullptr;
        }
        bool SharedElement::FoundMatch() const {
            const auto& s = State();
            return s->ActiveMatchFound() || s->MatchIsOrHasBeenConfigured() || m_Machine.ActiveMatchDeferred();
        }
        void SharedElement::OnLookaheadPlaced(ui::PlacementScope& place, SharedElementEntry& entry) {
            m_Machine.ProcessPendingRequest();
            const auto state = State();
            if (state == NoMatchFoundState() || !entry.IsEnabled()) return;
            auto& root = scope.Root();
            const LayoutCoordinates* c = place.Coordinates();
            if (!c || !root.lookaheadRoot) return;
            const LayoutCoordinates& lr = *root.lookaheadRoot;
            if (entry.boundsAnimation->Target() && state->ActiveMatchFound()) {
                const Offset topLeft = scope.Lookahead().LocalLookaheadPositionOf(lr, *c);
                const Offset structural = lr.LocalPositionOf(*c, {}, false);
                if (entry.IsDeferredExiting()) {
                    const Offset mfr = topLeft - structural;
                    if (!state->MatchIsOrHasBeenConfigured()) {
                        if (auto b = state->InitializeCurrentBounds(*this))
                            m_Machine.ConfigureActiveMatch(SizeOf(*b), TopLeft(*b), TopLeft(*b) - mfr);
                    } else if (TargetData* t = state->Target()) {
                        t->currentMfrOffset.Set(mfr);
                    }
                } else {
                    m_Machine.ConfigureActiveMatch(SizeF(c->Size()), topLeft, structural);
                }
            } else if (entry.IsDeferredEntering() && state->ActiveMatchFound()) {
                const Offset topLeft = scope.Lookahead().LocalLookaheadPositionOf(lr, *c);
                m_PendingTargetBounds.Set(MakeRect(topLeft, SizeF(c->Size())));
            }
        }
        void SharedElement::AddEntry(SharedElementEntry* e) { m_All.push_back(e); UpdateMatch(); }
        void SharedElement::RemoveEntry(SharedElementEntry* e) { std::erase(m_All, e); UpdateMatch(); }

        // --- the scope's internals -------------------------------------------------------------------

        std::shared_ptr<SharedElement> SharedTransitionRoot::SharedElementsFor(const rt::Key& key) {
            auto& e = m_Elements[key.value];
            if (!e) e = std::make_shared<SharedElement>(key, scope);
            return e;
        }

        void SharedTransitionRoot::OnNodeTransitionActivenessChanged(bool active) {
            if (active) ++m_ActiveCount;
            else m_ActiveCount = std::max(0, m_ActiveCount - 1);
            const bool now = m_ActiveCount > 0;
            if (now == scope.m_Active.Peek()) return;
            scope.m_Active.Set(now);
            if (!now) for (auto& [k, e] : m_Elements) e->OnSharedTransitionFinished();
        }

        void SharedTransitionRoot::DrawInOverlay(ContentDrawScope& s) {
            const int version = m_Version.Get();   // read: a change redraws the overlay
            if (m_LastSorted != version) {
                // LayerRendererComparator: top-level entries at zIndex 0 draw first (-1).
                auto order = [](const LayerRenderer* r) { return r->ZIndex() == 0 && r->IsEntry() && !r->ParentState() ? -1.0f : r->ZIndex(); };
                std::stable_sort(m_Renderers.begin(), m_Renderers.end(), [&](const LayerRenderer* a, const LayerRenderer* b) { return order(a) < order(b); });
                m_LastSorted = version;
            }
            const auto renderers = m_Renderers;
            for (LayerRenderer* r : renderers) r->DrawInOverlay(s);
        }

        void SharedTransitionRoot::OnEntryRemoved(SharedElementEntry* e) {
            e->OnEntryRemoved();
            SharedElement* element = e->sharedElement;
            element->RemoveEntry(e);
            std::erase(m_Renderers, static_cast<LayerRenderer*>(e));
            ZOrderChanged();
            if (element->AllEntries().empty()) {
                // Later: the key may come back in this frame (content moving).
                const std::uint64_t k = element->key.value;
                std::erase_if(m_Removals, [](const rt::Job& j) { return j.Done(); });
                m_Removals.push_back(rt::Launch(rt::Dispatcher::Main(), [](SharedTransitionRoot* self, std::uint64_t k) -> rt::Task<> {
                    auto it = self->m_Elements.find(k);
                    if (it != self->m_Elements.end() && it->second->AllEntries().empty()) self->m_Elements.erase(it);
                    co_return;
                }(this, k)));
            }
        }

        void SharedTransitionRoot::OnEntryAdded(SharedElementEntry* e) {
            SharedElement* element = e->sharedElement;
            element->AddEntry(e);
            e->UpdateTransitionActiveness();
            // After the last renderer of the same element (so it keeps its place in the order).
            auto last = m_Renderers.end();
            for (auto it = m_Renderers.begin(); it != m_Renderers.end(); ++it)
                if ((*it)->IsEntry() && static_cast<SharedElementEntry*>(*it)->sharedElement == element) { last = it; break; }
            if (last == m_Renderers.end() || last + 1 == m_Renderers.end()) m_Renderers.push_back(e);
            else m_Renderers.insert(last + 1, e);
            ZOrderChanged();
        }
    }

    // --- nodes -------------------------------------------------------------------------------------

    namespace {
        // SharedTransitionScopeRootModifierNode: records the root (and lookahead root) coordinates
        // and draws the overlay over the content.
        struct RootParams {
            SharedTransitionRoot* root = nullptr;
            bool operator==(const RootParams&) const = default;
        };
        struct RootNode final : ui::ModifierNode, ui::LayoutModifier, ui::DrawModifier {
            RootParams params;
            explicit RootNode(RootParams p) : params(p) {}
            void OnParamsChanged() {}
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::DrawModifier* AsDraw() override { return this; }
            void OnAttach() override { params.root->invalidateOverlay = [this] { InvalidateDraw(); }; }
            void OnDetach() override { params.root->invalidateOverlay = nullptr; }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                ui::Placeable& p = m.Measure(c);
                SharedTransitionRoot* root = params.root;
                return s.Layout(p.Width(), p.Height(), [&p, root](ui::PlacementScope& place) {
                    if (const LayoutCoordinates* coords = place.Coordinates()) {
                        if (place.IsLookingAhead()) root->lookaheadRoot = *coords;
                        else root->root = *coords;
                    }
                    place.Place(p, 0, 0);
                });
            }
            void Draw(ContentDrawScope& s) override {
                s.DrawContent();
                params.root->DrawInOverlay(s);
            }
        };

        // SkipToLookaheadSizeNode: measured with the lookahead constraints and reporting the
        // lookahead size (scaled into the bounds with a ScaleToBounds resize mode).
        struct SkipParams {
            std::optional<ResizeMode> scaleToBounds;
            std::function<bool()> isEnabled;
            bool operator==(const SkipParams&) const { return false; }
        };
        struct SkipNode final : ui::ModifierNode, ui::LayoutModifier, ui::DrawModifier {
            SkipParams params;
            std::optional<ui::Constraints> lookaheadConstraints;
            IntSize lookaheadSize{ -1, -1 };
            ui::Offset scaleAt;
            float scaleX = 1, scaleY = 1;
            explicit SkipNode(SkipParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::DrawModifier* AsDraw() override { return this; }
            bool Enabled() const { return !params.isEnabled || params.isEnabled(); }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                if (s.IsLookingAhead()) lookaheadConstraints = c;
                scaleX = scaleY = 1;
                if (!Enabled()) {
                    ui::Placeable& p = m.Measure(c);
                    return s.Layout(p.Width(), p.Height(), [&p](ui::PlacementScope& place) { place.Place(p, 0, 0); });
                }
                ui::Placeable* p = nullptr;
                if (s.IsLookingAhead()) {
                    p = &m.Measure(c);
                    lookaheadSize = { p->Width(), p->Height() };
                } else {
                    p = &m.Measure(lookaheadConstraints.value_or(c));
                    if (lookaheadSize.width < 0) lookaheadSize = { p->Width(), p->Height() };
                }
                const IntSize size{ c.ConstrainWidth(lookaheadSize.width), c.ConstrainHeight(lookaheadSize.height) };
                if (!params.scaleToBounds) return s.Layout(size.width, size.height, [p](ui::PlacementScope& place) { place.Place(*p, 0, 0); });
                const ResizeMode& r = *params.scaleToBounds;
                ScaleFactor f{ 1, 1 };
                if (lookaheadSize.width != 0 && lookaheadSize.height != 0) f = r.contentScale.ComputeScaleFactor(SizeF(lookaheadSize), SizeF(size));
                const IntOffset at = r.alignment.Align({ int(std::lround(lookaheadSize.width * f.x)), int(std::lround(lookaheadSize.height * f.y)) }, size,
                                                       s.Direction());
                scaleX = f.x;
                scaleY = f.y;
                scaleAt = { float(at.x), float(at.y) };
                return s.Layout(size.width, size.height, [p, at](ui::PlacementScope& place) { place.Place(*p, at.x, at.y); });
            }
            // placeWithLayer { scale, transformOrigin (0, 0) }.
            void Draw(ContentDrawScope& s) override {
                if (scaleX == 1 && scaleY == 1) { s.DrawContent(); return; }
                s.Save();
                s.Scale(scaleX, scaleY, scaleAt);
                s.DrawContent();
                s.Restore();
            }
            bool LayerTransform(ui::Size, gfx::Matrix& out) const override {
                if (scaleX == 1 && scaleY == 1) return false;
                out = { scaleX, 0, 0, scaleY, scaleAt.x - scaleX * scaleAt.x, scaleAt.y - scaleY * scaleAt.y };
                return true;
            }
            int Intrinsic(ui::MeasureScope& s, ui::Measurable& m, int cross, bool width, bool max) {
                if (!s.IsLookingAhead() && lookaheadSize.width >= 0) return width ? lookaheadSize.width : lookaheadSize.height;
                return width ? (max ? m.MaxIntrinsicWidth(cross) : m.MinIntrinsicWidth(cross)) : (max ? m.MaxIntrinsicHeight(cross) : m.MinIntrinsicHeight(cross));
            }
            int MinIntrinsicWidth(ui::MeasureScope& s, ui::Measurable& m, int h) override { return Intrinsic(s, m, h, true, false); }
            int MaxIntrinsicWidth(ui::MeasureScope& s, ui::Measurable& m, int h) override { return Intrinsic(s, m, h, true, true); }
            int MinIntrinsicHeight(ui::MeasureScope& s, ui::Measurable& m, int w) override { return Intrinsic(s, m, w, false, false); }
            int MaxIntrinsicHeight(ui::MeasureScope& s, ui::Measurable& m, int w) override { return Intrinsic(s, m, w, false, true); }
        };

        // RenderInTransitionOverlayNode.
        struct OverlayParams {
            SharedTransitionRoot* root = nullptr;
            std::function<bool()> renderInOverlay;
            float zIndex = 0;
            bool operator==(const OverlayParams&) const { return false; }
        };
        struct OverlayNode final : ui::ModifierNode, ui::LayoutModifier, ui::DrawModifier, LayerRenderer {
            OverlayParams params;
            bool enabled = false;
            ui::Offset position;
            layer::GraphicsLayerPtr layer;
            rt::State<float> zIndex;
            explicit OverlayNode(OverlayParams p) : params(std::move(p)), zIndex(params.zIndex) {}
            void OnParamsChanged() { if (zIndex.Peek() != params.zIndex) { zIndex.Set(params.zIndex); params.root->ZOrderChanged(); } }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::DrawModifier* AsDraw() override { return this; }
            void OnAttach() override { params.root->OnLayerRendererCreated(this); }
            void OnDetach() override {
                params.root->OnLayerRendererRemoved(this);
                if (layer) layer->Release();
                layer = nullptr;
            }
            SharedElementEntry* ParentState() const override { return Current(LocalSharedElementEntry); }
            layer::GraphicsLayer* Layer() const override { return layer.get(); }
            float ZIndex() const override { return zIndex.Get(); }
            void SetEnabled(bool e) {
                if (e == enabled) return;
                enabled = e;
                if (params.root->invalidateOverlay) params.root->invalidateOverlay();
                InvalidateDraw();
            }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                ui::Placeable& p = m.Measure(c);
                return s.Layout(p.Width(), p.Height(), [this, &p](ui::PlacementScope& place) {
                    if (!place.IsLookingAhead()) {
                        const bool render = params.renderInOverlay ? params.renderInOverlay() : params.root->scope.IsTransitionActive();
                        if (render && place.Coordinates() && params.root->root) {
                            SetEnabled(true);
                            const ui::Offset at = params.root->root->LocalPositionOf(ui::RequireLayoutCoordinates(*this), {});
                            if (!(at == position)) {
                                position = at;
                                if (params.root->invalidateOverlay) params.root->invalidateOverlay();
                            }
                        } else {
                            SetEnabled(false);
                        }
                    }
                    place.Place(p, 0, 0);
                });
            }
            void Draw(ContentDrawScope& s) override {
                if (enabled) {
                    if (!layer) layer = layer::GraphicsContext::Default().CreateGraphicsLayer();
                    const ui::Size size = s.GetSize();
                    layer->Record(s, { int(size.width), int(size.height) }, [](ContentDrawScope& d) { d.DrawContent(); });
                } else {
                    if (layer) layer->Release();
                    layer = nullptr;
                    s.DrawContent();
                }
            }
            void DrawInOverlay(ContentDrawScope& s) override {
                if (!enabled) return;
                if (!layer) layer = layer::GraphicsContext::Default().CreateGraphicsLayer();
                s.Save();
                s.Translate(position);
                layer::DrawLayer(s, *layer);
                s.Restore();
            }
        };

        // SharedBoundsNode: the node a sharedElement / sharedBounds puts on its layout.
        struct BoundsParams {
            std::shared_ptr<SharedElementEntry> entry;
            bool operator==(const BoundsParams& o) const { return entry == o.entry; }
        };
        struct BoundsNode final : ui::ModifierNode, ui::ApproachLayoutModifier, ui::DrawModifier, BoundsProvider {
            BoundsParams params;
            std::shared_ptr<SharedElementEntry> attached;
            std::optional<Rect> forcedHandoffBounds, boundsBeforeDetached;
            std::optional<anim::Vec> forcedHandoffVelocity;
            bool isPlaced = false;
            // Where the approach placed the content in this node. Compose draws a node that both
            // lays out and draws in the space below it (where its child is); kiln draws it in
            // its own, so the recording is shifted back to the child's origin.
            ui::Offset childOffset;
            explicit BoundsNode(BoundsParams p) : params(std::move(p)) {}
            void PlaceChild(ui::PlacementScope& place, ui::Placeable& p, int x, int y) {
                childOffset = { float(x), float(y) };
                place.Place(p, x, y);
            }
            ui::LayoutModifier* AsLayout() override { return this; }
            ui::DrawModifier* AsDraw() override { return this; }
            SharedElementEntry& Entry() const { return *params.entry; }
            SharedElement& Element() const { return *params.entry->sharedElement; }
            SharedTransitionRoot& Root() const { return Element().scope.Root(); }

            void ReleaseLayer() {
                if (Entry().layer) { Entry().layer->Release(); Entry().layer = nullptr; }
            }
            void Setup() {
                Provide(LocalSharedElementEntry, params.entry.get());
                Entry().SetParentState(Current(LocalSharedElementEntry));
                ReleaseLayer();
                isPlaced = false;
                Entry().boundsProvider = this;
            }
            void OnParamsChanged() {
                if (attached == params.entry) return;
                if (attached) attached->isAttached.Set(false);
                attached = params.entry;
                Entry().isAttached.Set(IsAttached());
                if (IsAttached()) { Setup(); OnObservedReadsChanged(); }
            }
            void OnAttach() override {
                attached = params.entry;
                Setup();
                Entry().isAttached.Set(true);
                OnObservedReadsChanged();
            }
            void OnDetach() override {
                if (Root().root && Root().root->IsAttached() && isPlaced && Owner()) {
                    const LayoutCoordinates c = ui::RequireLayoutCoordinates(*this);
                    const ui::Offset p = c.PositionInRoot() - Root().root->PositionInRoot();
                    boundsBeforeDetached = MakeRect(p, SizeF(c.Size()));
                } else {
                    boundsBeforeDetached.reset();
                }
                ReleaseLayer();
                resolvedTransformState = nullptr;
                Entry().SetParentState(nullptr);
                Entry().boundsProvider = nullptr;
                Entry().isAttached.Set(false);
                isPlaced = false;
            }
            void OnObservedReadsChanged() override {
                Element().UpdateMatch();
                ObserveReads([this] { Entry().ObservationBlock(); });
            }

            // BoundsProvider
            std::optional<Rect> LastBoundsInSharedTransitionScope() const override {
                if (!IsAttached()) return std::nullopt;
                if (!isPlaced) return boundsBeforeDetached;
                if (!Root().root) return std::nullopt;
                const LayoutCoordinates c = ui::RequireLayoutCoordinates(*this);
                return MakeRect(Root().root->LocalPositionOf(c, {}), SizeF(c.Size()));
            }
            std::optional<Rect> CalculateAlternativeTargetBounds(const Rect& r) const override { return Entry().CalculateTargetBounds(r); }
            // Kept once found (not while absent: the container may provide one later).
            mutable SharedTransformView* resolvedTransformState = nullptr;
            SharedTransformView* ModifierLocalTransformState() const override {
                if (!IsAttached()) return nullptr;
                if (!resolvedTransformState) resolvedTransformState = Current(foundation::detail::LocalSharedMutableTransformState);
                return resolvedTransformState;
            }
            bool IsSeeking() {
                if (!Entry().ShouldSeekDuringDeferredTransition()) return false;
                SharedTransformView* t = Entry().ActiveMutableTransformState();
                return t && t->IsMutating();
            }
            std::optional<Rect> SeekBounds(const Rect& base) {
                if (!IsSeeking()) return std::nullopt;
                SharedTransformView* t = Entry().ActiveMutableTransformState();
                const float progress = t ? t->ResolveSharedElementProgressValue() : 0.0f;
                const auto& pending = Element().PendingTargetBounds();
                if (progress > 0 && pending) return Lerp(base, *pending, progress);
                return std::nullopt;
            }
            // updateDeferredHandoffValues: when a hand-moved phase hands off, the bounds
            // animation starts from where the hand left them, with the hand's velocity.
            void UpdateDeferredHandoffValues() {
                const auto state = Element().State();
                TargetData* target = state->Target();
                const std::optional<Rect> current = state->CurrentBounds();
                if (!target || !current) return;
                SharedTransformView* m = Entry().ActiveMutableTransformState();
                if (!m || !m->IsHandoffActive()) { Entry().hasHandoffOccurred = false; return; }
                if (Entry().hasHandoffOccurred) return;
                const Offset mfrDelta = target->currentMfrOffset.Peek() - target->initialMfrOffset.Peek();
                if (!Entry().ShouldApplyContainerTransform()) {
                    if (Entry().ShouldSeekDuringDeferredTransition()) {
                        forcedHandoffBounds = Translate(*current, Offset{ -mfrDelta.x, -mfrDelta.y });
                        const float pv = m->SharedElementProgressHandoffVelocityValue();
                        const std::optional<Rect> base = Entry().boundsAnimation->Value();
                        const auto& pending = Element().PendingTargetBounds();
                        if (base && pending && pv != 0) {
                            const Rect b = Translate(*base, mfrDelta);
                            forcedHandoffVelocity = anim::Vec{ (pending->left - b.left) * pv, (pending->top - b.top) * pv, (pending->right - b.right) * pv,
                                                               (pending->bottom - b.bottom) * pv };
                        } else {
                            forcedHandoffVelocity.reset();
                        }
                        Entry().hasHandoffOccurred = true;
                    }
                    return;
                }
                const auto& parent = m->parentLayoutCoordinates;
                if (!parent || !parent->IsAttached() || !Root().root || !Root().root->IsAttached()) return;
                const float manualScale = m->LastManualScale();
                const Offset pivot = CalculatePivot(*parent, *Root().root, m->LastTransformOriginValue());
                const Offset tl = TransformPoint(TopLeft(*current), pivot, manualScale, m->LastManualSlide());
                const bool ownContainerMutating = Entry().boundsProvider && Entry().boundsProvider->ModifierLocalTransformState() == m;
                const bool containerApplies = !Entry().ShouldRenderInOverlay() && ownContainerMutating;
                const float scale = containerApplies ? 1.0f : manualScale;
                forcedHandoffBounds = Translate(Rect{ tl.x, tl.y, tl.x + current->Width() * scale, tl.y + current->Height() * scale }, Offset{ -mfrDelta.x, -mfrDelta.y });
                if (!containerApplies) {
                    const float sv = m->ScaleHandoffVelocityValue().value_or(0.0f);
                    const Offset ov = m->SlideHandoffVelocityValue().value_or(Offset{});
                    forcedHandoffVelocity = anim::Vec{ (current->left - pivot.x) * sv + ov.x, (current->top - pivot.y) * sv + ov.y,
                                                       (current->right - pivot.x) * sv + ov.x, (current->bottom - pivot.y) * sv + ov.y };
                } else {
                    forcedHandoffVelocity.reset();
                }
                Entry().hasHandoffOccurred = true;
            }

            // The lookahead pass: straight through, then where it lands configures the match.
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                ui::Placeable& p = m.Measure(c);
                SharedElement* element = &Element();
                SharedElementEntry* entry = &Entry();
                return s.Layout(p.Width(), p.Height(), [&p, element, entry](ui::PlacementScope& place) {
                    place.Place(p, 0, 0);
                    element->OnLookaheadPlaced(place, *entry);
                });
            }
            bool IsMeasurementApproachInProgress(IntSize) override {
                return Entry().IsEnabled() && Element().FoundMatch() &&
                       (Element().scope.IsTransitionActive() || Entry().IsDeferredEntering() || Entry().IsDeferredExiting());
            }
            ui::MeasureResult ApproachMeasure(ui::ApproachMeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                UpdateDeferredHandoffValues();
                std::optional<Rect> base = forcedHandoffBounds;
                if (!base) base = Entry().boundsAnimation->Value();
                if (!base && IsSeeking()) if (TargetData* t = Element().State()->Target()) base = t->TargetBounds();
                if (!base) base = Element().TryInitializingCurrentBounds();
                if (base) if (auto sought = SeekBounds(*base)) base = sought;
                ui::Constraints resolved = c;
                if (base) {
                    const int w = std::max(0, int(std::lround(base->Width()))), h = std::max(0, int(std::lround(base->Height())));
                    resolved = ui::Constraints::Fixed(w, h);
                }
                ui::Placeable& p = m.Measure(resolved);
                return ApproachPlace(s, p);
            }
            ui::MeasureResult ApproachPlace(ui::ApproachMeasureScope& s, ui::Placeable& p) {
                IntSize size{ p.Width(), p.Height() };
                if (Element().State()->MatchIsOrHasBeenConfigured()) size = Entry().placeholderSize.CalculateSize(s.lookaheadSize, size);
                return s.Layout(size.width, size.height, [this, &p](ui::PlacementScope& place) {
                    isPlaced = true;
                    boundsBeforeDetached.reset();
                    const auto state = Element().State();
                    if (!Entry().IsEnabled()) { PlaceChild(place, p, 0, 0); return; }
                    if (!state->MatchIsOrHasBeenConfigured()) { PlaceChild(place, p, 0, 0); return; }
                    TargetData* target = state->Target();
                    const std::optional<Rect> current = state->CurrentBounds();
                    if (!target || !current) { PlaceChild(place, p, 0, 0); return; }
                    if (Element().scope.IsTransitionActive()) PlaceInTransition(place, p, *target, *current);
                    else PlaceBeyondTransition(place, p, *current);
                });
            }
            void PlaceBeyondTransition(ui::PlacementScope& place, ui::Placeable& p, const Rect& current) {
                const LayoutCoordinates* c = place.Coordinates();
                if (Entry().boundsAnimation->Target() || !c || !Root().root) { PlaceChild(place, p, 0, 0); return; }
                const ui::Offset inScope = Root().root->LocalPositionOf(*c, {});
                PlaceChild(place, p, int(std::lround(current.left - inScope.x)), int(std::lround(current.top - inScope.y)));
            }
            void PlaceInTransition(ui::PlacementScope& place, ui::Placeable& p, TargetData& target, const Rect& current) {
                const LayoutCoordinates* c = place.Coordinates();
                if (!c || !Root().root) { PlaceChild(place, p, 0, 0); return; }
                const LayoutCoordinates& root = *Root().root;
                const bool removed = !Element().State()->ActiveMatchFound();
                const ui::Offset inScope = root.LocalPositionOf(*c, {});
                if (removed) {
                    Entry().boundsAnimation->Animate(current, target.TargetBounds(), [](const Rect&, const Rect&) { return anim::Spring(1.0f, anim::spring::StiffnessMedium, 1.0f); },
                                                     forcedHandoffBounds, forcedHandoffVelocity);
                } else {
                    Entry().boundsAnimation->Animate(current, target.TargetBounds(), {}, forcedHandoffBounds, forcedHandoffVelocity);
                }
                forcedHandoffBounds.reset();
                forcedHandoffVelocity.reset();
                const std::optional<Rect> animated = Entry().boundsAnimation->Value();
                const std::optional<ui::Offset> animatedTopLeft = animated ? std::optional(target.OffsetFromDirectManipulation(*animated)) : std::nullopt;
                const Rect base = animatedTopLeft ? MakeRect(*animatedTopLeft, SizeOf(*animated)) : MakeRect(inScope, SizeF(c->Size()));
                const std::optional<Rect> sought = SeekBounds(base);
                ui::Offset topLeft;
                if (Entry().boundsAnimation->Target() || removed) {
                    const Rect bounds = sought ? *sought : base;
                    topLeft = TopLeft(bounds);
                    Element().State()->UpdateBounds(bounds);
                } else {
                    topLeft = sought ? TopLeft(*sought) : animatedTopLeft ? *animatedTopLeft : TopLeft(current);
                }
                // In place, a container moved by hand moves its shared content along.
                ui::Offset finalTopLeft = topLeft;
                SharedTransformView* mt = Entry().ActiveMutableTransformState();
                if (Entry().ShouldApplyContainerTransform() && mt && mt->IsMutating()) {
                    if (!Entry().boundsAnimation->IsRunning()) finalTopLeft = inScope;
                    if (mt->parentLayoutCoordinates && mt->parentLayoutCoordinates->IsAttached() && root.IsAttached()) {
                        const Offset pivot = CalculatePivot(*mt->parentLayoutCoordinates, root, mt->ActiveTransformOrigin());
                        finalTopLeft = TransformPoint(topLeft, pivot, mt->ActiveScale(), mt->ActiveOffset());
                    }
                }
                const ui::Offset local = c->LocalPositionOf(root, finalTopLeft);
                PlaceChild(place, p, int(std::lround(local.x)), int(std::lround(local.y)));
            }

            void Draw(ContentDrawScope& s) override {
                const std::optional<Rect> bounds = Element().State()->CurrentBounds();
                if (!Entry().ShouldRenderInOverlay()) {
                    Entry().clipPathInOverlay.reset();
                    ReleaseLayer();
                    if (Entry().ShouldRenderInPlace()) s.DrawContent();
                    return;
                }
                Entry().clipPathInOverlay = bounds && Entry().overlayClip
                    ? Entry().overlayClip->GetClipPath(*Entry().userState, *bounds, s.Direction(), s) : std::nullopt;
                if (!Entry().layer) Entry().layer = layer::RequireGraphicsContext(*this).CreateGraphicsLayer();
                const ui::Size size = s.GetSize();
                const ui::Offset at = childOffset;
                Entry().layer->Record(s, { int(size.width), int(size.height) }, [at](ContentDrawScope& d) {
                    d.Translate({ -at.x, -at.y });
                    d.DrawContent();
                });
                if (Entry().ShouldRenderInPlace()) {
                    s.Save();
                    s.Translate(at);
                    layer::DrawLayer(s, *Entry().layer);
                    s.Restore();
                }
            }
        };

        // The parent's clip (ParentClip, the default OverlayClip).
        struct ParentClip final : OverlayClip {
            std::optional<gfx::Path> GetClipPath(const SharedContentState& state, const Rect&, ui::LayoutDirection, const ui::Density&) const override {
                SharedContentState* parent = state.ParentSharedContentState();
                return parent ? parent->ClipPathInOverlay() : std::nullopt;
            }
        };
        struct ShapeClip final : OverlayClip {
            std::shared_ptr<const gfx::Shape> shape;
            explicit ShapeClip(std::shared_ptr<const gfx::Shape> s) : shape(std::move(s)) {}
            std::optional<gfx::Path> GetClipPath(const SharedContentState&, const Rect& bounds, ui::LayoutDirection dir, const ui::Density& d) const override {
                gfx::Path p = shape->CreateOutline(SizeOf(bounds), dir, d).ToPath();
                p.Translate({ bounds.left, bounds.top });
                return p;
            }
        };
        OverlayClipPtr DefaultParentClip() {
            static const OverlayClipPtr clip = std::make_shared<ParentClip>();
            return clip;
        }

        // The enter / exit of sharedBounds content: fade, scale and slide on the visibility
        // transition, applied only while a match is found (createEnterExitModifier with isEnabled).
        Modifier EnterExitModifier(Modifier m, Transition<EnterExitState>& t, const EnterTransition& enter, const ExitTransition& exit,
                                   std::function<bool()> isEnabled) {
            using Seg = Transition<EnterExitState>::Segment;
            const auto entering = [](const Seg& s) { return s.initialState == EnterExitState::PreEnter && s.targetState == EnterExitState::Visible; };
            const auto exiting = [](const Seg& s) { return s.initialState == EnterExitState::Visible && s.targetState == EnterExitState::PostExit; };
            const float alpha = AnimateFloat<EnterExitState>(t, [&](EnterExitState s) {
                if (s == EnterExitState::Visible) return 1.0f;
                if (s == EnterExitState::PreEnter) return enter.config.fade ? enter.config.fade->alpha : 1.0f;
                return exit.config.fade ? exit.config.fade->alpha : 1.0f;
            }, [&](const Seg& s) {
                if (entering(s) && enter.config.fade && enter.config.fade->spec) return enter.config.fade->spec;
                if (exiting(s) && exit.config.fade && exit.config.fade->spec) return exit.config.fade->spec;
                return DefaultEnterExitSpring();
            }, rt::Key("sharedAlpha"));
            const float scale = AnimateFloat<EnterExitState>(t, [&](EnterExitState s) {
                if (s == EnterExitState::Visible) return 1.0f;
                if (s == EnterExitState::PreEnter) return enter.config.scale ? enter.config.scale->scale : 1.0f;
                return exit.config.scale ? exit.config.scale->scale : 1.0f;
            }, [&](const Seg& s) {
                if (entering(s) && enter.config.scale && enter.config.scale->spec) return enter.config.scale->spec;
                if (exiting(s) && exit.config.scale && exit.config.scale->spec) return exit.config.scale->spec;
                return DefaultEnterExitSpring();
            }, rt::Key("sharedScale"));
            const float slide = AnimateFloat<EnterExitState>(t, [&](EnterExitState s) {
                if (s == EnterExitState::Visible) return 1.0f;
                return (s == EnterExitState::PreEnter ? enter.config.slide.has_value() : exit.config.slide.has_value()) ? 0.0f : 1.0f;
            }, [&](const Seg& s) {
                if (entering(s) && enter.config.slide) return enter.config.slide->spec;
                if (exiting(s) && exit.config.slide) return exit.config.slide->spec;
                return DefaultSpatialEnterExitSpring();
            }, rt::Key("sharedSlide"));
            const EnterExitState target = t.TargetState();
            const auto slideCfg = target == EnterExitState::PostExit ? exit.config.slide : enter.config.slide;
            const auto scaleCfg = target == EnterExitState::PostExit ? exit.config.scale : enter.config.scale;
            return GraphicsLayer(std::move(m), [alpha, scale, slide, slideCfg, scaleCfg, isEnabled](GraphicsLayerParams& p) {
                if (isEnabled && !isEnabled()) return;
                p.alpha = alpha;
                p.scaleX = p.scaleY = scale;
                if (scaleCfg) { p.originX = scaleCfg->originX; p.originY = scaleCfg->originY; }
                if (slideCfg && slide < 1) {
                    const IntOffset o = slideCfg->offset({ int(p.size.width), int(p.size.height) });
                    p.translationX = float(o.x) * (1 - slide);
                    p.translationY = float(o.y) * (1 - slide);
                }
            });
        }
    }

    // --- SharedTransitionScope ---------------------------------------------------------------------

    SharedTransitionScope::SharedTransitionScope(ui::LookaheadScope& lookahead)
        : m_Lookahead(&lookahead), m_Root(std::make_unique<SharedTransitionRoot>(*this)) {}
    SharedTransitionScope::~SharedTransitionScope() = default;

    SharedContentConfig SharedTransitionScope::MakeSharedContentConfig(DeferredTransitionBehavior behavior) {
        SharedContentConfig c;
        c.deferredTransitionBehavior = behavior;
        return c;
    }

    OverlayClipPtr SharedTransitionScope::MakeOverlayClip(std::shared_ptr<const gfx::Shape> clipShape) const {
        return std::make_shared<ShapeClip>(std::move(clipShape));
    }

    SharedContentState& SharedTransitionScope::RememberSharedContentState(rt::Key key, std::optional<SharedContentConfig> config) {
        auto& c = *rt::Composition::Current();
        SharedContentConfig cfg = config ? std::move(*config) : MakeSharedContentConfig();
        auto& state = c.Remember<std::shared_ptr<SharedContentState>>([&] { return std::make_shared<SharedContentState>(key, cfg); }, rt::Keys(key.value));
        state->SetConfig(std::move(cfg));
        return *state;
    }

    Modifier SharedTransitionScope::SkipToLookaheadSize(Modifier m, std::function<bool()> enabled) {
        if (!enabled) enabled = [this] { return IsTransitionActive(); };
        return m.With<SkipNode>(SkipParams{ std::nullopt, std::move(enabled) });
    }

    Modifier SharedTransitionScope::SkipToLookaheadPosition(Modifier m, std::function<bool()> enabled) {
        if (!enabled) enabled = [this] { return IsTransitionActive(); };
        SharedTransitionRoot* root = m_Root.get();
        ui::LookaheadScope* lookahead = m_Lookahead;
        return ApproachLayout(std::move(m), [](IntSize) { return false; },
            [enabled](ui::PlacementScope&, const ui::LayoutCoordinates&) { return enabled(); },
            [enabled, root, lookahead](ui::ApproachMeasureScope& s, ui::Measurable& mm, const ui::Constraints& c) {
                ui::Placeable& p = mm.Measure(c);
                return s.Layout(p.Width(), p.Height(), [&p, enabled, root, lookahead](ui::PlacementScope& place) {
                    const LayoutCoordinates* it = place.Coordinates();
                    if (!enabled() || !it || !root->root) { place.Place(p, 0, 0); return; }
                    const LayoutCoordinates& scope = *root->root;
                    const ui::Offset target = lookahead->LocalLookaheadPositionOf(scope, *it);
                    const ui::Offset actual = scope.LocalPositionOf(*it, {});
                    const ui::Offset delta = target - actual;
                    const ui::Offset offset = it->LocalPositionOf(scope, delta) - it->LocalPositionOf(scope, {});
                    place.Place(p, int(std::lround(offset.x)), int(std::lround(offset.y)));
                });
            });
    }

    Modifier SharedTransitionScope::RenderInSharedTransitionScopeOverlay(Modifier m, float zIndex, std::function<bool()> renderInOverlay) {
        if (!renderInOverlay) renderInOverlay = [this] { return IsTransitionActive(); };
        return m.With<OverlayNode>(OverlayParams{ m_Root.get(), std::move(renderInOverlay), zIndex });
    }

    Modifier SharedTransitionScope::SharedElement(Modifier m, SharedContentState& state, const AnimatedVisibilityScope& av, BoundsTransform transform,
                                                  PlaceholderSize placeholder, bool renderInOverlay, float zIndex, OverlayClipPtr clip) {
        return SharedBoundsImpl(std::move(m), state, av.transition, false, std::move(transform), std::move(placeholder), true, renderInOverlay, zIndex,
                                std::move(clip));
    }

    Modifier SharedTransitionScope::SharedElementWithCallerManagedVisibility(Modifier m, SharedContentState& state, bool visible, BoundsTransform transform,
                                                                             PlaceholderSize placeholder, bool renderInOverlay, float zIndex, OverlayClipPtr clip) {
        return SharedBoundsImpl(std::move(m), state, nullptr, visible, std::move(transform), std::move(placeholder), true, renderInOverlay, zIndex,
                                std::move(clip));
    }

    Modifier SharedTransitionScope::SharedBounds(Modifier m, SharedContentState& state, const AnimatedVisibilityScope& av, std::optional<EnterTransition> enter,
                                                 std::optional<ExitTransition> exit, BoundsTransform transform, std::optional<ResizeMode> resizeMode,
                                                 PlaceholderSize placeholder, bool renderInOverlay, float zIndex, OverlayClipPtr clip) {
        if (!av.transition) throw std::logic_error("sharedBounds needs an AnimatedVisibilityScope with a transition");
        Modifier out = SharedBoundsImpl(std::move(m), state, av.transition, false, std::move(transform), std::move(placeholder), false, renderInOverlay,
                                        zIndex, std::move(clip));
        SharedContentState* sp = &state;
        auto matched = [sp] { return sp->IsMatchFound(); };
        out = EnterExitModifier(std::move(out), *av.transition, enter ? *enter : SharedTransitionDefaults::EnterTransition(),
                                exit ? *exit : SharedTransitionDefaults::ExitTransition(), matched);
        const ResizeMode mode = resizeMode ? *resizeMode : SharedTransitionDefaults::ResizeMode();
        if (mode.kind == ResizeMode::Kind::ScaleToBounds) {
            // createContentScaleModifier: Crop clips to the bounds.
            if (mode.contentScale.kind == ContentScale::Kind::Crop)
                out = GraphicsLayer(std::move(out), [matched](GraphicsLayerParams& p) { p.clip = matched(); });
            out = out.With<SkipNode>(SkipParams{ mode, matched });
        }
        return out;
    }

    Modifier SharedTransitionScope::SharedBoundsImpl(Modifier m, SharedContentState& state, Transition<EnterExitState>* parent, bool callerVisible,
                                                     BoundsTransform transform, PlaceholderSize placeholder, bool renderOnlyWhenVisible, bool renderInOverlay,
                                                     float zIndex, OverlayClipPtr clip) {
        auto& c = *rt::Composition::Current();
        if (!transform) transform = SharedTransitionDefaults::BoundsTransform();
        if (!clip) clip = DefaultParentClip();
        std::shared_ptr<SharedElementEntry> entry;
        c.Section([&] {
            auto& element = c.Remember<std::shared_ptr<detail::SharedElement>>([&] { return m_Root->SharedElementsFor(state.Key()); });
            Transition<bool>* bounds = nullptr;
            if (parent) {
                bounds = &CreateChildTransition<bool, EnterExitState>(*parent, [](const EnterExitState& s) { return s == EnterExitState::Visible; },
                                                                      rt::Key(std::int64_t(reinterpret_cast<std::uintptr_t>(parent))));
            } else {
                auto& ts = c.Remember<std::shared_ptr<MutableTransitionState<bool>>>([&] {
                    return std::make_shared<MutableTransitionState<bool>>(element->EnabledEntries().empty() ? callerVisible : !callerVisible);
                });
                ts->SetTargetState(callerVisible);
                bounds = &UpdateTransition(*ts);
            }
            // A new deferred animation whenever the scope's activeness flips (key(isTransitionActive)).
            auto& anim = CreateDeferredAnimation(*bounds, 4, rt::Key(std::int64_t(IsTransitionActive() ? 2 : 1)));
            auto* el = element.get();
            auto& boundsAnimation = c.Remember<std::shared_ptr<BoundsAnimation>>([&] {
                return std::make_shared<BoundsAnimation>(*this, *bounds, anim, transform, [el] { return el->MomentumAnimationOffset(); });
            }, rt::Keys(reinterpret_cast<std::uintptr_t>(bounds)));
            boundsAnimation->UpdateAnimation(anim, transform);
            auto& e = c.Remember<std::shared_ptr<SharedElementEntry>>([&] {
                return std::make_shared<SharedElementEntry>(el, boundsAnimation, placeholder, renderOnlyWhenVisible, clip, renderInOverlay, &state, zIndex);
            });
            state.SetInternalState(e.get());
            e->sharedElement = el;
            e->renderOnlyWhenVisible = renderOnlyWhenVisible;
            e->boundsAnimation = boundsAnimation;
            e->placeholderSize = placeholder;
            e->overlayClip = clip;
            e->SetZIndex(zIndex);
            e->renderInOverlayDuringTransition = renderInOverlay;
            e->userState = &state;
            // onRemembered / onForgotten.
            SharedTransitionRoot* root = m_Root.get();
            auto keep = e;
            c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(keep.get())), [root, keep] {
                root->OnEntryAdded(keep.get());
                keep->sharedElement->InvalidateTargetBoundsProvider();
                return std::function<void()>([root, keep] {
                    root->OnEntryRemoved(keep.get());
                    keep->sharedElement->InvalidateTargetBoundsProvider();
                });
            });
            entry = e;
        }, state.Key());
        return m.With<BoundsNode>(BoundsParams{ entry });
    }

    // --- the layouts -------------------------------------------------------------------------------

    void WithSharedTransitionScope(std::function<void(SharedTransitionScope&, const Modifier&)> content, rt::Key key) {
        LookaheadScope([content = std::move(content)](ui::LookaheadScope& lookahead) {
            auto& c = *rt::Composition::Current();
            auto& scope = c.Remember<std::shared_ptr<SharedTransitionScope>>([&] { return std::make_shared<SharedTransitionScope>(lookahead); });
            const Modifier root = Modifier{}.With<RootNode>(RootParams{ &scope->Root() });
            content(*scope, root);
        }, key);
    }

    void SharedTransitionLayout(const Modifier& modifier, std::function<void(SharedTransitionScope&)> content, rt::Key key) {
        WithSharedTransitionScope([modifier, content = std::move(content)](SharedTransitionScope& scope, const Modifier& root) {
            // The scope's modifier after the caller's, so theirs size and clip the overlay too.
            Box(modifier.Then(root), Alignment::TopStart(), false, [&scope, content] { content(scope); });
        }, key);
    }

}
