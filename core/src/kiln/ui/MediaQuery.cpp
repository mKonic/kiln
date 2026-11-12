#include "kiln/ui/MediaQuery.h"

#include "kiln/ui/Insets.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

namespace kiln::ui {

    namespace {
        // InputDevice.SOURCE_* values.
        constexpr std::uint32_t SourceTouchscreen = 0x1002, SourceMouse = 0x2002, SourceStylus = 0x4002, SourceTouchpad = 0x100008,
                                SourceJoystick = 0x1000010, SourceGamepad = 0x401;

        const UiMediaScope* OwnerScope(Owner* owner) { return owner ? &MediaScopeOf(*owner) : nullptr; }

        // Answers when there is no owner: a window of no size, nothing attached.
        struct NoMediaScope final : UiMediaScope {
            WindowPosture GetWindowPosture() const override { return {}; }
            Dp WindowWidth() const override { return Dp(0); }
            Dp WindowHeight() const override { return Dp(0); }
            PointerPrecision GetPointerPrecision() const override { return PointerPrecision::None; }
            KeyboardKind GetKeyboardKind() const override { return KeyboardKind::None; }
            bool HasMicrophone() const override { return false; }
            bool HasCamera() const override { return false; }
            ViewingDistance GetViewingDistance() const override { return ViewingDistance::Near; }
        };
        const NoMediaScope g_NoMedia;
    }

    OwnerMediaScope& MediaScopeOf(Owner& owner) {
        auto& slot = owner.Service("UiMediaScope");
        if (!slot) slot = std::make_shared<OwnerMediaScope>(owner);
        return *static_cast<OwnerMediaScope*>(slot.get());
    }

    rt::Local<const UiMediaScope*> LocalUiMediaScope = rt::LocalWithComputedDefault<const UiMediaScope*>([](const rt::LocalAccessorScope& s) {
        const UiMediaScope* scope = OwnerScope(s.CurrentValue(LocalOwner));
        return scope ? scope : static_cast<const UiMediaScope*>(&g_NoMedia);
    });

    UiMediaScope::PointerPrecision ResolvePointerPrecision(const std::vector<InputDeviceInfo>& devices) {
        auto precision = UiMediaScope::PointerPrecision::None;
        for (const InputDeviceInfo& d : devices) {
            if (d.HasSource(SourceMouse) || d.HasSource(SourceStylus) || d.HasSource(SourceTouchpad)) return UiMediaScope::PointerPrecision::Fine;
            if (d.HasSource(SourceTouchscreen)) precision = UiMediaScope::PointerPrecision::Coarse;
            else if (precision == UiMediaScope::PointerPrecision::None && (d.HasSource(SourceJoystick) || d.HasSource(SourceGamepad)))
                precision = UiMediaScope::PointerPrecision::Blunt;
        }
        return precision;
    }

    bool HasPhysicalKeyboard(const std::vector<InputDeviceInfo>& devices) {
        for (const InputDeviceInfo& d : devices) if (d.keyboardType == InputDeviceInfo::Alphabetic && !d.isVirtual) return true;
        return false;
    }

    // The scope goes with its owner (one of its services), after the owner's devices: the
    // listener went with them.
    OwnerMediaScope::~OwnerMediaScope() = default;

    void OwnerMediaScope::Listen() const {
        if (m_Listening) return;
        m_Listening = true;
        m_Owner.Devices().AcquireListener();
    }

    Dp OwnerMediaScope::WindowWidth() const {
        return Dp(float(m_Owner.ObservedWindowSize().width) / m_Owner.ObservedDensity().density);
    }
    Dp OwnerMediaScope::WindowHeight() const {
        return Dp(float(m_Owner.ObservedWindowSize().height) / m_Owner.ObservedDensity().density);
    }
    UiMediaScope::PointerPrecision OwnerMediaScope::GetPointerPrecision() const {
        Listen();
        return ResolvePointerPrecision(m_Owner.Devices().All());
    }
    UiMediaScope::KeyboardKind OwnerMediaScope::GetKeyboardKind() const {
        Listen();
        if (HasPhysicalKeyboard(m_Owner.Devices().All())) return KeyboardKind::Physical;
        return m_Owner.Insets().ImeVisible() ? KeyboardKind::Virtual : KeyboardKind::None;
    }
    UiMediaScope::ViewingDistance OwnerMediaScope::GetViewingDistance() const {
        const Features f = m_Features.Get();
        if (f.television) return ViewingDistance::Far;
        if (f.automotive || f.docked) return ViewingDistance::Medium;
        return ViewingDistance::Near;
    }

    const UiMediaScope& CurrentMediaScope(const ModifierNode& node) { return *CurrentValueOf(node, LocalUiMediaScope); }

}
