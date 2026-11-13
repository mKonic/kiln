#pragma once

// navigation3-runtime: NavEntry, its metadata, decorators and the entryProvider DSL.
//
// Keys are any copyable, equality-comparable type T. A key's "class" (entry<K>, the default
// content key) is the alternative it holds for a std::variant, the pointee's dynamic type for a
// std::shared_ptr, and T itself otherwise. Content keys are strings: the default is the key's
// text (NavKeyToString(k) found by ADL, else std::format, else its hash) and its class.

#include "kiln/runtime/Composition.h"
#include <kiln/Function.h>

#include <any>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <variant>
#include <vector>

namespace kiln::navigation3 {

    // --- Keys -----------------------------------------------------------------------------------

    namespace detail {
        template<typename T> struct IsVariant : std::false_type {};
        template<typename... A> struct IsVariant<std::variant<A...>> : std::true_type {};
        template<typename T> struct IsSharedPtr : std::false_type {};
        template<typename P> struct IsSharedPtr<std::shared_ptr<P>> : std::true_type {};

        template<typename K>
        std::string KeyText(const K& k) {
            if constexpr (requires { NavKeyToString(k); }) return NavKeyToString(k);
            else if constexpr (std::formattable<K, char>) return std::format("{}", k);
            else if constexpr (requires { std::hash<K>{}(k); }) return std::to_string(std::hash<K>{}(k));
            else if constexpr (std::is_empty_v<K>) return std::string();
            else static_assert(sizeof(K) == 0, "a navigation key needs NavKeyToString, std::format or std::hash");
        }
    }

    // NavKeyTraits<T>: a key's class, for entry<K> and default content keys.
    template<typename T>
    struct NavKeyTraits {
        static std::type_index ClassOf(const T& key) {
            if constexpr (detail::IsVariant<T>::value) return std::visit([](const auto& v) { return std::type_index(typeid(v)); }, key);
            else if constexpr (detail::IsSharedPtr<T>::value) return key ? std::type_index(typeid(*key)) : std::type_index(typeid(void));
            else return std::type_index(typeid(T));
        }
        template<typename K>
        static const K* As(const T& key) {
            if constexpr (detail::IsVariant<T>::value) return std::get_if<K>(&key);
            else if constexpr (detail::IsSharedPtr<T>::value) return dynamic_cast<const K*>(key.get());
            else if constexpr (std::is_same_v<K, T>) return &key;
            else return nullptr;
        }
        static std::string Text(const T& key) {
            if constexpr (detail::IsVariant<T>::value) return std::visit([](const auto& v) { return detail::KeyText(v); }, key);
            else if constexpr (detail::IsSharedPtr<T>::value) return key ? detail::KeyText(*key) : std::string("null");
            else return detail::KeyText(key);
        }
    };

    // defaultContentKey(key): "$key:${key::class}".
    template<typename T>
    std::string DefaultContentKey(const T& key) {
        return NavKeyTraits<T>::Text(key) + ":" + NavKeyTraits<T>::ClassOf(key).name();
    }

    // --- Metadata -------------------------------------------------------------------------------

    using NavMetadata = std::map<std::string, std::any>;

    // NavMetadataKey<V>: a typed metadata key (its name is the map key).
    template<typename V>
    struct NavMetadataKey {
        const char* name;
    };

    // metadata[key]: the value for `key`, or null when absent or of another type.
    template<typename V>
    const V* Get(const NavMetadata& metadata, const NavMetadataKey<V>& key) {
        auto it = metadata.find(key.name);
        return it == metadata.end() ? nullptr : std::any_cast<V>(&it->second);
    }
    template<typename V>
    bool Contains(const NavMetadata& metadata, const NavMetadataKey<V>& key) { return metadata.contains(key.name); }

    // metadata { put(key, value) }
    class MetadataScope {
    public:
        template<typename V>
        MetadataScope& Put(const NavMetadataKey<V>& key, V value) { m_Map[key.name] = std::move(value); return *this; }
        NavMetadata Build() const { return m_Map; }
    private:
        NavMetadata m_Map;
    };
    inline NavMetadata Metadata(const kiln::Function<void(MetadataScope&)>& builder) {
        MetadataScope s;
        builder(s);
        return s.Build();
    }
    // a + b: b's values win (Map.plus).
    inline NavMetadata operator+(NavMetadata a, const NavMetadata& b) {
        for (const auto& [k, v] : b) a[k] = v;
        return a;
    }

    // --- NavEntry -------------------------------------------------------------------------------

    // NavEntry: a key, the content it stands for and the metadata a display reads. Entries that
    // share a contentKey share their content and decorator state.
    template<typename T>
    class NavEntry {
    public:
        using ContentFn = kiln::Function<void(const T&)>;
        NavEntry(T key, std::string contentKey, NavMetadata metadata, ContentFn content)
            : m_Key(std::move(key)), m_ContentKey(std::move(contentKey)), m_Metadata(std::move(metadata)),
              m_Content(std::make_shared<ContentFn>(std::move(content))) {}
        NavEntry(T key, ContentFn content) : NavEntry(key, DefaultContentKey(key), {}, std::move(content)) {}
        // NavEntry(navEntry, content): the same key, content key and metadata.
        NavEntry(const NavEntry& entry, ContentFn content)
            : m_Key(entry.m_Key), m_ContentKey(entry.m_ContentKey), m_Metadata(entry.m_Metadata), m_Content(std::make_shared<ContentFn>(std::move(content))) {}
        NavEntry(const NavEntry&) = default;
        NavEntry& operator=(const NavEntry&) = default;

        const T& Key() const { return m_Key; }
        const std::string& ContentKey() const { return m_ContentKey; }
        const NavMetadata& Metadata() const { return m_Metadata; }
        void Content() const { (*m_Content)(m_Key); }
        // Same key, content key and content (metadata holds values that do not compare).
        bool operator==(const NavEntry& o) const { return m_Key == o.m_Key && m_ContentKey == o.m_ContentKey && m_Content == o.m_Content; }

    private:
        T m_Key;
        std::string m_ContentKey;
        NavMetadata m_Metadata;
        std::shared_ptr<ContentFn> m_Content;
    };

    template<typename T>
    using NavEntries = std::vector<NavEntry<T>>;

    // NavEntryDecorator: wraps the content of the entries a display renders. onPop(contentKey)
    // runs when the last entry with that content key left the back stack and the composition.
    template<typename T>
    class NavEntryDecorator {
    public:
        NavEntryDecorator(kiln::Function<void(const std::string&)> onPop, kiln::Function<void(const NavEntry<T>&)> decorate)
            : m_Impl(std::make_shared<Impl>(Impl{ std::move(onPop), std::move(decorate) })) {}
        explicit NavEntryDecorator(kiln::Function<void(const NavEntry<T>&)> decorate) : NavEntryDecorator([](const std::string&) {}, std::move(decorate)) {}
        void OnPop(const std::string& contentKey) const { if (m_Impl->onPop) m_Impl->onPop(contentKey); }
        void Decorate(const NavEntry<T>& entry) const { m_Impl->decorate(entry); }
        bool operator==(const NavEntryDecorator& o) const { return m_Impl == o.m_Impl; }

    private:
        struct Impl {
            kiln::Function<void(const std::string&)> onPop;
            kiln::Function<void(const NavEntry<T>&)> decorate;
        };
        std::shared_ptr<Impl> m_Impl;
    };

    // --- entryProvider --------------------------------------------------------------------------

    // EntryProviderScope: entries for single keys (entry(key, ...)) and for key classes
    // (entry<K>(...)); build() looks a key up, falling back to `fallback`.
    template<typename T>
    class EntryProviderScope {
    public:
        using Fallback = kiln::Function<NavEntry<T>(const T&)>;
        explicit EntryProviderScope(Fallback fallback = {}) : m_Fallback(std::move(fallback)) {}

        // entry(key, contentKey, metadata, content)
        template<typename K>
        void Entry(K key, kiln::Function<void(const K&)> content, NavMetadata metadata = {}, std::optional<std::string> contentKey = std::nullopt) {
            EntryWithMetadata<K>(std::move(key), std::move(content), [metadata](const K&) { return metadata; }, std::move(contentKey));
        }
        template<typename K>
        void EntryWithMetadata(K key, kiln::Function<void(const K&)> content, kiln::Function<NavMetadata(const K&)> metadata,
                               std::optional<std::string> contentKey = std::nullopt) {
            const T asKey(key);
            for (const auto& p : m_Providers)
                if (p.key == asKey) throw std::invalid_argument("An `entry` with the key `key` has already been added: " + NavKeyTraits<T>::Text(asKey) + ".");
            m_Providers.push_back({ asKey, contentKey ? *contentKey : DefaultContentKey(asKey),
                                    [metadata](const T& k) { return metadata(*NavKeyTraits<T>::template As<K>(k)); },
                                    [content](const T& k) { content(*NavKeyTraits<T>::template As<K>(k)); } });
        }
        // entry<K>(clazzContentKey, metadata, content): every key of class K.
        template<typename K>
        void Entry(kiln::Function<void(const K&)> content, NavMetadata metadata = {}, kiln::Function<std::string(const K&)> clazzContentKey = {}) {
            EntryWithMetadata<K>(std::move(content), [metadata](const K&) { return metadata; }, std::move(clazzContentKey));
        }
        template<typename K>
        void EntryWithMetadata(kiln::Function<void(const K&)> content, kiln::Function<NavMetadata(const K&)> metadata,
                               kiln::Function<std::string(const K&)> clazzContentKey = {}) {
            const std::type_index clazz(typeid(K));
            if (m_ClassProviders.contains(clazz)) throw std::invalid_argument(std::string("An `entry` with the same `clazz` has already been added: ") + clazz.name() + ".");
            m_ClassProviders.emplace(clazz, ClassProvider{
                [clazzContentKey](const T& k) { return clazzContentKey ? clazzContentKey(*NavKeyTraits<T>::template As<K>(k)) : DefaultContentKey(k); },
                [metadata](const T& k) { return metadata(*NavKeyTraits<T>::template As<K>(k)); },
                [content](const T& k) { content(*NavKeyTraits<T>::template As<K>(k)); } });
        }

        // The provider: key -> NavEntry. Metadata is computed once per key.
        kiln::Function<NavEntry<T>(const T&)> Build() const {
            struct State {
                std::vector<Provider> providers;
                std::map<std::type_index, ClassProvider> classProviders;
                Fallback fallback;
                std::map<std::string, NavMetadata> metadataCache;
            };
            auto s = std::make_shared<State>(State{ m_Providers, m_ClassProviders, m_Fallback, {} });
            return [s](const T& key) -> NavEntry<T> {
                const Provider* single = nullptr;
                for (const auto& p : s->providers) if (p.key == key) { single = &p; break; }
                auto cls = s->classProviders.find(NavKeyTraits<T>::ClassOf(key));
                const ClassProvider* byClass = cls == s->classProviders.end() ? nullptr : &cls->second;
                const std::string cacheKey = DefaultContentKey(key);
                auto it = s->metadataCache.find(cacheKey);
                if (it == s->metadataCache.end())
                    it = s->metadataCache.emplace(cacheKey, byClass ? byClass->metadata(key) : single ? single->metadata(key) : NavMetadata{}).first;
                if (single) return NavEntry<T>(key, single->contentKey, it->second, single->content);
                if (byClass) return NavEntry<T>(key, byClass->contentKey(key), it->second, byClass->content);
                if (s->fallback) return s->fallback(key);
                throw std::logic_error("Unknown screen " + NavKeyTraits<T>::Text(key));
            };
        }

    private:
        struct Provider {
            T key;
            std::string contentKey;
            kiln::Function<NavMetadata(const T&)> metadata;
            kiln::Function<void(const T&)> content;
        };
        struct ClassProvider {
            kiln::Function<std::string(const T&)> contentKey;
            kiln::Function<NavMetadata(const T&)> metadata;
            kiln::Function<void(const T&)> content;
        };
        Fallback m_Fallback;
        std::vector<Provider> m_Providers;
        std::map<std::type_index, ClassProvider> m_ClassProviders;
    };

    // entryProvider(fallback) { ... }
    template<typename T>
    kiln::Function<NavEntry<T>(const T&)> EntryProvider(const kiln::Function<void(EntryProviderScope<T>&)>& builder,
                                                       typename EntryProviderScope<T>::Fallback fallback = {}) {
        EntryProviderScope<T> scope(std::move(fallback));
        builder(scope);
        return scope.Build();
    }

}
