#pragma once

// Window insets (spec 02 req. 15, 06 §13 WindowInsets): per-type insets in window pixels, fed by
// the host from WindowInsets and, for the IME, every frame of WindowInsetsAnimation.

#include "kiln/runtime/State.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace kiln::ui {

    enum class InsetsType : std::uint8_t { StatusBars, NavigationBars, CaptionBar, Ime, DisplayCutout, SystemGestures,
                                           MandatorySystemGestures, TappableElement, Waterfall, Count };

    struct InsetsValue {
        int left = 0, top = 0, right = 0, bottom = 0;
        bool operator==(const InsetsValue&) const = default;
        InsetsValue Union(const InsetsValue& o) const {
            return { std::max(left, o.left), std::max(top, o.top), std::max(right, o.right), std::max(bottom, o.bottom) };
        }
    };

    // WindowInsetsAnimation of one type (WindowInsetsAnimationCompat): from source to target.
    struct InsetsAnimation {
        InsetsValue source, target;
        float fraction = 0;          // interpolated
        float alpha = 1;
        std::int64_t durationMillis = 0;
        bool operator==(const InsetsAnimation&) const = default;
    };

    struct InsetsRect {
        int left = 0, top = 0, right = 0, bottom = 0;
        bool operator==(const InsetsRect&) const = default;
    };

    class WindowInsetsHolder {
    public:
        // Current insets (zero while the type is hidden).
        const InsetsValue& Get(InsetsType t) const { return At(t).current.Get(); }
        void Set(InsetsType t, InsetsValue v) { At(t).current.Set(v); }
        // getInsetsIgnoringVisibility: what the type takes when shown (the IME has none).
        const InsetsValue& GetIgnoringVisibility(InsetsType t) const { return At(t).maximum.Get(); }
        void SetIgnoringVisibility(InsetsType t, InsetsValue v) { At(t).maximum.Set(v); }
        bool IsVisible(InsetsType t) const { return At(t).visible.Get(); }
        void SetVisible(InsetsType t, bool v) { At(t).visible.Set(v); }
        // The running animation of a type, if any.
        const std::optional<InsetsAnimation>& Animation(InsetsType t) const { return At(t).animation.Get(); }
        void SetAnimation(InsetsType t, std::optional<InsetsAnimation> a) { At(t).animation.Set(std::move(a)); }
        bool IsAnimating(InsetsType t) const { return Animation(t).has_value(); }
        // DisplayCutout.getBoundingRects, window pixels.
        const std::vector<InsetsRect>& CutoutRects() const { return m_Cutouts.Get(); }
        void SetCutoutRects(std::vector<InsetsRect> r) { m_Cutouts.Set(std::move(r)); }
        // Whether the host has delivered insets at all (rulers are provided only then).
        bool HasInsets() const { return m_Has.Get(); }
        void SetHasInsets(bool v) { m_Has.Set(v); }

        // Older names for the IME.
        bool ImeVisible() const { return IsVisible(InsetsType::Ime); }
        void SetImeVisible(bool v) { SetVisible(InsetsType::Ime, v); }
        bool ImeAnimating() const { return IsAnimating(InsetsType::Ime); }

    private:
        struct PerType {
            rt::State<InsetsValue> current, maximum;
            rt::State<bool> visible{ false };
            rt::State<std::optional<InsetsAnimation>> animation;
        };
        PerType& At(InsetsType t) { return m_Types[std::size_t(t)]; }
        const PerType& At(InsetsType t) const { return m_Types[std::size_t(t)]; }
        std::array<PerType, std::size_t(InsetsType::Count)> m_Types;
        rt::State<std::vector<InsetsRect>> m_Cutouts;
        rt::State<bool> m_Has{ false };
    };

}
