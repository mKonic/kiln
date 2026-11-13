#pragma once

// Style properties (foundation style/StyleProperty.kt): named values a style provides and a
// StyleResolver resolves. A property is either a plain property (read on the element whose style
// set it) or a local (read on that element and every element below it, until one sets it again).
// Values of a property animate between each other with its lerp. A property's identity is its
// address: define each one once (stylePropertyOf / styleLocalOf) and keep it.
//
// Breadth (a width or height: a distance, a fraction of the space, or none), Fill (a colour or a
// brush) and Shadows (one or several painted shadows) are the value types the predefined
// properties use.

#include "kiln/foundation/Shapes.h"
#include <kiln/Function.h>
#include "kiln/graphics/Brush.h"
#include "kiln/graphics/shadow/Shadow.h"
#include "kiln/text/TextStyle.h"
#include "kiln/ui/ColorSpace.h"
#include "kiln/ui/Geometry.h"

#include <any>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace kiln::foundation {

    // --- value types ----------------------------------------------------------------------------

    // Breadth: Distance (dp), Fraction (of the incoming maximum) or None.
    struct Breadth {
        enum class Kind : std::uint8_t { None, Distance, Fraction };
        Kind kind = Kind::None;
        ui::Dp distance{ 0 };
        float fraction = 0;
        constexpr Breadth() = default;
        // Breadth(value: Dp) / Breadth(value: Float).
        constexpr explicit Breadth(ui::Dp value) : kind(Kind::Distance), distance(value) {}
        constexpr explicit Breadth(float value) : kind(Kind::Fraction), fraction(value) {}
        static constexpr Breadth None() { return {}; }
        bool IsDistance() const { return kind == Kind::Distance; }
        bool IsFraction() const { return kind == Kind::Fraction; }
        bool operator==(const Breadth& o) const {
            return kind == o.kind && (kind == Kind::None || (kind == Kind::Distance ? distance == o.distance : fraction == o.fraction));
        }
    };
    // Breadth.lerp: None reads as the zero of the other kind; different kinds don't interpolate.
    std::optional<Breadth> LerpBreadth(const Breadth& a, const Breadth& b, float t);

    // Fill: a colour, a brush, or None (unspecified colour, no brush).
    struct Fill {
        enum class Kind : std::uint8_t { None, Color, Brush };
        Kind kind = Kind::None;
        ui::Color color = ui::Color::Unspecified();
        gfx::Brush brush;
        Fill() = default;
        explicit Fill(ui::Color c) : kind(Kind::Color), color(c) {}
        explicit Fill(gfx::Brush b) : kind(Kind::Brush), brush(std::move(b)) {}
        static Fill None() { return {}; }
        // asColor: the colour, a solid brush's colour, else unspecified. asBrush: the brush, a
        // colour as a solid brush, none for None.
        ui::Color AsColor() const;
        std::optional<gfx::Brush> AsBrush() const;
        bool operator==(const Fill& o) const {
            if (kind != o.kind) return false;
            if (kind == Kind::Color) return color == o.color;
            if (kind == Kind::Brush) return brush == o.brush;
            return true;
        }
    };
    // Fill.lerp: two colours (None is a colour) in Oklab; otherwise the brushes between them.
    Fill LerpFill(const Fill& a, const Fill& b, float t);

    // Shadows: one (Simple), several drawn together (Compound), or None.
    struct Shadows {
        enum class Kind : std::uint8_t { None, Simple, Compound };
        Kind kind = Kind::None;
        std::vector<gfx::shadow::Shadow> shadows;   // one for Simple
        Shadows() = default;
        explicit Shadows(gfx::shadow::Shadow s) : kind(Kind::Simple), shadows{ std::move(s) } {}
        explicit Shadows(std::vector<gfx::shadow::Shadow> s) : kind(Kind::Compound), shadows(std::move(s)) {}
        static Shadows None() { return {}; }
        bool operator==(const Shadows&) const = default;
    };
    // Shadows.lerp: two simple shadows (or a simple one and None, which fades it) interpolate;
    // anything else switches at t = 0.5.
    Shadows LerpShadows(const Shadows& a, const Shadows& b, float t);

    // lerp(Dp, Dp) / lerp(TextUnit, TextUnit) of the style package: unspecified and negative
    // distances read as 0; text units interpolate only within sp or em.
    ui::Dp StyleLerp(ui::Dp a, ui::Dp b, float t);
    text::TextUnit StyleLerp(text::TextUnit a, text::TextUnit b, float t);

    // defaultInterpolate: the value types that interpolate, else a switch at t = 0.5.
    template<typename T>
    T DefaultStyleInterpolate(const T& a, const T& b, float t) {
        if constexpr (std::is_same_v<T, Breadth>) {
            if (a == b) return t < 0.5f ? a : b;
            if (auto r = LerpBreadth(a, b, t)) return *r;
            return t < 0.5f ? a : b;
        } else if constexpr (std::is_same_v<T, Fill>) {
            if (a == b) return t < 0.5f ? a : b;
            return LerpFill(a, b, t);
        } else if constexpr (std::is_same_v<T, Shadows>) {
            if (a == b) return t < 0.5f ? a : b;
            return LerpShadows(a, b, t);
        } else if constexpr (std::is_same_v<T, ui::Dp> || std::is_same_v<T, text::TextUnit>) {
            return StyleLerp(a, b, t);
        } else if constexpr (std::is_same_v<T, float>) {
            return a + (b - a) * t;
        } else if constexpr (std::is_same_v<T, int>) {
            return a + int(std::lround(double(b - a) * t));
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            return a + std::int64_t(std::llround(double(b - a) * t));
        } else if constexpr (std::is_same_v<T, ui::Color>) {
            return ui::Lerp(a, b, t);
        } else if constexpr (std::is_same_v<T, gfx::Brush>) {
            return gfx::Lerp(a, &b, t);
        } else if constexpr (std::is_same_v<T, gfx::shadow::Shadow>) {
            return gfx::shadow::Lerp(a, b, t);
        } else if constexpr (std::is_same_v<T, ShapePtr>) {
            return Lerp(a, b, t);
        } else {
            return t < 0.5f ? a : b;
        }
    }

    // --- properties -----------------------------------------------------------------------------

    // The untyped side of a property: what the resolver stores and compares.
    class StylePropertyBase {
    public:
        StylePropertyBase(std::string name, bool isLocal) : m_Name(std::move(name)), m_IsLocal(isLocal) {}
        virtual ~StylePropertyBase() = default;
        const std::string& Name() const { return m_Name; }
        bool IsLocal() const { return m_IsLocal; }
        virtual std::any DefaultAny() const = 0;
        virtual std::any LerpAny(const std::any& a, const std::any& b, float t) const = 0;
        virtual bool EqualAny(const std::any& a, const std::any& b) const = 0;
        // toString: "<name> local" or "<name> property".
        std::string ToString() const { return m_Name + (m_IsLocal ? " local" : " property"); }
    private:
        std::string m_Name;
        bool m_IsLocal;
    };

    // StyleProperty<T>: its name, whether it is a local, its default, and how values interpolate
    // (overridable; the base switches at t = 0.5).
    template<typename T>
    class StyleProperty : public StylePropertyBase {
    public:
        StyleProperty(std::string name, bool isLocal, kiln::Function<T()> defaultValue)
            : StylePropertyBase(std::move(name), isLocal), m_Default(std::move(defaultValue)) {}
        T DefaultValue() const { return m_Default(); }
        virtual T Lerp(const T& a, const T& b, float t) const { return t < 0.5f ? a : b; }

        std::any DefaultAny() const override { return std::any(DefaultValue()); }
        std::any LerpAny(const std::any& a, const std::any& b, float t) const override {
            return std::any(Lerp(std::any_cast<const T&>(a), std::any_cast<const T&>(b), t));
        }
        bool EqualAny(const std::any& a, const std::any& b) const override {
            if constexpr (std::equality_comparable<T>) return std::any_cast<const T&>(a) == std::any_cast<const T&>(b);
            else return false;
        }
    private:
        kiln::Function<T()> m_Default;
    };

    // ProvidableStyleProperty<T>: a property a style can provide.
    template<typename T>
    class ProvidableStyleProperty : public StyleProperty<T> {
    public:
        using StyleProperty<T>::StyleProperty;
    };

    namespace detail {
        template<typename T>
        class ObjectStyleProperty final : public ProvidableStyleProperty<T> {
        public:
            ObjectStyleProperty(std::string name, bool local, kiln::Function<T(const T&, const T&, float)> interpolate, kiln::Function<T()> def)
                : ProvidableStyleProperty<T>(std::move(name), local, std::move(def)), m_Interpolate(std::move(interpolate)) {}
            T Lerp(const T& a, const T& b, float t) const override { return m_Interpolate(a, b, t); }
        private:
            kiln::Function<T(const T&, const T&, float)> m_Interpolate;
        };
    }

    template<typename T> using StylePropertyRef = std::shared_ptr<const ProvidableStyleProperty<T>>;

    // stylePropertyOf(name, interpolate, defaultValue): a property (not inherited).
    template<typename T>
    StylePropertyRef<T> StylePropertyOf(std::string name, kiln::Function<T()> defaultValue,
                                        kiln::Function<T(const T&, const T&, float)> interpolate = &DefaultStyleInterpolate<T>) {
        return std::make_shared<detail::ObjectStyleProperty<T>>(std::move(name), false, std::move(interpolate), std::move(defaultValue));
    }
    // styleLocalOf(name, interpolate, defaultValue): a local (inherited by the elements below).
    template<typename T>
    StylePropertyRef<T> StyleLocalOf(std::string name, kiln::Function<T()> defaultValue,
                                     kiln::Function<T(const T&, const T&, float)> interpolate = &DefaultStyleInterpolate<T>) {
        return std::make_shared<detail::ObjectStyleProperty<T>>(std::move(name), true, std::move(interpolate), std::move(defaultValue));
    }

    // StylePropertyAccessorScope: reading resolved properties.
    class StylePropertyAccessorScope {
    public:
        virtual ~StylePropertyAccessorScope() = default;
        // The resolved value (an inherited local for a local), else the default.
        template<typename T> T Value(const StyleProperty<T>& p) const {
            std::any v = ValueAny(p);
            return v.has_value() ? std::any_cast<T>(std::move(v)) : p.DefaultValue();
        }
        template<typename T> T Value(const StylePropertyRef<T>& p) const { return Value<T>(*p); }
        // isSet: some style provided it (for a local: here or above).
        virtual bool IsSet(const StylePropertyBase& p) const = 0;
        bool IsSet(const std::shared_ptr<const StylePropertyBase>& p) const { return IsSet(*p); }
        template<typename T> std::optional<T> GetOrNull(const StyleProperty<T>& p) const {
            std::any v = GetOrNullAny(p);
            if (!v.has_value()) return std::nullopt;
            return std::any_cast<T>(std::move(v));
        }
        template<typename T> std::optional<T> GetOrNull(const StylePropertyRef<T>& p) const { return GetOrNull<T>(*p); }
        // anySet: any of them is set.
        virtual bool AnySet(const std::vector<const StylePropertyBase*>& properties) const = 0;

        virtual std::any ValueAny(const StylePropertyBase& p) const = 0;
        virtual std::any GetOrNullAny(const StylePropertyBase& p) const = 0;
    };

    // ifSet / ifSetAs / getOrElse.
    template<typename T, typename F>
    void IfSet(const StylePropertyAccessorScope& scope, const StylePropertyRef<T>& p, F&& block) {
        if (auto v = scope.GetOrNull<T>(*p)) block(*v);
    }
    namespace detail {
        template<typename X> struct IsSharedPtr : std::false_type {};
        template<typename X> struct IsSharedPtr<std::shared_ptr<X>> : std::true_type {};
    }
    // ifSetAs<R>: the value when it is an R (shared pointers narrow with a dynamic cast).
    template<typename R, typename T, typename F>
    void IfSetAs(const StylePropertyAccessorScope& scope, const StylePropertyRef<T>& p, F&& block) {
        auto v = scope.GetOrNull<T>(*p);
        if (!v) return;
        if constexpr (detail::IsSharedPtr<R>::value && detail::IsSharedPtr<T>::value) {
            if (auto r = std::dynamic_pointer_cast<typename R::element_type>(*v)) block(r);
        } else if constexpr (std::is_same_v<R, T>) {
            block(*v);
        }
    }
    template<typename T, typename F>
    T GetOrElse(const StylePropertyAccessorScope& scope, const StylePropertyRef<T>& p, F&& defaultValue) {
        return scope.IsSet(*p) ? scope.Value<T>(*p) : defaultValue();
    }

    // StylePropertyProviderScope: providing values (a style's provide calls).
    class StylePropertyProviderScope {
    public:
        virtual ~StylePropertyProviderScope() = default;
        template<typename T> void Provide(const ProvidableStyleProperty<T>& p, T value) { ProvideAny(p, std::any(std::move(value))); }
        template<typename T, typename V> void Provide(const StylePropertyRef<T>& p, V&& value) { Provide<T>(*p, T(std::forward<V>(value))); }
        virtual void ProvideAny(const StylePropertyBase& p, std::any value) = 0;
    };

}
