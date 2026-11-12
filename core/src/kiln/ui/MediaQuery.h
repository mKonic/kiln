#pragma once

// Media queries (ui MediaQuery.kt, adaptive/MediaQuery.android.kt): what the window and the
// device are like, as observable values a composition branches on: the window's size and
// posture, the most precise pointer, the keyboard, microphone and camera, and how far away the
// screen is viewed from.

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/InputDevices.h"

#include <functional>
#include <vector>

namespace kiln::ui {

    class Owner;
    class ModifierNode;

    class UiMediaScope {
    public:
        enum class FoldState : std::uint8_t { Flat, HalfOpened };
        enum class FoldOrientation : std::uint8_t { Horizontal, Vertical };
        struct WindowFold {
            FoldState state = FoldState::Flat;
            FoldOrientation orientation = FoldOrientation::Vertical;
            bool operator==(const WindowFold&) const = default;
        };
        struct WindowPosture {
            std::vector<WindowFold> folds;
            bool IsFlat() const { for (const auto& f : folds) if (f.state == FoldState::HalfOpened) return false; return true; }
            bool IsTabletop() const {
                for (const auto& f : folds) if (f.state == FoldState::HalfOpened && f.orientation == FoldOrientation::Horizontal) return true;
                return false;
            }
            bool operator==(const WindowPosture&) const = default;
        };
        enum class PointerPrecision : std::uint8_t { Fine, Coarse, Blunt, None };
        enum class KeyboardKind : std::uint8_t { Physical, Virtual, None };
        enum class ViewingDistance : std::uint8_t { Near, Medium, Far };

        virtual ~UiMediaScope() = default;
        virtual WindowPosture GetWindowPosture() const = 0;
        virtual Dp WindowWidth() const = 0;
        virtual Dp WindowHeight() const = 0;
        virtual PointerPrecision GetPointerPrecision() const = 0;
        virtual KeyboardKind GetKeyboardKind() const = 0;
        virtual bool HasMicrophone() const = 0;
        virtual bool HasCamera() const = 0;
        virtual ViewingDistance GetViewingDistance() const = 0;
    };

    // resolvePointerPrecision: a mouse, stylus or touchpad is fine, a touchscreen coarse, a
    // joystick or gamepad blunt.
    UiMediaScope::PointerPrecision ResolvePointerPrecision(const std::vector<InputDeviceInfo>& devices);
    // hasPhysicalKeyboard: an alphabetic keyboard that is not virtual.
    bool HasPhysicalKeyboard(const std::vector<InputDeviceInfo>& devices);

    // The owner's media scope (UiMediaScopeImpl): the window size and IME from the owner, the
    // input devices from its InputDevices (listened to once read), the rest as the host reports.
    class OwnerMediaScope final : public UiMediaScope {
    public:
        struct Features {
            bool microphone = false, camera = false, television = false, automotive = false, docked = false;
            bool operator==(const Features&) const = default;
        };
        explicit OwnerMediaScope(Owner& owner) : m_Owner(owner) {}
        ~OwnerMediaScope() override;
        void SetFeatures(Features f) { m_Features.Set(f); }
        void SetWindowPosture(WindowPosture p) { m_Posture.Set(std::move(p)); }

        WindowPosture GetWindowPosture() const override { return m_Posture.Get(); }
        Dp WindowWidth() const override;
        Dp WindowHeight() const override;
        PointerPrecision GetPointerPrecision() const override;
        KeyboardKind GetKeyboardKind() const override;
        bool HasMicrophone() const override { return m_Features.Get().microphone; }
        bool HasCamera() const override { return m_Features.Get().camera; }
        ViewingDistance GetViewingDistance() const override;

    private:
        void Listen() const;
        Owner& m_Owner;
        rt::State<Features> m_Features;
        rt::State<WindowPosture> m_Posture;
        mutable bool m_Listening = false;
    };

    // The owner's media scope, created on first use (the host reports features to it).
    OwnerMediaScope& MediaScopeOf(Owner& owner);

    // LocalUiMediaScope: the owner's media scope unless one is provided.
    extern rt::Local<const UiMediaScope*> LocalUiMediaScope;

    // mediaQuery { }: the query's answer now; the composition re-runs when what it read changes.
    template<typename T>
    T MediaQuery(const std::function<T(const UiMediaScope&)>& query) {
        const UiMediaScope* scope = rt::Composition::Current()->Read(LocalUiMediaScope);
        return query(*scope);
    }
    // computedMediaQuery { }: the answer as derived state, so only readers of the result
    // re-run when it changes (not everything that read the window size).
    template<typename T>
    rt::Derived<T>& ComputedMediaQuery(std::function<T(const UiMediaScope&)> query, rt::Key key = {}) {
        rt::Composition* c = rt::Composition::Current();
        const UiMediaScope* scope = c->Read(LocalUiMediaScope);
        auto& current = c->Remember<std::shared_ptr<std::function<T(const UiMediaScope&)>>>(
            [] { return std::make_shared<std::function<T(const UiMediaScope&)>>(); }, {}, key);
        *current = std::move(query);   // rememberUpdatedState
        auto fn = current;
        return c->RememberNew<rt::Derived<T>>(rt::Keys(reinterpret_cast<std::uintptr_t>(scope)), key, [scope, fn] { return (*fn)(*scope); });
    }
    // CompositionLocalConsumerModifierNode.mediaQuery.
    const UiMediaScope& CurrentMediaScope(const ModifierNode& node);

}
