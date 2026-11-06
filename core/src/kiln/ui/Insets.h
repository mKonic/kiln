#pragma once

// Window insets (spec 02 req. 15, 06 §13 WindowInsets): per-type insets in window pixels, fed by
// the host from WindowInsets and, for the IME, every frame of WindowInsetsAnimation.

#include "kiln/runtime/State.h"

#include <array>
#include <cstdint>

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

    class WindowInsetsHolder {
    public:
        const InsetsValue& Get(InsetsType t) const { return m_Values[std::size_t(t)].Get(); }
        void Set(InsetsType t, InsetsValue v) { m_Values[std::size_t(t)].Set(v); }
        bool ImeVisible() const { return m_ImeVisible.Get(); }
        void SetImeVisible(bool v) { m_ImeVisible.Set(v); }
        bool ImeAnimating() const { return m_ImeAnimating.Get(); }
        void SetImeAnimating(bool v) { m_ImeAnimating.Set(v); }

    private:
        std::array<rt::State<InsetsValue>, std::size_t(InsetsType::Count)> m_Values;
        rt::State<bool> m_ImeVisible{ false }, m_ImeAnimating{ false };
    };

}
