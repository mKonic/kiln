#include "kiln/ui/NightMode.h"

#include "kiln/ui/Configuration.h"

#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace kiln::ui {

    namespace {
        struct State {
            std::mutex mutex;
            NightModeHost host;
            NightMode mode = NightMode::FollowSystem;
            std::map<int, std::shared_ptr<kiln::Function<void()>>> listeners;
            int next = 1;
        };
        State& S() {
            static State* s = new State;
            return *s;
        }
        int LocalHour() {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_r(&now, &local);
            return local.tm_hour;
        }
    }

    bool IsNightByHour(int hour) { return hour < 6 || hour >= 22; }

    void SetNightModeHost(NightModeHost host) {
        std::lock_guard lock(S().mutex);
        S().host = std::move(host);
    }

    void SetDefaultNightMode(NightMode mode) {
        switch (mode) {
            case NightMode::FollowSystem: case NightMode::AutoTime: case NightMode::No:
            case NightMode::Yes: case NightMode::AutoBattery: break;
            default: return;   // an unknown mode is ignored, as AppCompat logs and ignores it
        }
        std::vector<std::shared_ptr<kiln::Function<void()>>> notify;
        kiln::Function<void(NightMode)> set;
        {
            std::lock_guard lock(S().mutex);
            if (S().mode == mode) return;
            S().mode = mode;
            if (S().host.set) set = S().host.set;
            else for (auto& [id, l] : S().listeners) notify.push_back(l);
        }
        if (set) set(mode);
        for (auto& l : notify) (*l)();
    }

    NightMode GetDefaultNightMode() {
        std::lock_guard lock(S().mutex);
        return S().mode;
    }

    int ApplyNightMode(int systemUiMode) {
        NightMode mode;
        {
            std::lock_guard lock(S().mutex);
            if (S().host.set) return systemUiMode;
            mode = S().mode;
        }
        bool night;
        switch (mode) {
            case NightMode::Yes: night = true; break;
            case NightMode::No: case NightMode::AutoBattery: night = false; break;
            case NightMode::AutoTime: night = IsNightByHour(LocalHour()); break;
            default: return systemUiMode;
        }
        return (systemUiMode & ~Configuration::kUiModeNightMask) | (night ? Configuration::kUiModeNightYes : Configuration::kUiModeNightNo);
    }

    int AddNightModeListener(kiln::Function<void()> listener) {
        std::lock_guard lock(S().mutex);
        const int id = S().next++;
        S().listeners[id] = std::make_shared<kiln::Function<void()>>(std::move(listener));
        return id;
    }

    void RemoveNightModeListener(int id) {
        std::lock_guard lock(S().mutex);
        S().listeners.erase(id);
    }

}
