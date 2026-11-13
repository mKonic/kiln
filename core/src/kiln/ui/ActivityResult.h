#pragma once

// Activity results (androidx.activity.result): the Intent a contract asks the host to start, the
// result it gets back, and the native half of the ActivityResultRegistry. The host keeps the
// request codes, the launched keys and results that arrive with no callback (they survive the
// activity being recreated); native keeps the callbacks and results a contract gave without
// starting anything (getSynchronousResult).

#include "kiln/ui/Credentials.h"
#include <kiln/Function.h>

#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::ui {

    // An Intent as data: action, data URI, MIME type, categories, explicit component, flags,
    // extras, and the URIs of its ClipData (results).
    struct Intent {
        std::string action;
        std::optional<std::string> data, type;
        std::vector<std::string> categories;
        std::string packageName, className;
        int flags = 0;
        Bundle extras;
        std::vector<std::string> clipUris;
    };

    inline constexpr int kResultOk = -1, kResultCanceled = 0;

    struct ActivityResult {
        int resultCode = kResultCanceled;
        std::optional<Intent> data;
    };

    // ComponentActivity's registry on the host: keys bound to request codes, launching, the
    // permission check contracts use for their synchronous results.
    struct ActivityResultHost {
        // registerKey; the host then delivers a result kept for the key (DispatchActivityResult).
        kiln::Function<void(const std::string& key)> registerKey;
        kiln::Function<void(const std::string& key)> unregister;
        // False when no activity handles the intent.
        kiln::Function<bool(const std::string& key, const Intent& intent)> launch;
        kiln::Function<bool(const std::string& permission)> checkSelfPermission;
    };

    class ActivityResultRegistry {
    public:
        void SetHost(ActivityResultHost host) { m_Host = std::move(host); }
        const ActivityResultHost& Host() const { return m_Host; }

        // register(key, contract, callback): `deliver` gets each raw result for the key.
        void Register(const std::string& key, kiln::Function<void(const ActivityResult&)> deliver) {
            m_Callbacks[key] = std::move(deliver);
            if (m_Host.registerKey) m_Host.registerKey(key);
            if (auto it = m_ParsedPending.find(key); it != m_ParsedPending.end()) {
                auto deliverParsed = std::move(it->second);
                m_ParsedPending.erase(it);
                deliverParsed();
            }
        }
        void Unregister(const std::string& key) {
            m_Callbacks.erase(key);
            m_ParsedPending.erase(key);
            if (m_Host.unregister) m_Host.unregister(key);
        }
        // Throws ActivityNotFound when nothing handles the intent (ActivityNotFoundException).
        struct ActivityNotFound : std::runtime_error { using std::runtime_error::runtime_error; };
        void Launch(const std::string& key, const Intent& intent) {
            if (m_Host.launch && !m_Host.launch(key, intent)) throw ActivityNotFound("No activity found to handle " + intent.action);
        }
        // A raw result from the host (doDispatch with a callback present).
        void DispatchActivityResult(const std::string& key, const ActivityResult& result) {
            if (auto it = m_Callbacks.find(key); it != m_Callbacks.end()) {
                const auto deliver = it->second;
                deliver(result);
            }
        }
        // dispatchResult(requestCode, result) for a synchronous result: to the callback now, or
        // kept until one registers. `deliver` calls the parsed callback.
        void DispatchParsed(const std::string& key, kiln::Function<void()> deliver, bool hasCallback) {
            if (hasCallback) deliver();
            else m_ParsedPending[key] = std::move(deliver);
        }
        bool HasCallback(const std::string& key) const { return m_Callbacks.count(key) != 0; }

    private:
        ActivityResultHost m_Host;
        std::map<std::string, kiln::Function<void(const ActivityResult&)>> m_Callbacks;
        std::map<std::string, kiln::Function<void()>> m_ParsedPending;
    };

}
