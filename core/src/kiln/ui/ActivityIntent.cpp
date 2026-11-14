#include "kiln/ui/ActivityIntent.h"

#include <utility>
#include <vector>

namespace kiln::ui {

    namespace {
        struct Intents {
            std::optional<Intent> launch;
            std::vector<std::pair<int, kiln::Function<void(const Intent&)>>> listeners;
            int nextId = 1;
        };
        Intents& I() {
            static Intents* i = new Intents;
            return *i;
        }
    }

    std::optional<Intent> ActivityIntent() { return I().launch; }
    void SetActivityIntent(std::optional<Intent> intent) { I().launch = std::move(intent); }

    int AddOnNewIntentListener(kiln::Function<void(const Intent&)> listener) {
        const int id = I().nextId++;
        I().listeners.emplace_back(id, std::move(listener));
        return id;
    }
    void RemoveOnNewIntentListener(int id) {
        auto& l = I().listeners;
        for (auto it = l.begin(); it != l.end(); ++it)
            if (it->first == id) { l.erase(it); return; }
    }
    void DispatchNewIntent(const Intent& intent) {
        // A copy: listeners may add or remove listeners.
        const auto listeners = I().listeners;
        for (const auto& [id, fn] : listeners)
            if (fn) fn(intent);
    }

}
