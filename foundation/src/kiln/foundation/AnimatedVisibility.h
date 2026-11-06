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

    struct EnterExitParts {
        std::optional<std::pair<float, anim::SpecPtr>> fade;                          // alpha
        struct Scale { float scale; float originX = 0.5f, originY = 0.5f; anim::SpecPtr spec; };
        std::optional<Scale> scale;
        struct Slide { std::function<IntOffset(IntSize)> offset; anim::SpecPtr spec; };
        std::optional<Slide> slide;
        struct ChangeSize { Alignment alignment; std::function<IntSize(IntSize)> size; bool clip = true; anim::SpecPtr spec; };
        std::optional<ChangeSize> changeSize;
    };

    // EnterTransition / ExitTransition: combine with + (the left one wins per kind).
    struct EnterTransition {
        EnterExitParts parts;
        EnterTransition operator+(const EnterTransition& o) const;
    };
    struct ExitTransition {
        EnterExitParts parts;
        ExitTransition operator+(const ExitTransition& o) const;
    };

    anim::SpecPtr DefaultEnterExitSpring();
    EnterTransition FadeIn(anim::SpecPtr spec = DefaultEnterExitSpring(), float initialAlpha = 0);
    ExitTransition FadeOut(anim::SpecPtr spec = DefaultEnterExitSpring(), float targetAlpha = 0);
    EnterTransition ScaleIn(anim::SpecPtr spec = DefaultEnterExitSpring(), float initialScale = 0, float originX = 0.5f, float originY = 0.5f);
    ExitTransition ScaleOut(anim::SpecPtr spec = DefaultEnterExitSpring(), float targetScale = 0, float originX = 0.5f, float originY = 0.5f);
    EnterTransition SlideIn(std::function<IntOffset(IntSize)> initialOffset, anim::SpecPtr spec = DefaultEnterExitSpring());
    ExitTransition SlideOut(std::function<IntOffset(IntSize)> targetOffset, anim::SpecPtr spec = DefaultEnterExitSpring());
    EnterTransition SlideInVertically(std::function<int(int)> initialOffsetY = [](int h) { return -h / 2; }, anim::SpecPtr spec = DefaultEnterExitSpring());
    ExitTransition SlideOutVertically(std::function<int(int)> targetOffsetY = [](int h) { return -h / 2; }, anim::SpecPtr spec = DefaultEnterExitSpring());
    EnterTransition SlideInHorizontally(std::function<int(int)> initialOffsetX = [](int w) { return -w / 2; }, anim::SpecPtr spec = DefaultEnterExitSpring());
    ExitTransition SlideOutHorizontally(std::function<int(int)> targetOffsetX = [](int w) { return -w / 2; }, anim::SpecPtr spec = DefaultEnterExitSpring());
    EnterTransition ExpandIn(anim::SpecPtr spec = DefaultEnterExitSpring(), Alignment expandFrom = Alignment::BottomEnd(),
                             bool clip = true, std::function<IntSize(IntSize)> initialSize = [](IntSize) { return IntSize{ 0, 0 }; });
    ExitTransition ShrinkOut(anim::SpecPtr spec = DefaultEnterExitSpring(), Alignment shrinkTowards = Alignment::BottomEnd(),
                             bool clip = true, std::function<IntSize(IntSize)> targetSize = [](IntSize) { return IntSize{ 0, 0 }; });
    EnterTransition ExpandVertically(anim::SpecPtr spec = DefaultEnterExitSpring(), AlignmentV expandFrom = AlignmentV::Bottom(),
                                     bool clip = true, std::function<int(int)> initialHeight = [](int) { return 0; });
    ExitTransition ShrinkVertically(anim::SpecPtr spec = DefaultEnterExitSpring(), AlignmentV shrinkTowards = AlignmentV::Bottom(),
                                    bool clip = true, std::function<int(int)> targetHeight = [](int) { return 0; });
    EnterTransition ExpandHorizontally(anim::SpecPtr spec = DefaultEnterExitSpring(), AlignmentH expandFrom = AlignmentH::End(),
                                       bool clip = true, std::function<int(int)> initialWidth = [](int) { return 0; });
    ExitTransition ShrinkHorizontally(anim::SpecPtr spec = DefaultEnterExitSpring(), AlignmentH shrinkTowards = AlignmentH::End(),
                                      bool clip = true, std::function<int(int)> targetWidth = [](int) { return 0; });

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
    void AnimatedEnterExit(Transition<bool>& visibility, const Modifier& modifier, std::function<void()> content,
                           const EnterTransition& enter, const ExitTransition& exit, rt::Key key = {});

    // Transition.AnimatedVisibility(visible): visibility derived from a parent transition's
    // state, animated on the parent's clock (a child transition).
    template<typename S>
    void AnimatedVisibility(Transition<S>& transition, std::function<bool(const S&)> visible, const Modifier& modifier,
                            std::function<void()> content, EnterTransition enter = FadeIn() + ExpandIn(),
                            ExitTransition exit = ShrinkOut() + FadeOut(), rt::Key key = {}) {
        auto& child = CreateChildTransition<bool, S>(transition, std::move(visible), key);
        AnimatedEnterExit(child, modifier, std::move(content), enter, exit, key);
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
    // AnimatedContent(targetState): content keyed by `target`; the old content leaves with the
    // exit while the new one enters, both stacked at contentAlignment.
    void AnimatedContent(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform(AnimatedContentTransitionScope&)> transitionSpec,
                         Alignment contentAlignment = Alignment::TopStart(), rt::Key key = {});
    void AnimatedContent(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                         std::function<ContentTransform()> transform = DefaultContentTransform,
                         Alignment contentAlignment = Alignment::TopStart(), rt::Key key = {});
    // animateContentSize(spring): the size follows content size changes with an animation.
    Modifier AnimateContentSize(Modifier m, anim::SpecPtr spec = anim::Spring(1.0f, anim::spring::StiffnessMediumLow, 0.5f));

    // Crossfade(targetState, animationSpec = tween()): the old content fades out while the new one
    // fades in, both kept until the fade ends. The key identifies the content.
    void Crossfade(rt::Key target, const Modifier& modifier, std::function<void(rt::Key)> content,
                   anim::SpecPtr spec = anim::Tween(), rt::Key key = {});

}
