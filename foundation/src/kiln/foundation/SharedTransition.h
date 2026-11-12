#pragma once

// Shared element transitions (animation SharedTransitionScope.kt, SharedElement.kt,
// SharedElementEntry.kt, SharedTransitionStateMachine.kt, SharedContentNode.kt,
// BoundsAnimation.kt, SkipToLookaheadSizeNode.kt, RenderInTransitionOverlayNodeElement.kt).
//
// Inside a SharedTransitionLayout (a LookaheadScope), content marked sharedElement or
// sharedBounds with the same key in two places that swap visibility (an AnimatedVisibility, an
// AnimatedContent) is matched: while the transition runs, the bounds animate from where the
// leaving one was to where the arriving one settles (the lookahead pass says where), and the
// content is drawn in the layout's overlay, above everything else, through a graphics layer.

#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/GraphicsLayer.h"
#include "kiln/foundation/Image.h"
#include "kiln/foundation/Transition.h"
#include "kiln/graphics/Shape.h"
#include "kiln/ui/Layout.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::foundation {

    class SharedTransitionScope;
    class SharedContentState;
    namespace detail { class SharedElement; class SharedElementEntry; class LayerRenderer; class SharedTransitionRoot; }

    // BoundsTransform: the spec the bounds animate with, from the initial and target bounds.
    using BoundsTransform = std::function<anim::SpecPtr(const ui::Rect& initialBounds, const ui::Rect& targetBounds)>;

    // PlaceholderSize: the size the layout reports while its content animates (the content's
    // own lookahead size, or the animated size).
    struct PlaceholderSize {
        std::function<IntSize(IntSize contentSize, IntSize animatedSize)> calculate;
        IntSize CalculateSize(IntSize contentSize, IntSize animatedSize) const { return calculate ? calculate(contentSize, animatedSize) : contentSize; }
        static PlaceholderSize AnimatedSize() { return { [](IntSize, IntSize a) { return a; } }; }
        static PlaceholderSize ContentSize() { return { [](IntSize c, IntSize) { return c; } }; }
    };

    // ResizeMode: how sharedBounds content fits the animated bounds: remeasured to them each
    // frame, or measured once at its own (lookahead) size and scaled into them.
    struct ResizeMode {
        enum class Kind : std::uint8_t { RemeasureToBounds, ScaleToBounds };
        Kind kind = Kind::RemeasureToBounds;
        ContentScale contentScale = ContentScale::FillWidth();
        Alignment alignment = Alignment::Center();
        static ResizeMode RemeasureToBounds() { return {}; }
        static ResizeMode ScaleToBounds(ContentScale contentScale = ContentScale::FillWidth(), Alignment alignment = Alignment::Center()) {
            return { Kind::ScaleToBounds, contentScale, alignment };
        }
    };

    // OverlayClip: the clip of content drawn in the overlay (by default its parent shared
    // content's clip, so a child stays inside its animating parent).
    class OverlayClip {
    public:
        virtual ~OverlayClip() = default;
        virtual std::optional<gfx::Path> GetClipPath(const SharedContentState& state, const ui::Rect& bounds, ui::LayoutDirection direction,
                                                     const ui::Density& density) const = 0;
    };
    using OverlayClipPtr = std::shared_ptr<const OverlayClip>;

    // DeferredTransitionBehavior: what shared content does while its container is moved by hand
    // in a deferred phase (DeferredAnimatedContent): follow the container's transform, stay put,
    // or seek towards its target bounds by the phase's shared element progress.
    enum class DeferredTransitionBehavior : std::uint8_t { FollowContainer, Static, SeekToTarget };

    // SharedContentConfig: whether a state takes part, and where it goes when its target leaves
    // mid-transition.
    struct SharedContentConfig {
        std::function<bool(const SharedContentState&)> isEnabled;   // unset: always
        bool shouldKeepEnabledForOngoingAnimation = true;
        DeferredTransitionBehavior deferredTransitionBehavior = DeferredTransitionBehavior::FollowContainer;
        // alternativeTargetBoundsInTransitionScopeAfterRemoval: new target bounds (in the
        // layout) once the visible target left during the transition; null ends the match.
        std::function<std::optional<ui::Rect>(const SharedContentState&, const ui::Rect& targetBoundsBeforeRemoval, ui::Size layoutSize)>
            alternativeTargetBoundsAfterRemoval;
    };

    // SharedContentState: the key shared content matches by, and what is known about the match.
    class SharedContentState {
    public:
        SharedContentState(rt::Key key, SharedContentConfig config) : m_Key(key), m_Config(std::move(config)) {}
        const rt::Key& Key() const { return m_Key; }
        // isMatchFound: content with the same key is visible elsewhere and the bounds transform.
        bool IsMatchFound() const;
        // prepareTransitionWithInitialVelocity: the leaving content's velocity (a fling that
        // dismissed it), carried into the bounds animation as momentum.
        void PrepareTransitionWithInitialVelocity(ui::Offset velocity);
        // The clip it is drawn with in the overlay (null: none).
        std::optional<gfx::Path> ClipPathInOverlay() const;
        // The shared content state of the shared content around this one (null: none).
        SharedContentState* ParentSharedContentState() const;
        // internal
        const SharedContentConfig& Config() const { return m_Config.Get(); }
        void SetConfig(SharedContentConfig c) { m_Config.Set(std::move(c)); }
        bool IsEnabledByUser() const;
        detail::SharedElementEntry* InternalState() const { return m_Internal.Get(); }
        void SetInternalState(detail::SharedElementEntry* e) { if (m_Internal.Peek() != e) m_Internal.Set(e); }

    private:
        rt::Key m_Key;
        rt::State<SharedContentConfig, rt::NeverEqual> m_Config;
        rt::State<detail::SharedElementEntry*> m_Internal{ nullptr };
    };

    // SharedTransitionScope: the scope of a SharedTransitionLayout's content.
    class SharedTransitionScope {
    public:
        explicit SharedTransitionScope(ui::LookaheadScope& lookahead);
        ~SharedTransitionScope();
        SharedTransitionScope(const SharedTransitionScope&) = delete;
        SharedTransitionScope& operator=(const SharedTransitionScope&) = delete;

        // isTransitionActive: some matched shared content is animating (observable).
        bool IsTransitionActive() const { return m_Active.Get(); }
        ui::LookaheadScope& Lookahead() const { return *m_Lookahead; }

        // skipToLookaheadSize: measured at its lookahead (target) size while `enabled` (by
        // default while a transition runs), not at each animated size.
        Modifier SkipToLookaheadSize(Modifier m, std::function<bool()> enabled = {});
        // skipToLookaheadPosition: placed straight at its lookahead position while `enabled`.
        Modifier SkipToLookaheadPosition(Modifier m, std::function<bool()> enabled = {});
        // renderInSharedTransitionScopeOverlay: drawn in the overlay (above the shared content
        // at zIndexInOverlay) while `renderInOverlay` (by default while a transition runs).
        Modifier RenderInSharedTransitionScopeOverlay(Modifier m, float zIndexInOverlay = 0, std::function<bool()> renderInOverlay = {});

        // sharedElement: the same content in both places; only the visible one is drawn, at the
        // animated bounds.
        Modifier SharedElement(Modifier m, SharedContentState& state, const AnimatedVisibilityScope& animatedVisibilityScope,
                               BoundsTransform boundsTransform = {}, PlaceholderSize placeholderSize = PlaceholderSize::ContentSize(),
                               bool renderInOverlayDuringTransition = true, float zIndexInOverlay = 0, OverlayClipPtr clipInOverlayDuringTransition = nullptr);
        // sharedBounds: different content sharing its bounds; both are drawn in the animating
        // bounds, the leaving one exiting and the arriving one entering.
        Modifier SharedBounds(Modifier m, SharedContentState& state, const AnimatedVisibilityScope& animatedVisibilityScope,
                              std::optional<EnterTransition> enter = std::nullopt, std::optional<ExitTransition> exit = std::nullopt,
                              BoundsTransform boundsTransform = {}, std::optional<ResizeMode> resizeMode = std::nullopt,
                              PlaceholderSize placeholderSize = PlaceholderSize::ContentSize(), bool renderInOverlayDuringTransition = true,
                              float zIndexInOverlay = 0, OverlayClipPtr clipInOverlayDuringTransition = nullptr);
        // sharedElementWithCallerManagedVisibility: as sharedElement, visible as the caller says.
        Modifier SharedElementWithCallerManagedVisibility(Modifier m, SharedContentState& state, bool visible, BoundsTransform boundsTransform = {},
                                                          PlaceholderSize placeholderSize = PlaceholderSize::ContentSize(),
                                                          bool renderInOverlayDuringTransition = true, float zIndexInOverlay = 0,
                                                          OverlayClipPtr clipInOverlayDuringTransition = nullptr);
        // OverlayClip(shape): clips the overlay drawing to `shape` at the animated bounds.
        OverlayClipPtr MakeOverlayClip(std::shared_ptr<const gfx::Shape> clipShape) const;
        // rememberSharedContentState(key, config).
        SharedContentState& RememberSharedContentState(rt::Key key, std::optional<SharedContentConfig> config = std::nullopt);
        // SharedContentConfig(deferredTransitionBehavior).
        static SharedContentConfig MakeSharedContentConfig(DeferredTransitionBehavior behavior = DeferredTransitionBehavior::FollowContainer);

        // internal
        detail::SharedTransitionRoot& Root() { return *m_Root; }

    private:
        friend class detail::SharedTransitionRoot;
        friend class detail::SharedElement;
        friend class detail::SharedElementEntry;
        Modifier SharedBoundsImpl(Modifier m, SharedContentState& state, Transition<EnterExitState>* parent, bool callerVisible,
                                  BoundsTransform boundsTransform, PlaceholderSize placeholderSize, bool renderOnlyWhenVisible,
                                  bool renderInOverlay, float zIndex, OverlayClipPtr clip);
        ui::LookaheadScope* m_Lookahead;
        rt::State<bool> m_Active{ false };
        std::unique_ptr<detail::SharedTransitionRoot> m_Root;
    };

    // SharedTransitionDefaults.
    struct SharedTransitionDefaults {
        static foundation::EnterTransition EnterTransition() { return FadeIn(); }
        static foundation::ExitTransition ExitTransition() { return FadeOut(); }
        // spring(StiffnessMediumLow, 1 px threshold).
        static foundation::BoundsTransform BoundsTransform();
        static foundation::ResizeMode ResizeMode() { return ResizeMode::ScaleToBounds(ContentScale::FillWidth(), Alignment::Center()); }
        static foundation::SharedContentConfig SharedContentConfig() { return SharedTransitionScope::MakeSharedContentConfig(); }
    };

    // SharedTransitionLayout: a Box whose content can share elements.
    void SharedTransitionLayout(const Modifier& modifier, std::function<void(SharedTransitionScope&)> content, rt::Key key = {});
    // SharedTransitionScope { modifier -> }: as SharedTransitionLayout, but the content puts the
    // given modifier on its own root layout (which then hosts the overlay).
    void WithSharedTransitionScope(std::function<void(SharedTransitionScope&, const Modifier&)> content, rt::Key key = {});

}
