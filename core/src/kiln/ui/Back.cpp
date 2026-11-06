#include "kiln/ui/Back.h"

#include <algorithm>
#include <utility>

namespace kiln::ui {

    BackDispatcher& BackDispatcher::Main() {
        thread_local BackDispatcher d;
        return d;
    }

    void BackDispatcher::Add(BackCallback* cb) {
        m_Callbacks.push_back(cb);
        EnabledChanged();
    }

    void BackDispatcher::Remove(BackCallback* cb) {
        if (m_InProgress == cb) { m_InProgress = nullptr; cb->OnBackCancelled(); }
        std::erase(m_Callbacks, cb);
        EnabledChanged();
    }

    bool BackDispatcher::HasEnabledCallbacks() const {
        return std::any_of(m_Callbacks.begin(), m_Callbacks.end(), [](const BackCallback* c) { return c->enabled; });
    }

    void BackDispatcher::EnabledChanged() {
        const bool has = HasEnabledCallbacks();
        if (has == m_LastReported) return;
        m_LastReported = has;
        if (onHasEnabledChanged) onHasEnabledChanged(has);
    }

    BackCallback* BackDispatcher::Top() const {
        for (auto it = m_Callbacks.rbegin(); it != m_Callbacks.rend(); ++it)
            if ((*it)->enabled) return *it;
        return nullptr;
    }

    bool BackDispatcher::Started(const BackEvent& e) {
        if (m_InProgress) Cancelled();
        m_InProgress = Top();
        if (!m_InProgress) return false;
        m_InProgress->OnBackStarted(e);
        return true;
    }

    void BackDispatcher::Progressed(const BackEvent& e) {
        if (m_InProgress) m_InProgress->OnBackProgressed(e);
    }

    void BackDispatcher::Cancelled() {
        if (auto* cb = std::exchange(m_InProgress, nullptr)) cb->OnBackCancelled();
    }

    bool BackDispatcher::Completed() {
        BackCallback* cb = std::exchange(m_InProgress, nullptr);
        if (!cb) cb = Top();
        if (!cb) return false;
        cb->OnBack();
        return true;
    }

}
