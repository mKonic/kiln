#pragma once

// navigation3-ui scenes: Scene, OverlayScene, SceneStrategy (SinglePane, Dialog),
// SceneDecoratorStrategy, SceneInfo and the scene-setup decorator.

#include "kiln/foundation/Compose.h"
#include <kiln/Function.h>
#include "kiln/foundation/Dialog.h"
#include "kiln/foundation/NavigationEvent.h"
#include "kiln/foundation/SharedTransition.h"
#include "kiln/lifecycle/LifecycleCompose.h"
#include "kiln/navigation3/DecoratedNavEntries.h"

#include <set>

namespace kiln::navigation3 {

    // Scene: the entries rendered together, the ones before it (where back goes) and how they
    // render. Its key identifies it across back stack changes (with its class).
    template<typename T>
    class Scene {
    public:
        virtual ~Scene() = default;
        virtual std::string Key() const = 0;
        virtual const NavEntries<T>& Entries() const = 0;
        virtual const NavEntries<T>& PreviousEntries() const = 0;
        virtual void Content() const = 0;
        // The last entry's metadata.
        virtual NavMetadata Metadata() const { return Entries().empty() ? NavMetadata{} : Entries().back().Metadata(); }
        virtual bool Equals(const Scene& other) const = 0;
        virtual bool IsOverlay() const { return false; }
    };
    template<typename T>
    using ScenePtr = std::shared_ptr<const Scene<T>>;

    template<typename T>
    bool SameScene(const ScenePtr<T>& a, const ScenePtr<T>& b) {
        if (a == b) return true;
        if (!a || !b) return false;
        return typeid(*a) == typeid(*b) && a->Equals(*b);
    }

    // OverlayScene: rendered above the others, which stay composed (overlaidEntries).
    template<typename T>
    class OverlayScene : public Scene<T> {
    public:
        virtual const NavEntries<T>& OverlaidEntries() const = 0;
        // Runs before the scene leaves the composition (an exit animation, say).
        virtual rt::Task<> OnRemove() const { co_return; }
        bool IsOverlay() const override { return true; }
    };

    // SceneStrategyScope: what a strategy may call (onBack pops the back stack).
    template<typename T>
    class SceneStrategyScope {
    public:
        SceneStrategyScope() = default;
        explicit SceneStrategyScope(kiln::Function<void()> onBack) : onBack(std::move(onBack)) {}
        kiln::Function<void()> onBack = [] {};
    };

    // SceneStrategy: a scene for the last entries, or null to let the next strategy try.
    template<typename T>
    class SceneStrategy {
    public:
        virtual ~SceneStrategy() = default;
        virtual ScenePtr<T> CalculateScene(const SceneStrategyScope<T>& scope, const NavEntries<T>& entries) const = 0;
    };
    template<typename T>
    using SceneStrategyPtr = std::shared_ptr<const SceneStrategy<T>>;

    // A strategy from a function.
    template<typename T>
    SceneStrategyPtr<T> MakeSceneStrategy(kiln::Function<ScenePtr<T>(const SceneStrategyScope<T>&, const NavEntries<T>&)> fn) {
        struct Fn final : SceneStrategy<T> {
            kiln::Function<ScenePtr<T>(const SceneStrategyScope<T>&, const NavEntries<T>&)> fn;
            ScenePtr<T> CalculateScene(const SceneStrategyScope<T>& s, const NavEntries<T>& e) const override { return fn(s, e); }
        };
        auto p = std::make_shared<Fn>();
        p->fn = std::move(fn);
        return p;
    }

    // SceneDecoratorStrategy: wraps a calculated (non-overlay) scene.
    template<typename T>
    class SceneDecoratorStrategy {
    public:
        virtual ~SceneDecoratorStrategy() = default;
        virtual ScenePtr<T> DecorateScene(const SceneStrategyScope<T>& scope, ScenePtr<T> scene) const = 0;
    };
    template<typename T>
    using SceneDecoratorStrategyPtr = std::shared_ptr<const SceneDecoratorStrategy<T>>;

    // --- SinglePaneScene ------------------------------------------------------------------------

    template<typename T>
    class SinglePaneScene final : public Scene<T> {
    public:
        SinglePaneScene(std::string key, NavEntry<T> entry, NavEntries<T> previous)
            : m_Key(std::move(key)), m_Entries{ std::move(entry) }, m_Previous(std::move(previous)) {}
        std::string Key() const override { return m_Key; }
        const NavEntries<T>& Entries() const override { return m_Entries; }
        const NavEntries<T>& PreviousEntries() const override { return m_Previous; }
        void Content() const override { m_Entries.front().Content(); }
        bool Equals(const Scene<T>& other) const override {
            const auto& o = static_cast<const SinglePaneScene&>(other);
            return m_Key == o.m_Key && m_Entries == o.m_Entries && m_Previous == o.m_Previous;
        }
    private:
        std::string m_Key;
        NavEntries<T> m_Entries, m_Previous;
    };

    // SinglePaneSceneStrategy: the last entry alone.
    template<typename T>
    class SinglePaneSceneStrategy final : public SceneStrategy<T> {
    public:
        ScenePtr<T> CalculateScene(const SceneStrategyScope<T>&, const NavEntries<T>& entries) const override {
            return std::make_shared<SinglePaneScene<T>>(entries.back().ContentKey(), entries.back(), NavEntries<T>(entries.begin(), entries.end() - 1));
        }
    };

    template<typename T>
    ScenePtr<T> CalculateSceneWithSinglePaneFallback(const std::vector<SceneStrategyPtr<T>>& strategies, const SceneStrategyScope<T>& scope,
                                                     const NavEntries<T>& entries) {
        for (const auto& s : strategies)
            if (auto scene = s->CalculateScene(scope, entries)) return scene;
        return SinglePaneSceneStrategy<T>().CalculateScene(scope, entries);
    }

    // --- DialogScene ----------------------------------------------------------------------------

    template<typename T>
    class DialogScene final : public OverlayScene<T> {
    public:
        DialogScene(std::string key, NavEntry<T> entry, NavEntries<T> previous, NavEntries<T> overlaid, foundation::DialogProperties properties,
                    kiln::Function<void()> onBack)
            : m_Key(std::move(key)), m_Entries{ std::move(entry) }, m_Previous(std::move(previous)), m_Overlaid(std::move(overlaid)),
              m_Properties(properties), m_OnBack(std::move(onBack)) {}
        std::string Key() const override { return m_Key; }
        const NavEntries<T>& Entries() const override { return m_Entries; }
        const NavEntries<T>& PreviousEntries() const override { return m_Previous; }
        const NavEntries<T>& OverlaidEntries() const override { return m_Overlaid; }
        void Content() const override {
            NavEntry<T> entry = m_Entries.front();
            const auto lifecycleOwner = lifecycle::RememberLifecycleOwner();
            foundation::Dialog(m_OnBack, m_Properties, [entry, lifecycleOwner] {
                rt::Composition::Current()->Provide(lifecycle::LocalLifecycleOwner, lifecycleOwner, [entry] { entry.Content(); });
            });
        }
        bool Equals(const Scene<T>& other) const override {
            const auto& o = static_cast<const DialogScene&>(other);
            return m_Key == o.m_Key && m_Previous == o.m_Previous && m_Overlaid == o.m_Overlaid && m_Entries == o.m_Entries &&
                   m_Properties == o.m_Properties;
        }
    private:
        std::string m_Key;
        NavEntries<T> m_Entries, m_Previous, m_Overlaid;
        foundation::DialogProperties m_Properties;
        kiln::Function<void()> m_OnBack;
    };

    // The metadata key that makes an entry a dialog.
    inline constexpr NavMetadataKey<foundation::DialogProperties> DialogKey{ "androidx.navigation3.scene.DialogSceneStrategy.DialogKey" };

    // DialogSceneStrategy: the last entry in a dialog when its metadata has DialogKey.
    template<typename T>
    class DialogSceneStrategy final : public SceneStrategy<T> {
    public:
        ScenePtr<T> CalculateScene(const SceneStrategyScope<T>& scope, const NavEntries<T>& entries) const override {
            if (entries.empty()) return nullptr;
            const auto* properties = Get(entries.back().Metadata(), DialogKey);
            if (!properties) return nullptr;
            NavEntries<T> before(entries.begin(), entries.end() - 1);
            return std::make_shared<DialogScene<T>>(entries.back().ContentKey(), entries.back(), before, before, *properties, scope.onBack);
        }
        // dialog(dialogProperties)
        static NavMetadata Dialog(foundation::DialogProperties properties = {}) { return Metadata([&](MetadataScope& s) { s.Put(DialogKey, properties); }); }
    };

    // --- SceneInfo ------------------------------------------------------------------------------

    inline constexpr NavMetadataKey<kiln::Function<std::string()>> TitleMetadataKey{ "androidx.navigation3.scene.TitleMetadataKey" };
    inline constexpr NavMetadataKey<kiln::Function<std::string()>> UrlMetadataKey{ "androidx.navigation3.scene.UrlMetadataKey" };

    // SceneInfo: a scene as navigation event info.
    template<typename T>
    struct SceneInfo : foundation::NavigationEventInfo {
        ScenePtr<T> scene;
        SceneInfo() = default;
        explicit SceneInfo(ScenePtr<T> s, std::optional<std::string> t = std::nullopt, std::optional<std::string> u = std::nullopt) : scene(std::move(s)) {
            title = std::move(t);
            url = std::move(u);
        }
        bool operator==(const SceneInfo& o) const { return SameScene(scene, o.scene) && title == o.title && url == o.url; }
    };

    // --- Scene setup ----------------------------------------------------------------------------

    // The content keys the scene being composed must skip (another scene renders them).
    inline rt::Local<std::shared_ptr<const std::set<std::string>>> LocalEntriesToExcludeFromCurrentScene{ std::make_shared<const std::set<std::string>>() };

    // SceneSetupNavEntryDecorator: each entry renders through a movable content per content key,
    // so its state follows it from scene to scene; an excluded entry renders nothing here.
    template<typename T>
    NavEntryDecorator<T> RememberSceneSetupNavEntryDecorator(rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        using Movable = rt::MovableContent<kiln::Function<void()>>;
        using Map = std::map<std::string, Movable>;
        auto& decorator = c.Remember<std::shared_ptr<NavEntryDecorator<T>>>([] {
            auto map = std::make_shared<Map>();
            return std::make_shared<NavEntryDecorator<T>>(
                [map](const std::string& contentKey) { map->erase(contentKey); },
                [map](const NavEntry<T>& entry) {
                    auto& cc = *rt::Composition::Current();
                    const std::string contentKey = entry.ContentKey();
                    const auto& excluded = cc.Read(LocalEntriesToExcludeFromCurrentScene);
                    if (excluded && excluded->contains(contentKey)) return;
                    cc.Section([&cc, map, contentKey, entry] {
                        auto& movable = cc.Remember<std::shared_ptr<Movable>>([&] {
                            auto it = map->find(contentKey);
                            if (it == map->end())
                                it = map->emplace(contentKey, Movable(kiln::Function<void(kiln::Function<void()>)>([](kiln::Function<void()> content) { content(); }))).first;
                            return std::make_shared<Movable>(it->second);
                        });
                        (*movable)(kiln::Function<void()>([entry] { entry.Content(); }));
                    }, rt::Key(contentKey));
                });
        }, {}, key);
        return *decorator;
    }

    // LocalNavAnimatedContentScope: the AnimatedContent scope NavDisplay shows the entries in
    // (for sharedElement / animateEnterExit in a NavEntry); null outside NavDisplay, where
    // upstream throws on access (see NavAnimatedContentScope).
    inline rt::Local<foundation::AnimatedVisibilityScope*> LocalNavAnimatedContentScope{ nullptr };
    inline foundation::AnimatedVisibilityScope& NavAnimatedContentScope() {
        auto* scope = rt::Composition::Current()->Read(LocalNavAnimatedContentScope);
        if (!scope)
            throw std::logic_error("Unexpected access to LocalNavAnimatedContentScope. You should only access LocalNavAnimatedContentScope inside a NavEntry passed to NavDisplay.");
        return *scope;
    }

    // LocalCurrentScene (internal upstream): the scene being composed, and whether it overlays.
    struct CurrentScene {
        const void* scene = nullptr;
        bool overlay = false;
        bool operator==(const CurrentScene&) const = default;
    };
    inline rt::Local<CurrentScene> LocalCurrentScene{ CurrentScene{} };

    // SharedEntryInSceneNavEntryDecorator: an entry in a (non-overlay) scene is a shared element
    // keyed by its content key, so it moves between scenes (a list pane becoming a full screen)
    // instead of fading out and in. Content outside a scene, or in an overlay, is left alone.
    template<typename T>
    NavEntryDecorator<T> RememberSharedEntryInSceneNavEntryDecorator(foundation::SharedTransitionScope& sharedTransitionScope, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto* scope = &sharedTransitionScope;
        auto& decorator = c.Remember<std::shared_ptr<NavEntryDecorator<T>>>([scope] {
            return std::make_shared<NavEntryDecorator<T>>([scope](const NavEntry<T>& entry) {
                auto& cc = *rt::Composition::Current();
                const CurrentScene current = cc.Read(LocalCurrentScene);
                foundation::AnimatedVisibilityScope* animated = cc.Read(LocalNavAnimatedContentScope);
                if (!current.scene || current.overlay || !animated) {
                    entry.Content();
                    return;
                }
                auto& state = scope->RememberSharedContentState(rt::Key(std::string_view(entry.ContentKey())),
                    foundation::SharedTransitionScope::MakeSharedContentConfig(foundation::DeferredTransitionBehavior::SeekToTarget));
                foundation::Box(scope->SharedElement(ui::Modifier{}, state, *animated), ui::Alignment::TopStart(), false, [entry] { entry.Content(); });
            });
        }, rt::Keys(static_cast<const void*>(scope)), key);
        return *decorator;
    }

    // BackStackAwareLifecycleNavEntryDecorator: each entry gets a lifecycle owner capped at
    // RESUMED while on the back stack and at CREATED once popped (still animating out).
    template<typename T>
    NavEntryDecorator<T> RememberBackStackAwareLifecycleNavEntryDecorator(const NavEntries<T>& entries, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        // rememberUpdatedState(entries)
        auto& keys = c.Remember<std::shared_ptr<std::set<std::string>>>([] { return std::make_shared<std::set<std::string>>(); }, {}, key);
        keys->clear();
        for (const auto& e : entries) keys->insert(e.ContentKey());
        auto& decorator = c.Remember<std::shared_ptr<NavEntryDecorator<T>>>([&] {
            return std::make_shared<NavEntryDecorator<T>>([keys = keys](const NavEntry<T>& entry) {
                const bool inBackStack = keys->contains(entry.ContentKey());
                const auto owner = lifecycle::RememberLifecycleOwner(inBackStack ? lifecycle::State::Resumed : lifecycle::State::Created);
                rt::Composition::Current()->Provide(lifecycle::LocalLifecycleOwner, owner, [entry] { entry.Content(); });
            });
        }, {}, key);
        return *decorator;
    }

}
