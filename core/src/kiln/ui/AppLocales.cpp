#include "kiln/ui/AppLocales.h"

#include <map>
#include <memory>
#include <mutex>

namespace kiln::ui {

    namespace {
        struct State {
            std::mutex mutex;
            AppLocalesHost host;
            std::vector<std::string> requested;   // without a host
            std::map<int, std::shared_ptr<kiln::Function<void()>>> listeners;
            int next = 1;
        };
        State& S() {
            static State* s = new State;
            return *s;
        }
    }

    void SetAppLocalesHost(AppLocalesHost host) {
        std::lock_guard lock(S().mutex);
        S().host = std::move(host);
    }

    void SetApplicationLocales(std::vector<std::string> tags) {
        std::vector<std::shared_ptr<kiln::Function<void()>>> notify;
        {
            std::unique_lock lock(S().mutex);
            if (S().host.set) {
                auto set = S().host.set;
                lock.unlock();
                set(tags);
                return;
            }
            if (S().requested == tags) return;
            S().requested = std::move(tags);
            for (auto& [id, l] : S().listeners) notify.push_back(l);
        }
        for (auto& l : notify) (*l)();
    }

    std::vector<std::string> GetApplicationLocales() {
        std::unique_lock lock(S().mutex);
        if (S().host.get) {
            auto get = S().host.get;
            lock.unlock();
            return get();
        }
        return S().requested;
    }

    std::vector<std::string> ApplyApplicationLocales(std::vector<std::string> system) {
        std::lock_guard lock(S().mutex);
        if (S().host.set || S().requested.empty()) return system;
        return S().requested;
    }

    int AddApplicationLocalesListener(kiln::Function<void()> listener) {
        std::lock_guard lock(S().mutex);
        const int id = S().next++;
        S().listeners[id] = std::make_shared<kiln::Function<void()>>(std::move(listener));
        return id;
    }
    void RemoveApplicationLocalesListener(int id) {
        std::lock_guard lock(S().mutex);
        S().listeners.erase(id);
    }

}
