#pragma once

// Styles (foundation style/CustomStyle.kt, CommonStyle.kt, StyleScope.kt, NestedStyles.kt,
// StyleLayerScope.kt): a style is a block that provides style property values in a scope.
// CustomStyle<Scope> is the general form a design system builds on; CommonStyle runs in a
// CommonStyleScope, which adds:
//   - state:      provide values only while a StyleState key is active (pressed { }, ...);
//   - animate:    values provided inside animate { } animate when they change (to and from);
//   - styleLayer: values provided in a later (deeper) layer win over earlier ones;
//   - nested styles: an ancestor provides a style for a key (provideNestedStyle) that the
//     elements below apply (applyNestedStyle), in the ancestor's state.
// Styles combine with Then (the later one wins where both provide a property).

#include "kiln/animation/Spec.h"
#include "kiln/foundation/style/StyleProperty.h"
#include "kiln/foundation/style/StyleState.h"
#include "kiln/ui/LayoutNode.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kiln::foundation {

    // CustomStyleScope: Density, CompositionLocalAccessorScope and StylePropertyProviderScope.
    class CustomStyleScope : public StylePropertyProviderScope {
    public:
        virtual const ui::Density& GetDensity() const = 0;
        float ToPx(ui::Dp d) const { return GetDensity().ToPx(d); }
        int RoundToPx(ui::Dp d) const { return GetDensity().RoundToPx(d); }
        ui::Dp ToDp(float px) const { return GetDensity().ToDp(px); }
        // CompositionLocal.currentValue where the style resolves.
        template<typename T> const T& CurrentValue(const rt::Local<T>& local) const { return ui::CurrentValueOf(*LocalNode(), local); }
        virtual const ui::ModifierNode* LocalNode() const = 0;
    };

    // CustomStyle<Scope>: one or more blocks applied in order (the identity is the blocks').
    template<typename Scope>
    class CustomStyle {
    public:
        using Block = std::function<void(Scope&)>;
        CustomStyle() = default;
        CustomStyle(Block block) {
            if (block) m_Parts.push_back(std::make_shared<const Block>(std::move(block)));
        }
        // applyStyle.
        void ApplyStyle(Scope& scope) const {
            for (const auto& p : m_Parts) (*p)(scope);
        }
        bool Empty() const { return m_Parts.empty(); }
        bool operator==(const CustomStyle& o) const { return m_Parts == o.m_Parts; }
    protected:
        std::vector<std::shared_ptr<const Block>> m_Parts;
    };

    class CommonStyle;

    // AnimateStyleScope: values provided in `block` animate to (toSpec) and back from (fromSpec)
    // their new values.
    class AnimateStyleScope {
    public:
        virtual ~AnimateStyleScope() = default;
        virtual void Animate(anim::SpecPtr toSpec, anim::SpecPtr fromSpec, const std::function<void()>& block) = 0;
        // animate(block): the default spring both ways; animate(spec, block): `spec` both ways.
        void Animate(const std::function<void()>& block) { Animate(DefaultSpec(), DefaultSpec(), block); }
        void Animate(anim::SpecPtr spec, const std::function<void()>& block) { Animate(spec, spec, block); }
        static const anim::SpecPtr& DefaultSpec();
    };

    // StyleStateScope: the state, and blocks that run only while a key is active.
    class StyleStateScope {
    public:
        virtual ~StyleStateScope() = default;
        virtual const StyleState& State() const = 0;
        template<typename T>
        void State(const StyleStateKey<T>& key, const std::function<void()>& block,
                   const std::function<bool(const StyleStateKey<T>&, const StyleState&)>& active) {
            StateAny(key, block, [&] { return active(key, State()); });
        }
        virtual void StateAny(const StyleStateKeyBase& key, const std::function<void()>& block, const std::function<bool()>& active) = 0;
    };
    // state(key, block) for a Boolean key, and the predefined states.
    void State(StyleStateScope& scope, const StyleStateKey<bool>& key, const std::function<void()>& block);
    void Pressed(StyleStateScope& scope, const std::function<void()>& block);
    void Hovered(StyleStateScope& scope, const std::function<void()>& block);
    void Focused(StyleStateScope& scope, const std::function<void()>& block);
    void Selected(StyleStateScope& scope, const std::function<void()>& block);
    void Disabled(StyleStateScope& scope, const std::function<void()>& block);
    void Checked(StyleStateScope& scope, const std::function<void()>& block);
    void TriStateToggleOn(StyleStateScope& scope, const std::function<void()>& block);
    void TriStateToggleOff(StyleStateScope& scope, const std::function<void()>& block);
    void TriStateToggleIndeterminate(StyleStateScope& scope, const std::function<void()>& block);

    // NestedStyleKey: names a nested style (identity is the key's address).
    class NestedStyleKey {
    public:
        explicit NestedStyleKey(std::string label = {}) : m_Label(std::move(label)) {}
        const std::string& Label() const { return m_Label; }
        std::string ToString() const { return m_Label.empty() ? "NestedStyleKey" : "NestedStyleKey(label = " + m_Label + ")"; }
    private:
        std::string m_Label;
    };
    class NestedStyleProviderStyleScope {
    public:
        virtual ~NestedStyleProviderStyleScope() = default;
        virtual void ProvideNestedStyle(const NestedStyleKey& key, const CommonStyle& style) = 0;
    };
    class NestedStyleConsumerStyleScope {
    public:
        virtual ~NestedStyleConsumerStyleScope() = default;
        virtual void ApplyNestedStyle(const NestedStyleKey& key) = 0;
    };

    // StyleLayerScope: what `block` provides wins over what earlier layers provided.
    class StyleLayerScope {
    public:
        virtual ~StyleLayerScope() = default;
        virtual void StyleLayer(const std::function<void()>& block) = 0;
    };

    class CommonStyleScope : public CustomStyleScope,
                             public AnimateStyleScope,
                             public StyleStateScope,
                             public NestedStyleProviderStyleScope,
                             public NestedStyleConsumerStyleScope,
                             public StyleLayerScope {
    public:
        using AnimateStyleScope::Animate;
        using StyleStateScope::State;
    };

    // CommonStyle: the empty style by default.
    class CommonStyle : public CustomStyle<CommonStyleScope> {
    public:
        using CustomStyle<CommonStyleScope>::CustomStyle;
        CommonStyle() = default;
        // apply(style) inside a style: run another style here.
        void Apply(CommonStyleScope& scope) const { ApplyStyle(scope); }
        // then: this style, then `other` (flattened; the empty style drops out).
        CommonStyle Then(const CommonStyle& other) const {
            if (Empty()) return other;
            if (other.Empty()) return *this;
            CommonStyle r = *this;
            r.m_Parts.insert(r.m_Parts.end(), other.m_Parts.begin(), other.m_Parts.end());
            return r;
        }
    };
    // CommonStyle(style1, style2, ...): all of them in order.
    template<typename... Rest>
    CommonStyle CommonStyleOf(const CommonStyle& first, const Rest&... rest) {
        CommonStyle r = first;
        ((r = r.Then(rest)), ...);
        return r;
    }

}
