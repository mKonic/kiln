#pragma once

// StyleState (foundation style/StyleState.kt): the state a style reads to pick what it provides —
// pressed, hovered, focused, selected, enabled, a tri-state toggle, and custom keys. Pressed,
// hovered and focused follow an interaction source while a resolver uses the state. Reads are
// observed, so a style that reads a state re-resolves when it changes.

#include "kiln/foundation/Interaction.h"
#include <kiln/Function.h>
#include "kiln/runtime/State.h"
#include "kiln/ui/ToggleableState.h"

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>

namespace kiln::foundation {

    class MutableStyleState;
    class StyleState;

    // The untyped side of a StyleStateKey (its identity is its address).
    class StyleStateKeyBase {
    public:
        virtual ~StyleStateKeyBase() = default;
        // processInteraction: a custom key sees each interaction that is not a press, hover or
        // focus while the state follows a source, and may update the state.
        virtual void ProcessInteraction(const Interaction&, MutableStyleState&) const {}
        virtual bool IsPredefined() const { return false; }
    };

    // StyleStateKey<T>: a custom state with its default value. Define each key once and keep it.
    template<typename T>
    class StyleStateKey : public StyleStateKeyBase {
    public:
        explicit StyleStateKey(T defaultValue) : m_Default(std::move(defaultValue)) {}
        const T& DefaultValue() const { return m_Default; }
        // Predefined keys read and write the state's own flags.
        virtual T GetValueFrom(const MutableStyleState& state) const;
        virtual void SetValueTo(T value, MutableStyleState& state) const;
    private:
        T m_Default;
    };

    // StyleStateKey.Pressed / Hovered / Focused / Selected / Enabled / Toggle.
    struct StyleStateKeys {
        static const StyleStateKey<bool>& Pressed();
        static const StyleStateKey<bool>& Hovered();
        static const StyleStateKey<bool>& Focused();
        static const StyleStateKey<bool>& Selected();
        static const StyleStateKey<bool>& Enabled();   // true by default
        static const StyleStateKey<ui::ToggleableState>& Toggle();
    };

    class StyleState {
    public:
        virtual ~StyleState() = default;
        virtual bool IsEnabled() const = 0;
        virtual bool IsFocused() const = 0;
        virtual bool IsHovered() const = 0;
        virtual bool IsPressed() const = 0;
        virtual bool IsSelected() const = 0;
        virtual bool IsChecked() const = 0;
        virtual ui::ToggleableState TriStateToggle() const = 0;
        template<typename T> T Get(const StyleStateKey<T>& key) const;
        // The source pressed / hovered / focused follow (null: set them yourself).
        virtual const std::shared_ptr<MutableInteractionSource>& InteractionSource() const = 0;
        // processInteractions: follow `source` until the returned function is called.
        virtual kiln::Function<void()> ProcessInteractions(const std::shared_ptr<MutableInteractionSource>& source) = 0;
    };

    class MutableStyleState final : public StyleState {
    public:
        explicit MutableStyleState(std::shared_ptr<MutableInteractionSource> interactionSource = nullptr)
            : m_Source(std::move(interactionSource)) {}

        bool IsEnabled() const override { return Flag(kEnabled); }
        bool IsFocused() const override { return Flag(kFocused); }
        bool IsHovered() const override { return Flag(kHovered); }
        bool IsPressed() const override { return Flag(kPressed); }
        bool IsSelected() const override { return Flag(kSelected); }
        bool IsChecked() const override { return TriStateToggle() == ui::ToggleableState::On; }
        ui::ToggleableState TriStateToggle() const override;
        void SetEnabled(bool v) { UpdateFlags(kEnabled, v ? kEnabled : 0); }
        void SetFocused(bool v) { UpdateFlags(kFocused, v ? kFocused : 0); }
        void SetHovered(bool v) { UpdateFlags(kHovered, v ? kHovered : 0); }
        void SetPressed(bool v) { UpdateFlags(kPressed, v ? kPressed : 0); }
        void SetSelected(bool v) { UpdateFlags(kSelected, v ? kSelected : 0); }
        void SetChecked(bool v) { SetTriStateToggle(ui::ToggleableStateOf(v)); }
        void SetTriStateToggle(ui::ToggleableState v);

        template<typename T> void Set(const StyleStateKey<T>& key, T value) { key.SetValueTo(std::move(value), *this); }
        // remove: a custom key back to its default (predefined keys cannot be removed).
        void Remove(const StyleStateKeyBase& key);

        const std::shared_ptr<MutableInteractionSource>& InteractionSource() const override { return m_Source; }
        kiln::Function<void()> ProcessInteractions(const std::shared_ptr<MutableInteractionSource>& source) override;

        // The flags (bit 0 pressed, 1 hovered, 2 focused, 3 selected, 4 enabled, 5-6 toggle).
        int Flags() const { return m_Flags.Get(); }
        bool Flag(int mask) const { return (Flags() & mask) != 0; }
        void UpdateFlags(int mask, int values);
        template<typename T> T GetCustomValue(const StyleStateKey<T>& key) const {
            const auto& map = m_Custom.Get();
            auto it = map.find(&key);
            return it == map.end() ? key.DefaultValue() : std::any_cast<T>(it->second);
        }
        template<typename T> void SetCustomValue(const StyleStateKey<T>& key, T value) {
            m_Custom.Update([&](auto& map) { map[&key] = std::any(std::move(value)); });
        }

        static constexpr int kPressed = 1 << 0, kHovered = 1 << 1, kFocused = 1 << 2, kSelected = 1 << 3, kEnabled = 1 << 4;
        static constexpr int kToggleShift = 5, kToggleOn = 1 << kToggleShift, kToggleIndeterminate = 2 << kToggleShift,
                             kToggleMask = 3 << kToggleShift;

    private:
        std::shared_ptr<MutableInteractionSource> m_Source;
        rt::State<int> m_Flags{ kEnabled };
        rt::State<std::map<const StyleStateKeyBase*, std::any>, rt::NeverEqual> m_Custom;
    };

    template<typename T> T StyleStateKey<T>::GetValueFrom(const MutableStyleState& state) const { return state.GetCustomValue(*this); }
    template<typename T> void StyleStateKey<T>::SetValueTo(T value, MutableStyleState& state) const { state.SetCustomValue(*this, std::move(value)); }
    template<typename T> T StyleState::Get(const StyleStateKey<T>& key) const {
        return key.GetValueFrom(static_cast<const MutableStyleState&>(*this));   // StyleState is sealed
    }

    // rememberUpdatedStyleState(interactionSource, block): a state remembered per source; `block`
    // updates it on each composition.
    std::shared_ptr<MutableStyleState> RememberUpdatedStyleState(std::shared_ptr<MutableInteractionSource> interactionSource,
                                                                 const kiln::Function<void(MutableStyleState&)>& block = {});

}
