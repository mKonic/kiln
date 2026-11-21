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
        ActivityHost& Host() {
            static auto* h = new ActivityHost;
            return *h;
        }
    }

    bool BundleIntent::operator==(const BundleIntent& o) const {
        if (!intent || !o.intent) return intent == o.intent;
        return *intent == *o.intent;
    }

    bool StartActivity(const Intent& intent) { return StartActivity(intent, {}); }
    bool StartActivity(const Intent& intent, const ActivityOptions& options) { return Host().startActivity && Host().startActivity(intent, options); }
    bool ShouldShowRequestPermissionRationale(const std::string& permission) {
        return Host().shouldShowRequestPermissionRationale && Host().shouldShowRequestPermissionRationale(permission);
    }
    void SetActivityHost(ActivityHost host) { Host() = std::move(host); }

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
