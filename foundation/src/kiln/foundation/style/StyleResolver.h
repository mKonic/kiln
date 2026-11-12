#pragma once

// StyleResolver (foundation style/StyleResolver.kt, StyleResolverNode.kt,
// StylePropertyCollector.kt, StyleAnimations.kt): runs a CommonStyle for an element and keeps
// the values it provided. Bind it to the element with StyleResolverMod (Modifier.styleResolver);
// read the values with Resolve. Resolution runs on first read after binding and again whenever
// what the style read changes (state, composition locals, other state). Locals are inherited
// from the nearest ancestor resolver that provides them. Values provided under animate { }
// animate between their old and new values.
//
// The predefined properties at the end are what the style* modifiers read
// (StylePadding / Placement / Appearance / StyleGraphicsLayer).

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/style/Style.h"
#include "kiln/runtime/Task.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kiln::foundation {

    class StyleResolver;
    class StyleResolverNode;

    // StyleResolverScope: StylePropertyAccessorScope, composition locals and density.
    class StyleResolverScope : public StylePropertyAccessorScope {
    public:
        virtual const ui::Density& GetDensity() const = 0;
        float ToPx(ui::Dp d) const { return GetDensity().ToPx(d); }
        int RoundToPx(ui::Dp d) const { return GetDensity().RoundToPx(d); }
        ui::Dp ToDp(float px) const { return GetDensity().ToDp(px); }
        template<typename T> const T& CurrentValue(const rt::Local<T>& local) const { return ui::CurrentValueOf(*LocalNode(), local); }
        virtual const ui::ModifierNode* LocalNode() const = 0;
    };

    namespace detail {
        // The values one resolution provided (StyleProperties).
        class StyleValues {
        public:
            void Set(const StylePropertyBase& p, std::any v) { m_Values[&p] = std::move(v); }
            const std::any* Find(const StylePropertyBase& p) const {
                auto it = m_Values.find(&p);
                return it == m_Values.end() ? nullptr : &it->second;
            }
            bool Contains(const StylePropertyBase& p) const { return m_Values.contains(&p); }
            std::any Get(const StylePropertyBase& p) const {
                const std::any* v = Find(p);
                return v ? *v : p.DefaultAny();
            }
            void Clear() { m_Values.clear(); }
            const std::unordered_map<const StylePropertyBase*, std::any>& Map() const { return m_Values; }
            // diffInto: properties whose value differs or that only one side has.
            std::unordered_set<const StylePropertyBase*> Diff(const StyleValues& other) const;
        private:
            std::unordered_map<const StylePropertyBase*, std::any> m_Values;
        };

        // StyleAnimations: a running animation per property, from the value it was showing to
        // the new one.
        class StyleAnimations {
        public:
            enum class EntryState : std::uint8_t { Untouched, Unchanged, Changed, Inserted, Interrupted, Removing };
            struct Entry {
                anim::SpecPtr spec;
                std::any start, end;
                anim::Animatable animation{ 0.0f };
                EntryState state = EntryState::Inserted;
                rt::Job job;
            };
            ~StyleAnimations() { Close(); }
            bool Empty() const { return m_Entries.empty(); }
            bool Animating(const StylePropertyBase& p) const { return m_Entries.contains(&p); }
            // The animated value of `p` while it animates (observed), else `orElse`.
            std::any AnimatedValueOrElse(const StylePropertyBase& p, const std::function<std::any()>& orElse) const;
            void Record(const std::unordered_set<const StylePropertyBase*>& animating, const std::unordered_set<const StylePropertyBase*>& changes,
                        const std::unordered_map<const StylePropertyBase*, anim::SpecPtr>* toSpecs,
                        const std::unordered_map<const StylePropertyBase*, anim::SpecPtr>* fromSpecs,
                        const std::unordered_map<const StylePropertyBase*, anim::SpecPtr>* previousFromSpecs, const StyleValues& start,
                        const StyleValues& end);
            void Close();
        private:
            void Animate(Entry& e);
            void Cleanup();
            std::unordered_map<const StylePropertyBase*, std::unique_ptr<Entry>> m_Entries;
            rt::State<int> m_Version{ 0 };   // the entry set changed
            std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
        };

        // ResolvedProperties: each resolved value as state (equal values don't notify), animated
        // while its property animates.
        class ResolvedProperties {
        public:
            bool Contains(const StylePropertyBase& p) const;
            std::any GetOrNull(const StylePropertyBase& p, const StyleAnimations& animations) const;
            void Update(const StylePropertyBase& p, std::any value);
            std::vector<const StylePropertyBase*> Keys() const;
            template<typename F> void RemoveIf(F&& pred) {
                bool removed = false;
                for (auto it = m_Map.begin(); it != m_Map.end();) {
                    if (pred(*it->first)) { it = m_Map.erase(it); removed = true; }
                    else ++it;
                }
                if (removed) m_Version.Set(m_Version.Peek() + 1);
            }
        private:
            struct Value {
                rt::State<std::any, rt::NeverEqual> current;
            };
            std::unordered_map<const StylePropertyBase*, std::unique_ptr<Value>> m_Map;
            rt::State<int> m_Version{ 0 };
        };

        class StylePropertyCollector;
    }

    class StyleResolver : public std::enable_shared_from_this<StyleResolver> {
    public:
        explicit StyleResolver(CommonStyle style, std::shared_ptr<StyleState> styleState = std::make_shared<MutableStyleState>());
        ~StyleResolver();

        // The style and state it resolves (observable; setting them resolves again).
        const CommonStyle& Style() const { return m_Style.Get(); }
        void SetStyle(CommonStyle style) { m_Style.Set(std::move(style)); }
        const std::shared_ptr<StyleState>& GetStyleState() const { return m_State.Get(); }
        void SetStyleState(std::shared_ptr<StyleState> state) { m_State.Set(std::move(state)); }

        // resolve { }: read the resolved values. Throws before the resolver is bound to an
        // element and resolved (values are not available in the composition that creates it).
        template<typename F> decltype(auto) Resolve(F&& block) { return block(static_cast<const StyleResolverScope&>(AccessorScope())); }

        // Internal (the node and the collector).
        void Bind(StyleResolverNode* node);
        void Unbind();
        void Dispose();
        void Collect();
        void UpdateProperties(const detail::StyleValues& values, const std::unordered_set<const NestedStyleKey*>* nestedKeys);
        void DiscardUnneededProperties(const detail::StyleValues& current, const detail::StyleValues& previous);
        void ResolveNestedStyle(const NestedStyleKey& key, const std::function<void(const CommonStyle&, const StyleState&)>& block);
        std::any ResolveLocal(const StylePropertyBase& p) const;
        void TraverseAncestors(const std::function<bool(StyleResolver&)>& block) const;
        StyleResolverNode* Node() const { return m_Node; }
        detail::StyleAnimations& Animations() { return m_Animations; }
        const detail::ResolvedProperties& Properties() const { return m_Properties; }
        const detail::ResolvedProperties& Locals() const { return m_Locals; }
        // Testing hook (bindParent): the parent chain without nodes.
        void BindParent(StyleResolver* parent) { m_ParentOverride = parent; }

    private:
        class Scope;
        const StyleResolverScope& AccessorScope();
        rt::State<CommonStyle, rt::NeverEqual> m_Style;
        rt::State<std::shared_ptr<StyleState>> m_State;
        detail::ResolvedProperties m_Properties, m_Locals;
        detail::StyleAnimations m_Animations;
        std::unique_ptr<detail::StylePropertyCollector> m_Collector;
        std::unique_ptr<Scope> m_Scope;
        rt::State<std::vector<const NestedStyleKey*>> m_NestedKeys;
        StyleResolverNode* m_Node = nullptr;
        StyleResolver* m_ParentOverride = nullptr;
        bool m_ResolvedAtLeastOnce = false;
    };

    // rememberStyleResolver(style, styleState): a resolver remembered here, updated to `style`
    // and `styleState` on each composition.
    std::shared_ptr<StyleResolver> RememberStyleResolver(CommonStyle style, std::shared_ptr<StyleState> styleState = nullptr);

    // Modifier.styleResolver: binds `resolver` to this element (one element per resolver).
    Modifier StyleResolverMod(Modifier m, std::shared_ptr<StyleResolver> resolver);

    // The node StyleResolverMod adds (TraversableNode keyed by the resolver kind).
    class StyleResolverNode final : public ui::ModifierNode, public ui::TraversableNode {
    public:
        struct Params {
            std::shared_ptr<StyleResolver> resolver;
            bool operator==(const Params&) const = default;
        };
        explicit StyleResolverNode(Params p) : params(std::move(p)) {}
        Params params;
        void OnParamsChanged();
        void OnAttach() override;
        void OnDetach() override;
        ui::TraversableNode* AsTraversable() override { return this; }
        const void* TraverseKey() const override;
        void OnObservedReadsChanged() override { ResolveStyle(); }
        void EnsureResolved();
        ui::Density RequireDensity() const;
        StyleResolver& Resolver() const { return *params.resolver; }
    private:
        void ResolveStyle();
        std::shared_ptr<StyleResolver> m_Bound;
        bool m_Resolved = false;
        std::shared_ptr<MutableInteractionSource> m_Source;
        std::function<void()> m_StopInteractions;
    };

    // contentFill / contentColor / contentBrush: the content fill local where the style resolves.
    Fill ContentFill(const StylePropertyAccessorScope& scope);
    ui::Color ContentColor(const StylePropertyAccessorScope& scope);
    std::optional<gfx::Brush> ContentBrush(const StylePropertyAccessorScope& scope);

    // The predefined properties (StyleProperties.kt).
    namespace style {
        const StylePropertyRef<ui::Dp>& ContentPaddingStart();
        const StylePropertyRef<ui::Dp>& ContentPaddingEnd();
        const StylePropertyRef<ui::Dp>& ContentPaddingTop();
        const StylePropertyRef<ui::Dp>& ContentPaddingBottom();
        const StylePropertyRef<ui::Dp>& ExternalPaddingStart();
        const StylePropertyRef<ui::Dp>& ExternalPaddingEnd();
        const StylePropertyRef<ui::Dp>& ExternalPaddingTop();
        const StylePropertyRef<ui::Dp>& ExternalPaddingBottom();
        const StylePropertyRef<Breadth>& Width();
        const StylePropertyRef<Breadth>& Height();
        const StylePropertyRef<ui::Dp>& Left();
        const StylePropertyRef<ui::Dp>& Right();
        const StylePropertyRef<ui::Dp>& Top();
        const StylePropertyRef<ui::Dp>& Bottom();
        const StylePropertyRef<ui::Dp>& MinWidth();
        const StylePropertyRef<ui::Dp>& MaxWidth();
        const StylePropertyRef<ui::Dp>& MinHeight();
        const StylePropertyRef<ui::Dp>& MaxHeight();
        const StylePropertyRef<Fill>& BorderFill();
        const StylePropertyRef<ui::Dp>& BorderWidth();
        const StylePropertyRef<Fill>& Background();
        const StylePropertyRef<Fill>& Foreground();
        const StylePropertyRef<bool>& Clip();
        const StylePropertyRef<ShapePtr>& Shape();
        const StylePropertyRef<Shadows>& DropShadow();
        const StylePropertyRef<Shadows>& InnerShadow();
        const StylePropertyRef<float>& Alpha();
        const StylePropertyRef<float>& ScaleX();
        const StylePropertyRef<float>& ScaleY();
        const StylePropertyRef<float>& TranslationX();
        const StylePropertyRef<float>& TranslationY();
        const StylePropertyRef<float>& RotationX();
        const StylePropertyRef<float>& RotationY();
        const StylePropertyRef<float>& RotationZ();
        const StylePropertyRef<float>& TransformOriginX();
        const StylePropertyRef<float>& TransformOriginY();
        const StylePropertyRef<float>& CameraDistance();
        const StylePropertyRef<float>& ZIndex();
        const StylePropertyRef<std::optional<gfx::ColorFilter>>& ColorFilter();
        const StylePropertyRef<Fill>& ContentFillLocal();
    }

}
