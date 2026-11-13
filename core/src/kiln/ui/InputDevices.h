#pragma once

// PrecisionPointer.android.kt's Devices cache, held by a window's owner. Source bits and
// keyboard types are InputDevice's values; no Android classes are needed in the core.

#include "kiln/runtime/State.h"
#include <kiln/Function.h>

#include <cstdint>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace kiln::ui {

    struct InputDeviceInfo {
        int id = 0;
        bool isVirtual = false;
        std::uint32_t sources = 0;
        int keyboardType = 0;
        static constexpr std::uint32_t SourceKeyboard = 0x101, SourceMouse = 0x2002, SourceStylus = 0x4002;
        static constexpr int Alphabetic = 2;
        bool HasSource(std::uint32_t source) const { return (sources & source) == source; }
        bool IsKeyboard() const { return !isVirtual && HasSource(SourceKeyboard) && keyboardType == Alphabetic; }
        // A source that is both mouse and stylus is an external drawing tablet, not a mouse.
        bool IsMouse() const { return !isVirtual && HasSource(SourceMouse) && !HasSource(SourceStylus); }
    };

    class InputDevices {
    public:
        void SetListenerHost(kiln::Function<void(bool)> host) {
            if (m_Listeners && m_Host) m_Host(false);
            m_Host = std::move(host);
            if (m_Listeners && m_Host) m_Host(true);
        }
        void AcquireListener() {
            if (++m_Listeners == 1 && m_Host) m_Host(true);
        }
        void ReleaseListener() {
            if (m_Listeners == 0 || --m_Listeners != 0) return;
            if (m_Host) m_Host(false);
            Clear();
        }
        int ListenerCount() const { return m_Listeners; }
        bool HasKeyboardAndMouse() const { return m_HasBoth.Get(); }
        void Update(const InputDeviceInfo& device) {
            if (!m_Listeners) return;
            // Every device as last reported (an empty source set: removed).
            m_All.Update([&](std::vector<InputDeviceInfo>& all) {
                std::erase_if(all, [&](const InputDeviceInfo& d) { return d.id == device.id; });
                if (device.sources != 0 || device.keyboardType != 0) all.push_back(device);
            });
            UpdatePresence(m_Keyboards, device.id, device.IsKeyboard());
            UpdatePresence(m_Mice, device.id, device.IsMouse());
            m_HasBoth.Set(!m_Keyboards.empty() && !m_Mice.empty());
        }
        void Remove(int id) { Update({ id }); }
        // The devices attached (observable), while someone listens.
        const std::vector<InputDeviceInfo>& All() const { return m_All.Get(); }
        void Clear() {
            if (!m_All.Peek().empty()) m_All.Set({});
            m_Keyboards.clear();
            m_Mice.clear();
            m_HasBoth.Set(false);
        }
    private:
        static void UpdatePresence(std::set<int>& devices, int id, bool present) {
            if (present) devices.insert(id);
            else devices.erase(id);
        }
        int m_Listeners = 0;
        kiln::Function<void(bool)> m_Host;
        std::set<int> m_Keyboards, m_Mice;
        rt::State<bool> m_HasBoth{ false };
        rt::State<std::vector<InputDeviceInfo>, rt::NeverEqual> m_All{ {} };
    };

}
