#pragma once

// NavDisplay (navigation3-ui NavDisplay.kt, NavDisplay.android.kt, PredictivePopGesture.kt): the
// back stack's scenes in an AnimatedContent, moving to the previous scene by hand while a back
// gesture is in progress. Not here yet (no kiln counterpart): the lifecycle caps on scenes and
// entries, LocalNavAnimatedContentScope and the shared element decorator.

#include "kiln/navigation3/SceneState.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/ui/ColorSpace.h"

#include <algorithm>
#include <cmath>

namespace kiln::navigation3 {

    using ui::Alignment;
    using ui::Modifier;

    using SceneTransitionSpec = std::function<foundation::ContentTransform(foundation::AnimatedContentTransitionScope&)>;
    using PredictivePopTransitionSpec = std::function<foundation::ContentTransform(foundation::AnimatedContentTransitionScope&, ui::SwipeEdge)>;

    // NavDisplay's metadata keys: a scene (its last entry) can bring its own transitions.
    namespace NavDisplayKeys {
        inline constexpr NavMetadataKey<SceneTransitionSpec> TransitionKey{ "androidx.navigation3.ui.NavDisplay.TransitionKey" };
        inline constexpr NavMetadataKey<SceneTransitionSpec> PopTransitionKey{ "androidx.navigation3.ui.NavDisplay.PopTransitionKey" };
        inline constexpr NavMetadataKey<PredictivePopTransitionSpec> PredictivePopTransitionKey{ "androidx.navigation3.ui.NavDisplay.PredictivePopTransitionKey" };
        inline NavMetadata TransitionSpec(SceneTransitionSpec spec) { return Metadata([&](MetadataScope& s) { s.Put(TransitionKey, std::move(spec)); }); }
        inline NavMetadata PopTransitionSpec(SceneTransitionSpec spec) { return Metadata([&](MetadataScope& s) { s.Put(PopTransitionKey, std::move(spec)); }); }
        inline NavMetadata PredictivePopTransitionSpecOf(PredictivePopTransitionSpec spec) {
            return Metadata([&](MetadataScope& s) { s.Put(PredictivePopTransitionKey, std::move(spec)); });
        }
    }

    inline constexpr int DefaultTransitionDurationMillisecond = 700;

    // fadeIn(tween(700)) togetherWith fadeOut(tween(700)), for navigating and popping alike.
    inline SceneTransitionSpec DefaultTransitionSpec() {
        return [](foundation::AnimatedContentTransitionScope&) {
            return foundation::TogetherWith(foundation::FadeIn(anim::Tween(DefaultTransitionDurationMillisecond)),
                                            foundation::FadeOut(anim::Tween(DefaultTransitionDurationMillisecond)));
        };
    }
    inline SceneTransitionSpec DefaultPopTransitionSpec() { return DefaultTransitionSpec(); }
    // fadeIn(spring(1, 1600)) togetherWith scaleOut(0.7).
    inline PredictivePopTransitionSpec DefaultPredictivePopTransitionSpec() {
        return [](foundation::AnimatedContentTransitionScope&, ui::SwipeEdge) {
            return foundation::TogetherWith(foundation::FadeIn(anim::Spring(1.0f, 1600.0f)), foundation::ScaleOut(foundation::DefaultEnterExitSpring(), 0.7f));
        };
    }

    // The options NavDisplay takes besides its back stack or entries.
    template<typename T>
    struct NavDisplayOptions {
        Alignment contentAlignment = Alignment::TopStart();
        // Pops one entry; unset, NavDisplay over a NavBackStack removes the last key.
        std::function<void()> onBack;
        // Unset: rememberSaveableStateHolderNavEntryDecorator().
        std::optional<std::vector<NavEntryDecorator<T>>> entryDecorators;
        std::vector<SceneStrategyPtr<T>> sceneStrategies{ std::make_shared<SinglePaneSceneStrategy<T>>() };
        std::vector<SceneDecoratorStrategyPtr<T>> sceneDecoratorStrategies;
        std::optional<foundation::SizeTransform> sizeTransform;
        SceneTransitionSpec transitionSpec = DefaultTransitionSpec();
        SceneTransitionSpec popTransitionSpec = DefaultPopTransitionSpec();
        PredictivePopTransitionSpec predictivePopTransitionSpec = DefaultPredictivePopTransitionSpec();
    };

    namespace detail {
        // AnimatedSceneKey(scene::class, scene.key) as AnimatedContent's content key.
        template<typename T>
        std::uint64_t SceneKey(const Scene<T>& scene) {
            const std::size_t h = std::hash<std::string>{}(scene.Key()) * 31u ^ std::type_index(typeid(scene)).hash_code();
            return std::uint64_t(h) | 1u;
        }

        inline bool IsPop(const std::vector<std::string>& oldStack, const std::vector<std::string>& newStack) {
            if (oldStack.empty() || newStack.empty()) return false;
            if (oldStack.front() != newStack.front()) return false;       // entire stack replaced
            if (newStack.size() > oldStack.size()) return false;          // navigated
            for (std::size_t i = 0; i < newStack.size(); ++i)
                if (newStack[i] != oldStack[i]) return false;
            return newStack.size() != oldStack.size();
        }

        inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
        inline int LerpInt(int a, int b, float t) { return int(std::lround(double(a) + double(b - a) * double(t))); }
        inline float EasedProgress(const anim::SpecPtr& spec, float progress) {
            const auto easing = anim::TweenEasing(spec);
            return easing ? easing->Transform(progress) : progress;
        }

        // ContentTransform.toMutableTransform(progress): the enter and exit set by hand along the
        // gesture's progress (each part through its tween's easing, linearly otherwise).
        inline std::shared_ptr<foundation::MutableContentTransform> ToMutableTransform(const foundation::ContentTransform& t,
                                                                                         std::function<float()> progress) {
            const foundation::EnterExitParts enter = t.enter.parts, exit = t.exit.parts;
            return foundation::MakeMutableContentTransform(
                exit.veil ? exit.veil->matchParentSize : false, enter.veil ? enter.veil->matchParentSize : false, {}, {},
                [enter, exit, progress](foundation::MutableContentTransform& m) {
                    m.SharedElementProgress(progress);
                    m.TargetContentTransform([enter, progress](foundation::TransformScope& s, ui::IntSize fullSize) {
                        const float p = progress();
                        if (enter.fade) s.SetAlpha(Lerp(enter.fade->first, 1.0f, EasedProgress(enter.fade->second, p)));
                        if (enter.scale) {
                            s.SetScale(Lerp(enter.scale->scale, 1.0f, EasedProgress(enter.scale->spec, p)));
                            s.SetTransformOrigin({ enter.scale->originX, enter.scale->originY });
                        }
                        if (enter.slide) {
                            const float q = EasedProgress(enter.slide->spec, p);
                            const ui::IntOffset initial = enter.slide->offset(fullSize);
                            s.SetOffset({ LerpInt(initial.x, 0, q), LerpInt(initial.y, 0, q) });
                        }
                        if (enter.veil) s.SetVeil(ui::Lerp(enter.veil->initialColor, enter.veil->targetColor, EasedProgress(enter.veil->spec, p)));
                    });
                    m.InitialContentTransform([exit, progress](foundation::TransformScope& s, ui::IntSize fullSize) {
                        const float p = progress();
                        if (exit.fade) s.SetAlpha(Lerp(1.0f, exit.fade->first, EasedProgress(exit.fade->second, p)));
                        if (exit.scale) {
                            s.SetScale(Lerp(1.0f, exit.scale->scale, EasedProgress(exit.scale->spec, p)));
                            s.SetTransformOrigin({ exit.scale->originX, exit.scale->originY });
                        }
                        if (exit.slide) {
                            const float q = EasedProgress(exit.slide->spec, p);
                            const ui::IntOffset target = exit.slide->offset(fullSize);
                            s.SetOffset({ LerpInt(0, target.x, q), LerpInt(0, target.y, q) });
                        }
                        if (exit.veil) s.SetVeil(ui::Lerp(exit.veil->initialColor, exit.veil->targetColor, EasedProgress(exit.veil->spec, p)));
                    });
                });
        }

        // What NavDisplay keeps between compositions.
        template<typename T>
        struct DisplayBook {
            std::map<std::uint64_t, ScenePtr<T>> sceneMap;
            std::map<std::uint64_t, float> zIndices;
            std::vector<std::string> previousEntries;
            std::vector<std::string> isPopEntries;
            bool isPop = false;
            bool started = false;
            float lastProgress = 0;
            std::map<std::uint64_t, std::shared_ptr<const std::set<std::string>>> excluded;
        };
    }

    // NavDisplay(sceneState, navigationEventState, ...): the scenes, animated as they change.
    template<typename T>
    void NavDisplay(const SceneState<T>& sceneState, foundation::NavigationEventState<SceneInfo<T>>& navigationEventState, const Modifier& modifier = {},
                    const NavDisplayOptions<T>& options = {}, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto* nes = &navigationEventState;
        c.Child([&c, sceneState, nes, modifier, options] {
            auto& navigationEventState = *nes;
            using foundation::AnimatedContentTransitionScope;
            using foundation::ContentTransform;
            const ScenePtr<T> scene = sceneState.currentScene;
            auto& book = c.Remember<std::shared_ptr<detail::DisplayBook<T>>>([] { return std::make_shared<detail::DisplayBook<T>>(); });
            auto& transitionState = c.RememberNew<foundation::DeferredTransitionState<std::uint64_t>>({}, {}, detail::SceneKey(*scene));
            if (!book->started) { book->sceneMap[detail::SceneKey(*scene)] = scene; book->started = true; }

            // Gesture back tracking.
            const ScenePtr<T> previousScene = sceneState.previousScenes.empty() ? nullptr : sceneState.previousScenes.back();
            const foundation::NavigationEventTransitionState gesture = navigationEventState.TransitionState();
            const bool inPredictiveBack = gesture.inProgress && previousScene;
            const ui::SwipeEdge swipeEdge = gesture.inProgress ? gesture.latestEvent.swipeEdge : ui::SwipeEdge::None;
            if (gesture.inProgress) book->lastProgress = gesture.latestEvent.progress;

            const ScenePtr<T> targetScene = inPredictiveBack ? previousScene : scene;
            const std::uint64_t targetKey = detail::SceneKey(*targetScene);
            if (inPredictiveBack) transitionState.Defer(targetKey);
            else transitionState.AnimateTo(targetKey);
            auto& transition = foundation::RememberDeferredTransition(transitionState);

            // Settled: scenes other than the target leave the book-keeping.
            if (transition.CurrentState() == transition.TargetState() && !transition.PendingTargetState()) {
                const std::uint64_t settled = transition.TargetState();
                std::erase_if(book->sceneMap, [&](const auto& kv) { return kv.first != settled; });
                std::erase_if(book->zIndices, [&](const auto& kv) { return kv.first != settled; });
            }

            // Whether the back stack change is a pop (computed when the entries change).
            std::vector<std::string> entryKeys;
            for (const auto& e : sceneState.entries) entryKeys.push_back(e.ContentKey());
            if (book->isPopEntries != entryKeys || book->previousEntries.empty()) {
                const std::vector<std::string> oldStack = book->previousEntries.empty() ? entryKeys : book->previousEntries;
                book->isPop = detail::IsPop(oldStack, entryKeys);
                book->previousEntries = entryKeys;
                book->isPopEntries = entryKeys;
            }
            const bool isPop = book->isPop;

            // Z indices: navigating stacks the target above, popping below.
            const std::uint64_t initialKey = transition.CurrentState();
            if (!book->zIndices.contains(initialKey)) book->zIndices[initialKey] = 0;
            const float initialZ = book->zIndices[initialKey];
            float targetZ;
            if (book->zIndices.contains(targetKey)) targetZ = book->zIndices[targetKey];
            else if (isPop || inPredictiveBack) {
                float lowest = initialZ;
                bool any = false;
                for (const auto& [k, s] : book->sceneMap) if (book->zIndices.contains(k)) { lowest = any ? std::min(lowest, book->zIndices[k]) : book->zIndices[k]; any = true; }
                targetZ = lowest - 1;
            } else {
                float highest = initialZ;
                bool any = false;
                for (const auto& [k, s] : book->sceneMap) if (book->zIndices.contains(k)) { highest = any ? std::max(highest, book->zIndices[k]) : book->zIndices[k]; any = true; }
                targetZ = highest + 1;
            }
            book->sceneMap[targetKey] = targetScene;
            book->zIndices[targetKey] = targetZ;

            // Overlay scenes still on the back stack, plus popped ones still animating out.
            auto& currentOverlays = c.Remember<std::shared_ptr<rt::StateList<OverlayScenePtr<T>>>>([] { return std::make_shared<rt::StateList<OverlayScenePtr<T>>>(); });
            const std::vector<OverlayScenePtr<T>> overlayScenes = sceneState.overlayScenes;
            {
                auto list = currentOverlays;
                c.SideEffect([list, overlayScenes] {
                    for (const auto& o : overlayScenes) {
                        bool present = false;
                        for (const auto& cur : list->Get()) if (cur->Key() == o->Key()) { present = true; break; }
                        if (!present) list->Add(o);
                    }
                });
            }

            // Which entries each rendered scene leaves to another: every content key renders in
            // the topmost scene that has it (the target claims first while popping).
            {
                std::vector<std::pair<std::uint64_t, ScenePtr<T>>> sorted(book->sceneMap.begin(), book->sceneMap.end());
                std::stable_sort(sorted.begin(), sorted.end(), [&](const auto& a, const auto& b) { return book->zIndices[a.first] > book->zIndices[b.first]; });
                std::vector<ScenePtr<T>> scenes;
                for (const auto& [k, s] : sorted) {
                    bool dup = false;
                    for (const auto& x : scenes) if (SameScene(x, s)) { dup = true; break; }
                    if (!dup) scenes.push_back(s);
                }
                const std::uint64_t pendingOrTarget = transition.PendingTargetState() ? *transition.PendingTargetState() : transition.TargetState();
                const bool popOrder = !scenes.empty() && pendingOrTarget != detail::SceneKey(*scenes.front());
                std::vector<ScenePtr<T>> inZOrder;
                for (const auto& o : currentOverlays->Get()) inZOrder.push_back(o);
                for (const auto& s : scenes) inZOrder.push_back(s);
                if (popOrder) std::reverse(inZOrder.begin(), inZOrder.end());
                std::set<std::string> covered;
                book->excluded.clear();
                for (const auto& s : inZOrder) {
                    book->excluded[detail::SceneKey(*s)] = std::make_shared<const std::set<std::string>>(covered);
                    for (const auto& e : s->Entries()) covered.insert(e.ContentKey());
                }
            }

            auto bookRef = book;
            const NavDisplayOptions<T> opts = options;
            auto contentTransform = [bookRef, opts](AnimatedContentTransitionScope& scope) -> ContentTransform {
                const float initialZLocal = bookRef->zIndices[scope.initialState.value];
                const float targetZLocal = bookRef->zIndices[scope.targetState.value];
                const bool popLocal = targetZLocal < initialZLocal;
                const auto sceneOf = [&](std::uint64_t k) -> ScenePtr<T> { auto it = bookRef->sceneMap.find(k); return it == bookRef->sceneMap.end() ? nullptr : it->second; };
                const ScenePtr<T> transitionScene = initialZLocal >= targetZLocal ? sceneOf(scope.initialState.value) : sceneOf(scope.targetState.value);
                const NavMetadata metadata = transitionScene ? transitionScene->Metadata() : NavMetadata{};
                if (popLocal) {
                    if (const auto* spec = Get(metadata, NavDisplayKeys::PopTransitionKey)) return (*spec)(scope);
                    return opts.popTransitionSpec(scope);
                }
                if (const auto* spec = Get(metadata, NavDisplayKeys::TransitionKey)) return (*spec)(scope);
                return opts.transitionSpec(scope);
            };

            foundation::DeferredAnimatedContent(
                transition, modifier,
                [bookRef](rt::Key k) {
                    auto it = bookRef->sceneMap.find(k.value);
                    if (it == bookRef->sceneMap.end()) return;
                    const ScenePtr<T> s = it->second;
                    auto ex = bookRef->excluded.find(k.value);
                    auto excluded = ex == bookRef->excluded.end() ? std::make_shared<const std::set<std::string>>() : ex->second;
                    rt::Composition::Current()->Provide(LocalEntriesToExcludeFromCurrentScene, excluded, [s] { s->Content(); });
                },
                [contentTransform, targetZ, opts](AnimatedContentTransitionScope& scope) {
                    ContentTransform t = contentTransform(scope);
                    // The z index rises while navigating and falls while popping.
                    t.targetContentZIndex = targetZ;
                    t.sizeTransform = opts.sizeTransform;
                    return t;
                },
                options.contentAlignment,
                [bookRef, opts, inPredictiveBack, swipeEdge](AnimatedContentTransitionScope& scope) -> std::shared_ptr<foundation::MutableContentTransform> {
                    if (!inPredictiveBack) return nullptr;
                    ContentTransform spec;
                    auto it = bookRef->sceneMap.find(scope.initialState.value);
                    const PredictivePopTransitionSpec* own = it == bookRef->sceneMap.end() ? nullptr : Get(it->second->Metadata(), NavDisplayKeys::PredictivePopTransitionKey);
                    spec = own ? (*own)(scope, swipeEdge) : opts.predictivePopTransitionSpec(scope, swipeEdge);
                    return detail::ToMutableTransform(spec, [bookRef] { return bookRef->lastProgress; });
                });

            // The overlay scenes above the AnimatedContent, topmost last.
            const std::vector<OverlayScenePtr<T>> overlays = currentOverlays->Get();
            for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
                const OverlayScenePtr<T> overlay = *it;
                const std::uint64_t okey = detail::SceneKey(*overlay);
                auto ex = book->excluded.find(okey);
                auto excluded = ex == book->excluded.end() ? std::make_shared<const std::set<std::string>>() : ex->second;
                bool onBackStack = false;
                for (const auto& o : overlayScenes) if (o->Key() == overlay->Key()) { onBackStack = true; break; }
                c.Section([&c, overlay, excluded, onBackStack, list = currentOverlays] {
                    c.Provide(LocalEntriesToExcludeFromCurrentScene, excluded, [overlay] { overlay->Content(); });
                    // A popped overlay finishes onRemove before it leaves.
                    if (!onBackStack)
                        c.LaunchedEffect(rt::Keys(overlay->Key()), [overlay, list]() -> rt::Task<> { return [](OverlayScenePtr<T> o, std::shared_ptr<rt::StateList<OverlayScenePtr<T>>> l) -> rt::Task<> {
                            co_await o->OnRemove();
                            l->Update([&](std::vector<OverlayScenePtr<T>>& v) { std::erase(v, o); });
                        }(overlay, list); });
                }, rt::Key(overlay->Key()));
            }
        }, key);
    }

    // NavDisplay(entries, ...): the scenes of already decorated entries; options.onBack pops one.
    template<typename T>
    void NavDisplay(const NavEntries<T>& entries, const Modifier& modifier = {}, const NavDisplayOptions<T>& options = {}, rt::Key key = {}) {
        if (entries.empty()) throw std::invalid_argument("NavDisplay entries cannot be empty");
        auto& c = *rt::Composition::Current();
        c.Child([entries, modifier, options] {
            const std::function<void()> onBack = options.onBack ? options.onBack : [] {};
            const SceneState<T> sceneState = RememberSceneState(entries, options.sceneStrategies, options.sceneDecoratorStrategies, onBack);
            auto& navigationEventState = RememberNavigationEventState(sceneState);
            NavigationBackHandler(sceneState, navigationEventState, [] {}, onBack);
            NavDisplay(sceneState, navigationEventState, modifier, options);
        }, key);
    }

    // NavDisplay(backStack, entryProvider, ...).
    template<typename T>
    void NavDisplay(const std::vector<T>& backStack, const std::function<NavEntry<T>(const T&)>& entryProvider, const Modifier& modifier = {},
                    const NavDisplayOptions<T>& options = {}, rt::Key key = {}) {
        if (backStack.empty()) throw std::invalid_argument("NavDisplay backstack cannot be empty");
        auto& c = *rt::Composition::Current();
        c.Child([backStack, entryProvider, modifier, options] {
            const std::vector<NavEntryDecorator<T>> decorators =
                options.entryDecorators ? *options.entryDecorators : std::vector<NavEntryDecorator<T>>{ RememberSaveableStateHolderNavEntryDecorator<T>() };
            const NavEntries<T> entries = RememberDecoratedNavEntries(backStack, decorators, entryProvider);
            NavDisplay(entries, modifier, options);
        }, key);
    }

    // NavDisplay(backStack: NavBackStack, ...): back removes the last key unless options.onBack.
    template<typename T>
    void NavDisplay(NavBackStack<T>& backStack, const std::function<NavEntry<T>(const T&)>& entryProvider, const Modifier& modifier = {},
                    NavDisplayOptions<T> options = {}, rt::Key key = {}) {
        if (!options.onBack) {
            auto* stack = &backStack;
            options.onBack = [stack] { stack->RemoveLast(); };
        }
        NavDisplay(backStack.ToList(), entryProvider, modifier, options, key);
    }

}
