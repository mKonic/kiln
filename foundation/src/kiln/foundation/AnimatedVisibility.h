#pragma once

// Enter/exit transitions and AnimatedVisibility / Crossfade / AnimatedContent (compose
// animation: EnterExitTransition.kt, AnimatedVisibility.kt, Crossfade.kt, AnimatedContent.kt).
// Defaults: fade / scale / slide / expand-shrink use spring(StiffnessMediumLow); AnimatedVisibility
// enters with fadeIn + expandIn and exits with shrinkOut + fadeOut; content stays composed until
// its exit finishes.

#include "kiln/animation/Spec.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Transition.h"

#include <functional>
#include <optional>

namespace kiln::foundation {

    using ui::IntOffset;
    using ui::IntSize;

    // The parts of an enter or exit transition (FadeConfig, ScaleConfig, SlideConfig,
    // ChangeSizeConfig, VeilConfig and EnterExitTransitionConfig); each value is the start (enter)
    // or end (exit) of its animation.
    struct FadeConfig { float alpha; anim::SpecPtr spec; };
    struct ScaleConfig { float scale; float originX = 0.5f, originY = 0.5f; anim::SpecPtr spec; };
    struct SlideConfig { std::function<IntOffset(IntSize)> offset; anim::SpecPtr spec; };
    struct ChangeSizeConfig { Alignment alignment; std::function<IntSize(IntSize)> size; bool clip = true; anim::SpecPtr spec; };
    // A scrim over the content; with matchParentSize it is drawn apart from the other transforms.
    struct VeilConfig { ui::Color initialColor, targetColor; anim::SpecPtr spec; bool matchParentSize = false; };
    struct EnterExitTransitionConfig {
        std::optional<FadeConfig> fade;
        std::optional<ScaleConfig> scale;
        std::optional<SlideConfig> slide;
        std::optional<ChangeSizeConfig> changeSize;
        std::optional<VeilConfig> veil;
    };

    // EnterTransition / ExitTransition: combine with + (the right one wins per kind).
    struct EnterTransition {
        EnterExitTransitionConfig config;
        EnterTransition operator+(const EnterTransition& o) const;
    };
    struct ExitTransition {
        EnterExitTransitionConfig config;
        ExitTransition operator+(const ExitTransition& o) const;
    };

    anim::SpecPtr DefaultEnterExitSpring();
    // spring(stiffness = MediumLow, visibilityThreshold = 1 px): slides' and size changes' default.
    anim::SpecPtr DefaultSpatialEnterExitSpring();
    EnterTransition FadeIn(anim::SpecPtr spec = DefaultEnterExitSpring(), float initialAlpha = 0);
    ExitTransition FadeOut(anim::SpecPtr spec = DefaultEnterExitSpring(), float targetAlpha = 0);
    EnterTransition ScaleIn(anim::SpecPtr spec = DefaultEnterExitSpring(), float initialScale = 0, float originX = 0.5f, float originY = 0.5f);
    ExitTransition ScaleOut(anim::SpecPtr spec = DefaultEnterExitSpring(), float targetScale = 0, float originX = 0.5f, float originY = 0.5f);
    EnterTransition SlideIn(std::function<IntOffset(IntSize)> initialOffset, anim::SpecPtr spec = DefaultSpatialEnterExitSpring());
    ExitTransition SlideOut(std::function<IntOffset(IntSize)> targetOffset, anim::SpecPtr spec = DefaultSpatialEnterExitSpring());
    EnterTransition SlideInVertically(std::function<int(int)> initialOffsetY = [](int h) { return -h / 2; }, anim::SpecPtr spec = DefaultSpatialEnterExitSpring());
    ExitTransition SlideOutVertically(std::function<int(int)> targetOffsetY = [](int h) { return -h / 2; }, anim::SpecPtr spec = DefaultSpatialEnterExitSpring());
    EnterTransition SlideInHorizontally(std::function<int(int)> initialOffsetX = [](int w) { return -w / 2; }, anim::SpecPtr spec = DefaultSpatialEnterExitSpring());
    ExitTransition SlideOutHorizontally(std::function<int(int)> targetOffsetX = [](int w) { return -w / 2; }, anim::SpecPtr spec = DefaultSpatialEnterExitSpring());
    EnterTransition ExpandIn(anim::SpecPtr spec = DefaultSpatialEnterExitSpring(), Alignment expandFrom = Alignment::BottomEnd(),
                             bool clip = true, std::function<IntSize(IntSize)> initialSize = [](IntSize) { return IntSize{ 0, 0 }; });
    ExitTransition ShrinkOut(anim::SpecPtr spec = DefaultSpatialEnterExitSpring(), Alignment shrinkTowards = Alignment::BottomEnd(),
                             bool clip = true, std::function<IntSize(IntSize)> targetSize = [](IntSize) { return IntSize{ 0, 0 }; });
    EnterTransition ExpandVertically(anim::SpecPtr spec = DefaultSpatialEnterExitSpring(), AlignmentV expandFrom = AlignmentV::Bottom(),
                                     bool clip = true, std::function<int(int)> initialHeight = [](int) { return 0; });
    ExitTransition ShrinkVertically(anim::SpecPtr spec = DefaultSpatialEnterExitSpring(), AlignmentV shrinkTowards = AlignmentV::Bottom(),
                                    bool clip = true, std::function<int(int)> targetHeight = [](int) { return 0; });
    EnterTransition ExpandHorizontally(anim::SpecPtr spec = DefaultSpatialEnterExitSpring(), AlignmentH expandFrom = AlignmentH::End(),
                                       bool clip = true, std::function<int(int)> initialWidth = [](int) { return 0; });
    ExitTransition ShrinkHorizontally(anim::SpecPtr spec = DefaultSpatialEnterExitSpring(), AlignmentH shrinkTowards = AlignmentH::End(),
                                      bool clip = true, std::function<int(int)> targetWidth = [](int) { return 0; });

    // unveilIn: a scrim (Black at 50% by default) that clears as the content enters; veilOut:
    // one that darkens over the content as it exits.
    EnterTransition UnveilIn(anim::SpecPtr spec = DefaultEnterExitSpring(), ui::Color initialColor = ui::Color(0x80000000u), bool matchParentSize = false);
    ExitTransition VeilOut(anim::SpecPtr spec = DefaultEnterExitSpring(), ui::Color targetColor = ui::Color(0x80000000u), bool matchParentSize = false);

    // --- The deferred phase (DeferredEnterExitTransition.kt) ------------------------------------

    // TransformScope: what a deferred phase sets by hand on the content each frame. A property it
    // does not set stays at rest (alpha 1, scale 1, no offset, centred origin, no veil).
    class TransformScope {
    public:
        float Alpha() const { return m_Alpha.Get(); }
        void SetAlpha(float v) { m_Alpha.Set(v); m_AlphaMutated.Set(true); }
        float Scale() const { return m_Scale.Get(); }
        void SetScale(float v) { m_Scale.Set(v); m_ScaleMutated.Set(true); }
        TransformOrigin GetTransformOrigin() const { return m_Origin.Get(); }
        void SetTransformOrigin(TransformOrigin v) { m_Origin.Set(v); m_OriginMutated.Set(true); }
        IntOffset Offset() const { return m_Offset; }
        void SetOffset(IntOffset v) { m_Offset = v; m_OffsetMutated = true; }
        ui::Color Veil() const { return m_Veil.Get(); }
        void SetVeil(ui::Color v) { m_Veil.Set(v); m_VeilMutated.Set(true); }

        // internal
        bool AlphaMutated() const { return m_AlphaMutated.Get(); }
        bool ScaleMutated() const { return m_ScaleMutated.Get(); }
        bool OriginMutated() const { return m_OriginMutated.Get(); }
        bool OffsetMutated() const { return m_OffsetMutated; }
        bool VeilMutated() const { return m_VeilMutated.Get(); }
        void Reset();

    private:
        rt::State<float> m_Alpha{ 1 }, m_Scale{ 1 };
        rt::State<TransformOrigin> m_Origin{ TransformOrigin::Center() };
        rt::State<ui::Color> m_Veil{ ui::Color::Transparent() };
        rt::State<bool> m_AlphaMutated{ false }, m_ScaleMutated{ false }, m_OriginMutated{ false }, m_VeilMutated{ false };
        IntOffset m_Offset{};
        bool m_OffsetMutated = false;
    };

    // MutableTransform: the hand-set transform of one content during a deferred phase. The block
    // runs in layout each frame (read gesture state there); its values are handed to the
    // automatic transition when the phase ends. offsetVelocityProvider (px/s), read once then:
    // unset, the velocity is tracked from the offsets set.
    struct MutableTransform {
        bool veilMatchParentSize = false;
        std::function<ui::Offset()> offsetVelocityProvider;
        std::function<void(TransformScope&, IntSize fullSize)> block;
        std::function<float()> sharedElementProgressProvider;
        void Update(std::function<void(TransformScope&, IntSize fullSize)> b) { block = std::move(b); }
        void Clear() { block = {}; veilMatchParentSize = false; offsetVelocityProvider = {}; sharedElementProgressProvider = {}; }
    };

    // MutableContentTransform: the exiting (initial) and entering (target) contents' transforms
    // for DeferredAnimatedContent's deferred phase.
    struct MutableContentTransform {
        std::shared_ptr<MutableTransform> initialTransform = std::make_shared<MutableTransform>();
        std::shared_ptr<MutableTransform> targetTransform = std::make_shared<MutableTransform>();
        void InitialContentTransform(std::function<void(TransformScope&, IntSize)> block) { initialTransform->Update(std::move(block)); }
        void TargetContentTransform(std::function<void(TransformScope&, IntSize)> block) { targetTransform->Update(std::move(block)); }
        // Shared elements' interpolation progress during the phase (0..1).
        void SharedElementProgress(std::function<float()> progress) {
            initialTransform->sharedElementProgressProvider = progress;
            targetTransform->sharedElementProgressProvider = std::move(progress);
        }
    };
    std::shared_ptr<MutableContentTransform> MakeMutableContentTransform(bool initialVeilMatchParentSize = false, bool targetVeilMatchParentSize = false,
                                                                         std::function<ui::Offset()> initialOffsetVelocityProvider = {},
                                                                         std::function<ui::Offset()> targetOffsetVelocityProvider = {},
                                                                         std::function<void(MutableContentTransform&)> block = {});

    // EnterExitState: where AnimatedVisibility's content is in its life.
    enum class EnterExitState : std::uint8_t { PreEnter, Visible, PostExit };

    // AnimatedVisibilityScope: what AnimatedVisibility (and AnimatedEnterExit, AnimatedContent's
    // contents) give their content: the enter/exit transition, to animate on (Transition.animate*)
    // or to run AnimateEnterExit with. Read with LocalAnimatedVisibilityScope.
    struct AnimatedVisibilityScope {
        Transition<EnterExitState>* transition = nullptr;
    };
    inline rt::Local<AnimatedVisibilityScope*> LocalAnimatedVisibilityScope{ nullptr };

    // Modifier.animateEnterExit (as a wrapper): `content` enters and exits with the
    // AnimatedVisibility around it, by its own transitions (a child that slides while the
    // whole fades).
    void AnimateEnterExit(const Modifier& modifier, std::function<void()> content, EnterTransition enter = FadeIn(), ExitTransition exit = FadeOut(),
                          rt::Key key = {});

    void AnimatedVisibility(bool visible, const Modifier& modifier, std::function<void()> content,
                            EnterTransition enter = FadeIn() + ExpandIn(), ExitTransition exit = ShrinkOut() + FadeOut(), rt::Key key = {});

    // AnimatedVisibility(visibleState): driven by a MutableTransitionState (start invisible and
    // set the target to animate in on first composition; IsIdle once done).
    void AnimatedVisibility(MutableTransitionState<bool>& visibleState, const Modifier& modifier, std::function<void()> content,
                            EnterTransition enter = FadeIn() + ExpandIn(), ExitTransition exit = FadeOut() + ShrinkOut(), rt::Key key = {});
    // RowScope / ColumnScope.AnimatedVisibility: the defaults expand and shrink along the
    // layout's main axis only.
    void AnimatedVisibilityInRow(bool visible, const Modifier& modifier, std::function<void()> content,
                                 EnterTransition enter = FadeIn() + ExpandHorizontally(),
                                 ExitTransition exit = FadeOut() + ShrinkHorizontally(), rt::Key key = {});
    void AnimatedVisibilityInRow(MutableTransitionState<bool>& visibleState, const Modifier& modifier, std::function<void()> content,
                                 EnterTransition enter = ExpandHorizontally() + FadeIn(),
                                 ExitTransition exit = ShrinkHorizontally() + FadeOut(), rt::Key key = {});
    void AnimatedVisibilityInColumn(bool visible, const Modifier& modifier, std::function<void()> content,
                                    EnterTransition enter = FadeIn() + ExpandVertically(),
                                    ExitTransition exit = FadeOut() + ShrinkVertically(), rt::Key key = {});
    void AnimatedVisibilityInColumn(MutableTransitionState<bool>& visibleState, const Modifier& modifier, std::function<void()> content,
                                    EnterTransition enter = ExpandVertically() + FadeIn(),
                                    ExitTransition exit = ShrinkVertically() + FadeOut(), rt::Key key = {});

    // AnimatedEnterExit: content that enters while `visibility` goes to true and exits (then
    // leaves the composition) while it goes to false.
    // In a deferred phase (the transition has a pending target) mutableTransform moves the
    // content by hand; forceVisible keeps it composed after its exit (interrupted content).
    void AnimatedEnterExit(Transition<bool>& visibility, const Modifier& modifier, std::function<void()> content,
                           const EnterTransition& enter, const ExitTransition& exit, rt::Key key = {},
                           std::shared_ptr<MutableTransform> mutableTransform = nullptr, bool forceVisible = false);

    namespace detail {
        // AnimatedVisibilityImpl's layout: in a lookahead pass, content on its way out takes no
        // space (where the layout ends up) while the main pass animates it away.
        Modifier LookaheadHide(const Modifier& m, const Transition<bool>& visibility);
    }

    // Transition.AnimatedVisibility(visible): visibility derived from a parent transition's
    // state, animated on the parent's clock (a child transition).
    template<typename S>
    void AnimatedVisibility(Transition<S>& transition, std::function<bool(const S&)> visible, const Modifier& modifier,
                            std::function<void()> content, EnterTransition enter = FadeIn() + ExpandIn(),
                            ExitTransition exit = ShrinkOut() + FadeOut(), rt::Key key = {}) {
        auto& child = CreateChildTransition<bool, S>(transition, std::move(visible), key);
        AnimatedEnterExit(child, detail::LookaheadHide(modifier, child), std::move(content), enter, exit, key);
    }

    // DeferredTransition.DeferredAnimatedVisibility(visible): as above on a deferred transition
    // (RememberDeferredTransition); while a target is pending, mutableTransform moves the content.
    template<typename S>
    void DeferredAnimatedVisibility(Transition<S>& transition, std::function<bool(const S&)> visible, const Modifier& modifier,
                                    std::function<void()> content, EnterTransition enter = FadeIn() + ExpandIn(),
                                    ExitTransition exit = ShrinkOut() + FadeOut(), std::shared_ptr<MutableTransform> mutableTransform = nullptr,
                                    rt::Key key = {}) {
        auto& child = CreateChildTransition<bool, S>(transition, std::move(visible), key);
        AnimatedEnterExit(child, detail::LookaheadHide(modifier, child), std::move(content), enter, exit, key, std::move(mutableTransform));
    }

    // SizeTransform: how the container's size follows the content (clipped or not), with a spec
    // chosen from the initial and target sizes.
    struct SizeTransform {
        bool clip = true;
        std::function<anim::SpecPtr(IntSize initial, IntSize target)> spec;   // null: spring(MediumLow)
    };

    // ContentTransform (enter togetherWith exit, targetContentZIndex, using SizeTransform).
    struct ContentTransform {
        EnterTransition enter;
        ExitTransition exit;
        float targetContentZIndex = 0;      // the incoming content's draw order among the contents
        std::optional<SizeTransform> sizeTransform = SizeTransform{};   // none: size snaps
    };
    // enter togetherWith exit
    inline ContentTransform TogetherWith(EnterTransition enter, ExitTransition exit) { return { std::move(enter), std::move(exit) }; }
    // ContentTransform using sizeTransform
    inline ContentTransform Using(ContentTransform t, std::optional<SizeTransform> size) { t.sizeTransform = std::move(size); return t; }

    // AnimatedContentTransitionScope: what a transitionSpec sees, and its slide helpers.
    class AnimatedContentTransitionScope {
    public:
        enum class SlideDirection : std::uint8_t { Left, Right, Up, Down, Start, End };
        rt::Key initialState, targetState;
        Alignment contentAlignment = Alignment::TopStart();
        ui::LayoutDirection layoutDirection = ui::LayoutDirection::Ltr;
        // slideIntoContainer: the new content slides in from the container's edge towards
        // `towards` (initialOffset maps the full-slide offset; identity by default).
        EnterTransition SlideIntoContainer(SlideDirection towards, anim::SpecPtr spec = nullptr,
                                           std::function<int(int)> initialOffset = {}) const;
        // slideOutOfContainer: the old content slides out past the edge towards `towards`.
        ExitTransition SlideOutOfContainer(SlideDirection towards, anim::SpecPtr spec = nullptr,
                                           std::function<int(int)> targetOffset = {}) const;

        // internal: sizes measured on the last pass.
        struct Sizes { IntSize container; IntSize target; };
        std::shared_ptr<Sizes> sizes = std::make_shared<Sizes>();
    };

    // AnimatedContent's default: fadeIn(tween(220, 90)) + scaleIn(0.92, tween(220, 90)) togetherWith
    // fadeOut(tween(90)) (the Material fade-through numbers).
    ContentTransform DefaultContentTransform();
    // AnimatedContentDefaults: the size spec (spring(MediumLow, 1 px threshold)), SizeTransform
    // and ContentTransform AnimatedContent uses when given none.
    struct AnimatedContentDefaults {
        static anim::SpecPtr SizeAnimationSpec();
        static foundation::SizeTransform SizeTransform() { return {}; }
        static foundation::ContentTransform ContentTransform() { return DefaultContentTransform(); }
    };
    // AnimatedContent(targetState): content keyed by `target`; the old content leaves with the
    // exit while the new one enters, both stacked at contentAlignment.
    void AnimatedContent(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform(AnimatedContentTransitionScope&)> transitionSpec,
                         Alignment contentAlignment = Alignment::TopStart(), rt::Key key = {});
    void AnimatedContent(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform()> transform = DefaultContentTransform,
                         Alignment contentAlignment = Alignment::TopStart(), rt::Key key = {});
    // DeferredAnimatedContent: AnimatedContent over a deferred transition of content keys
    // (RememberDeferredTransition of a DeferredTransitionState<std::uint64_t>). While a target is
    // pending its content composes before entering, and mutableTransformSpec (seeing the
    // current target as initial and the pending one as target) moves the leaving and entering
    // contents by hand; when the automatic transition starts, the hand-set values are handed
    // to transitionSpec's animations. Up to three interrupted contents stay composed.
    void DeferredAnimatedContent(Transition<std::uint64_t>& transition, const Modifier& modifier, std::function<void(rt::Key)> content,
                                 std::function<ContentTransform(AnimatedContentTransitionScope&)> transitionSpec = {},
                                 Alignment contentAlignment = Alignment::TopStart(),
                                 std::function<std::shared_ptr<MutableContentTransform>(AnimatedContentTransitionScope&)> mutableTransformSpec = {},
                                 rt::Key key = {});
    // Transition.AnimatedContent(transitionSpec, contentAlignment, contentKey): contents keyed by
    // the transition's states, animated on its clock (a seekable transition scrubs them).
    void AnimatedContent(Transition<std::uint64_t>& transition, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform(AnimatedContentTransitionScope&)> transitionSpec = {},
                         Alignment contentAlignment = Alignment::TopStart(), rt::Key key = {});
    // animateContentSize(spring): the size follows content size changes with an animation.
    Modifier AnimateContentSize(Modifier m, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 0.5f));

    // Crossfade(targetState, animationSpec = tween()): the old content fades out while the new one
    // fades in, both kept until the fade ends. The key identifies the content.
    void Crossfade(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                   anim::SpecPtr spec = anim::Tween(), rt::Key key = {});

}
