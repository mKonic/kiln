#pragma once

// The machinery behind SharedTransition.h: one SharedElement per key, one SharedElementEntry
// per place that key is used, a state machine for the match, and the bounds animation.

#include "kiln/foundation/SharedTransition.h"
#include <kiln/Function.h>
#include "kiln/animation/Animatable.h"

namespace kiln::foundation::detail {

    using ui::Offset;
    using ui::Rect;
    using ui::Size;

    // TargetData: where the matched content goes. The target's structural offset (without
    // scrolling) plus its motion-frame-of-reference offset; the direct manipulation (scroll)
    // since the match was configured moves the animated bounds along.
    struct TargetData {
        rt::State<Size> size;
        rt::State<Offset> initialMfrOffset, targetStructuralOffset, currentMfrOffset;
        TargetData(Size s, Offset initialMfr, Offset structural) : size(s), initialMfrOffset(initialMfr), targetStructuralOffset(structural), currentMfrOffset(initialMfr) {}
        Rect TargetBounds() const {
            const Offset o = initialMfrOffset.Get() + targetStructuralOffset.Get();
            const Size s = size.Get();
            return { o.x, o.y, o.x + s.width, o.y + s.height };
        }
        Offset OffsetFromDirectManipulation(const Rect& animated) const {
            return Offset{ animated.left, animated.top } - initialMfrOffset.Get() + currentMfrOffset.Get();
        }
    };

    // BoundsProvider: the node that reports a place's bounds.
    class BoundsProvider {
    public:
        virtual ~BoundsProvider() = default;
        virtual std::optional<Rect> LastBoundsInSharedTransitionScope() const = 0;
        virtual std::optional<Rect> CalculateAlternativeTargetBounds(const Rect& targetBoundsBeforeDisposed) const = 0;
        // The deferred phase's transform state around the place (null outside one).
        virtual SharedTransformView* ModifierLocalTransformState() const { return nullptr; }
    };

    // The match's states (SharedTransitionStateMachine.State).
    class MatchState {
    public:
        virtual ~MatchState() = default;
        virtual TargetData* Target() const { return nullptr; }
        virtual std::optional<Rect> CurrentBounds() const { return std::nullopt; }
        virtual bool ActiveMatchFound() const { return false; }
        virtual bool MatchIsOrHasBeenConfigured() const { return false; }
        virtual void UpdateBounds(const Rect&) {}
        virtual std::shared_ptr<MatchState> OnMatchFound(BoundsProvider* previousTargetBoundsProvider) = 0;
        virtual std::shared_ptr<MatchState> OnVisibleContentRemovedDuringTransition() = 0;
        virtual std::optional<Rect> InitializeCurrentBounds(SharedElement&) { return CurrentBounds(); }
        virtual std::shared_ptr<MatchState> ConfigureActiveMatch(SharedElement&, BoundsProvider&, Size, Offset, Offset);
    };
    std::shared_ptr<MatchState> NoMatchFoundState();

    enum class StateChangeRequest : std::uint8_t { NoRequest, MatchFound, VisibleContentAbsentDuringTransition, NoMatchFound };

    class SharedTransitionStateMachine {
    public:
        explicit SharedTransitionStateMachine(SharedElement& e) : m_Element(e) {}
        bool ActiveMatchDeferred() const { return m_Request == StateChangeRequest::MatchFound; }
        const std::shared_ptr<MatchState>& State() const { return m_State.Get(); }
        void ResetState();
        void DeferRequest(StateChangeRequest r);
        void ProcessPendingRequest();
        void CheckForAndDeferStateUpdates(bool hasVisibleContent);
        void InvalidateTargetBoundsProvider();
        void UpdateTargetBoundsProvider();
        std::optional<Rect> TryInitializingCurrentBounds();
        void ConfigureActiveMatch(Size lookaheadSize, Offset topLeft, Offset structuralOffset);

    private:
        SharedElement& m_Element;
        rt::State<std::shared_ptr<MatchState>, rt::NeverEqual> m_State{ NoMatchFoundState() };
        int m_LastHandledRequestId = 0;
        rt::State<int> m_RequestId{ 0 };
        StateChangeRequest m_Request = StateChangeRequest::NoRequest;
        BoundsProvider* m_TargetBoundsProvider = nullptr;
        rt::State<int> m_TargetProviderUpdateRequestId{ 0 };
        int m_LastHandledTargetProviderUpdateRequestId = 0;
    };

    // BoundsAnimation: the bounds' deferred animation on the place's visibility transition.
    class BoundsAnimation {
    public:
        BoundsAnimation(SharedTransitionScope& scope, Transition<bool>& transition, DeferredAnimation<bool>& animation, BoundsTransform transform,
                        kiln::Function<Offset()> momentumOffset);
        void UpdateAnimation(DeferredAnimation<bool>& animation, BoundsTransform transform);
        // isRunning: the visibility transition is between states.
        bool IsRunning() const { return m_Transition->IsRunning() || !(m_Transition->CurrentState() == m_Transition->TargetState()); }
        // The animated bounds while a transition is active (null before it animated).
        std::optional<Rect> Value() const;
        void Animate(const Rect& currentBounds, const Rect& targetBounds, BoundsTransform forced = {}, std::optional<Rect> forcedInitialValue = std::nullopt,
                     std::optional<anim::Vec> forcedInitialVelocity = std::nullopt);
        bool Target() const { return m_Transition->TargetState(); }
        Transition<bool>& GetTransition() const { return *m_Transition; }
        bool HasAnimationState() const { return m_Started.Get(); }
        anim::SpecPtr Spec() const { return m_Spec; }

    private:
        SharedTransitionScope* m_Scope;
        Transition<bool>* m_Transition;
        DeferredAnimation<bool>* m_Animation;
        rt::State<bool> m_Started{ false };
        BoundsTransform m_Transform;
        anim::SpecPtr m_Spec;
        kiln::Function<Offset()> m_Momentum;
        Rect m_Current, m_TargetRect;
    };

    // LayerRenderer: something drawn in the overlay.
    class LayerRenderer {
    public:
        virtual ~LayerRenderer() = default;
        virtual SharedElementEntry* ParentState() const = 0;
        virtual layer::GraphicsLayer* Layer() const = 0;
        virtual void DrawInOverlay(ui::ContentDrawScope& scope) = 0;
        virtual float ZIndex() const = 0;
        virtual bool IsEntry() const { return false; }
    };

    // SharedElementEntry: one place a key is used.
    class SharedElementEntry final : public LayerRenderer {
    public:
        SharedElementEntry(SharedElement* element, std::shared_ptr<BoundsAnimation> bounds, PlaceholderSize placeholder, bool renderOnlyWhenVisible,
                           OverlayClipPtr clip, bool renderInOverlay, SharedContentState* userState, float zIndex);
        SharedElement* sharedElement;
        std::shared_ptr<BoundsAnimation> boundsAnimation;
        PlaceholderSize placeholderSize;
        bool renderOnlyWhenVisible;
        OverlayClipPtr overlayClip;
        bool renderInOverlayDuringTransition;
        SharedContentState* userState;
        rt::State<bool> isAttached{ false };
        BoundsProvider* boundsProvider = nullptr;
        std::optional<gfx::Path> clipPathInOverlay;
        layer::GraphicsLayerPtr layer;

        void SetZIndex(float z);
        float ZIndex() const override { return m_ZIndex.Get(); }
        SharedElementEntry* ParentState() const override { return m_Parent; }
        void SetParentState(SharedElementEntry* p);
        layer::GraphicsLayer* Layer() const override { return layer.get(); }
        bool IsEntry() const override { return true; }
        bool IsEnabled() const { return isAttached.Get() && userState->IsEnabledByUser(); }
        bool Target() const { return boundsAnimation->Target(); }
        std::optional<bool> PendingTarget() const { return boundsAnimation->GetTransition().PendingTargetState(); }
        bool IsDeferredEntering() const { const auto p = PendingTarget(); return !Target() && p && *p; }
        bool IsDeferredExiting() const { const auto p = PendingTarget(); return Target() && p && !*p; }
        bool ShouldApplyContainerTransform() const;
        bool ShouldSeekDuringDeferredTransition() const;
        // The transform state that moves this content in a deferred phase (its container's, or
        // while entering, the exiting content's container's).
        SharedTransformView* ActiveMutableTransformState();
        bool hasHandoffOccurred = false;
        std::optional<Rect> CalculateTargetBounds(const Rect& previousTargetBoundsBeforeLosingTarget) const;
        bool ShouldRenderAtAll() const;
        bool ShouldRenderInOverlay() const;
        bool ShouldRenderInPlace() const;
        void DrawInOverlay(ui::ContentDrawScope& scope) override;
        void ObservationBlock() const;
        void UpdateTransitionActiveness();
        void OnEntryRemoved();

    private:
        rt::State<float> m_ZIndex;
        SharedElementEntry* m_Parent = nullptr;
        bool m_TransitionActive = false;
        SharedTransformView* m_DeferredTransformState = nullptr;
    };

    class SharedElement {
    public:
        SharedElement(rt::Key key, SharedTransitionScope& scope);
        rt::Key key;
        SharedTransitionScope& scope;
        const std::shared_ptr<MatchState>& State() const { return m_Machine.State(); }
        const std::vector<SharedElementEntry*>& EnabledEntries() const { return m_Enabled.Get(); }
        const std::vector<SharedElementEntry*>& AllEntries() const { return m_All; }
        bool IsAnimating() const;
        void UpdateMatch();
        void UpdateExitVelocity(Offset velocity);
        Offset MomentumAnimationOffset();
        void InvalidateTargetBoundsProvider() { m_Machine.InvalidateTargetBoundsProvider(); }
        std::optional<Rect> TryInitializingCurrentBounds() { return m_Machine.TryInitializingCurrentBounds(); }
        void OnSharedTransitionFinished();
        const std::optional<Rect>& PendingTargetBounds() const { return m_PendingTargetBounds.Get(); }
        SharedElementEntry* PendingTargetEntry() const;
        bool FoundMatch() const;
        bool BoundsTransformIsActive() const { return State()->MatchIsOrHasBeenConfigured(); }
        void OnLookaheadPlaced(ui::PlacementScope& scope, SharedElementEntry& entry);
        void AddEntry(SharedElementEntry* e);
        void RemoveEntry(SharedElementEntry* e);

    private:
        SharedTransitionStateMachine m_Machine;
        std::vector<SharedElementEntry*> m_All;
        rt::State<std::vector<SharedElementEntry*>> m_Enabled{ {} };
        anim::Animatable m_Momentum{ anim::Vec{ 0, 0, 0, 0 }, 2 };
        bool m_AnimationSpecFinalized = false;
        rt::State<std::optional<Rect>> m_PendingTargetBounds{ std::nullopt };
        rt::Job m_MomentumJob;
    };

    // The scope's internals (SharedTransitionScopeImpl's state).
    class SharedTransitionRoot {
    public:
        explicit SharedTransitionRoot(SharedTransitionScope& s) : scope(s) {}
        SharedTransitionScope& scope;
        kiln::Function<void()> invalidateOverlay;
        std::optional<ui::LayoutCoordinates> root, lookaheadRoot;
        std::shared_ptr<SharedElement> SharedElementsFor(const rt::Key& key);
        void OnNodeTransitionActivenessChanged(bool active);
        void DrawInOverlay(ui::ContentDrawScope& s);
        void OnEntryAdded(SharedElementEntry* e);
        void OnEntryRemoved(SharedElementEntry* e);
        void OnLayerRendererCreated(LayerRenderer* r) { m_Renderers.push_back(r); ZOrderChanged(); }
        void OnLayerRendererRemoved(LayerRenderer* r) { std::erase(m_Renderers, r); ZOrderChanged(); }
        void ZOrderChanged() { m_Version.Set(m_Version.Peek() + 1); }

    private:
        std::vector<LayerRenderer*> m_Renderers;
        rt::State<int> m_Version{ 0 };
        int m_LastSorted = -1;
        std::map<std::uint64_t, std::shared_ptr<SharedElement>> m_Elements;
        int m_ActiveCount = 0;
        std::vector<rt::Job> m_Removals;
    };

    // ModifierLocalSharedElementInternalState: the shared content around a node.
    inline ui::ModifierLocal<SharedElementEntry*> LocalSharedElementEntry{ [] { return static_cast<SharedElementEntry*>(nullptr); } };

}
