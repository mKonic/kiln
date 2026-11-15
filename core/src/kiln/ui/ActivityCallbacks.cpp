#include "kiln/ui/ActivityCallbacks.h"

#include "kiln/runtime/State.h"

#include <map>
#include <memory>
#include <vector>

namespace kiln::ui {

    namespace {
        std::map<int, std::shared_ptr<kiln::Function<void(int)>>>& TrimListeners() {
            static auto* m = new std::map<int, std::shared_ptr<kiln::Function<void(int)>>>;
            return *m;
        }
        int g_NextTrim = 1;
        rt::State<bool>& MultiWindow() {
            static auto* s = new rt::State<bool>(false);
            return *s;
        }
    }

    int AddOnTrimMemoryListener(kiln::Function<void(int)> listener) {
        const int id = g_NextTrim++;
        TrimListeners()[id] = std::make_shared<kiln::Function<void(int)>>(std::move(listener));
        return id;
    }
    void RemoveOnTrimMemoryListener(int id) { TrimListeners().erase(id); }

    void DispatchTrimMemory(int level) {
        // A listener may remove itself (or others) while called.
        std::vector<std::shared_ptr<kiln::Function<void(int)>>> listeners;
        for (auto& [id, l] : TrimListeners()) listeners.push_back(l);
        for (auto& l : listeners) (*l)(level);
    }

    bool IsInMultiWindowMode() { return MultiWindow().Get(); }
    void SetInMultiWindowMode(bool on) {
        if (MultiWindow().Peek() != on) MultiWindow().Set(on);
    }

}
