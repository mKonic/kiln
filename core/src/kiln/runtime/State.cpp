#include "kiln/runtime/State.h"
#include <kiln/Function.h>

namespace kiln::rt {

    namespace {
        struct Registry {
            Subscriber* reader = nullptr;
            bool untracked = false;
            std::unordered_map<StateObject*, std::vector<Subscriber*>> readers;
            std::unordered_set<StateObject*> changed;
            kiln::Function<void()> listener;
            std::unordered_set<Subscriber*>* notifying = nullptr;
        };
        Registry& R() {
            thread_local Registry registry;
            return registry;
        }
        void Erase(std::vector<Subscriber*>& v, Subscriber* s) {
            for (std::size_t i = 0; i < v.size(); ++i)
                if (v[i] == s) { v[i] = v.back(); v.pop_back(); return; }
        }
    }

    Subscriber::~Subscriber() { Observation::Forget(this); }
    void Subscriber::Unsubscribe() { Observation::Forget(this); }

    StateObject::~StateObject() { Observation::Forget(this); }
    void StateObject::RecordRead() const { Observation::OnRead(this); }
    void StateObject::RecordWrite() { ++m_Version; Observation::OnWrite(this); }

    void Observation::OnRead(const StateObject* object) {
        Registry& r = R();
        if (!r.reader || r.untracked) return;
        auto* obj = const_cast<StateObject*>(object);
        if (r.reader->m_Reads.insert(obj).second) r.readers[obj].push_back(r.reader);
    }

    void Observation::OnWrite(StateObject* object) {
        Registry& r = R();
        const bool first = r.changed.empty();
        r.changed.insert(object);
        if (first && r.listener) r.listener();
    }

    void Observation::Forget(StateObject* object) {
        Registry& r = R();
        if (auto it = r.readers.find(object); it != r.readers.end()) {
            for (Subscriber* s : it->second) s->m_Reads.erase(object);
            r.readers.erase(it);
        }
        r.changed.erase(object);
    }

    void Observation::Forget(Subscriber* subscriber) {
        Registry& r = R();
        for (StateObject* obj : subscriber->m_Reads)
            if (auto it = r.readers.find(obj); it != r.readers.end()) Erase(it->second, subscriber);
        subscriber->m_Reads.clear();
        if (r.reader == subscriber) r.reader = nullptr;
        if (r.notifying) r.notifying->erase(subscriber);
    }

    void Observation::Track(Subscriber& subscriber, const kiln::Function<void()>& fn) {
        Registry& r = R();
        // Only the outermost run starts the reads over: a run nested in one of the same
        // subscriber (directly or through another) adds to them (SnapshotStateObserver).
        if (subscriber.m_TrackDepth == 0) Forget(&subscriber);
        Subscriber* outer = r.reader;
        const bool outerUntracked = r.untracked;
        r.reader = &subscriber;
        r.untracked = false;
        ++subscriber.m_TrackDepth;
        struct Restore { Registry& r; Subscriber* o; bool u; Subscriber& s; ~Restore() { r.reader = o; r.untracked = u; --s.m_TrackDepth; } } restore{ r, outer, outerUntracked, subscriber };
        fn();
    }

    void Observation::Untracked(const kiln::Function<void()>& fn) {
        Registry& r = R();
        const bool outer = r.untracked;
        r.untracked = true;
        struct Restore { Registry& r; bool u; ~Restore() { r.untracked = u; } } restore{ r, outer };
        fn();
    }

    bool Observation::HasPendingChanges() { return !R().changed.empty(); }
    void Observation::SetChangeListener(kiln::Function<void()> listener) { R().listener = std::move(listener); }

    bool Observation::ApplyChanges() {
        Registry& r = R();
        if (r.changed.empty()) return false;
        std::unordered_set<StateObject*> changed;
        changed.swap(r.changed);
        // Collect first: notifying can re-track subscribers and mutate the reader lists.
        std::vector<Subscriber*> notify;
        std::unordered_set<Subscriber*> seen;
        for (StateObject* obj : changed)
            if (auto it = r.readers.find(obj); it != r.readers.end())
                for (Subscriber* s : it->second)
                    if (seen.insert(s).second) notify.push_back(s);
        // A subscriber disposed by an earlier notification (a scope removed by its parent's
        // recomposition) is dropped from `seen` by Forget and skipped here.
        r.notifying = &seen;
        for (Subscriber* s : notify)
            if (seen.count(s)) s->OnStateChanged();
        r.notifying = nullptr;
        return true;
    }

    DerivedBase::~DerivedBase() = default;

    bool DerivedBase::Stale() {
        if (!m_HasComputed) return true;
        // Reads() only holds live objects (Forget removes destroyed ones). Refresh derived
        // dependencies first so their versions are current.
        for (StateObject* dep : Reads()) {
            if (auto* d = dynamic_cast<DerivedBase*>(dep)) d->Refresh();
            auto it = m_DepVersions.find(dep);
            if (it == m_DepVersions.end() || it->second != dep->Version()) return true;
        }
        return false;
    }

    void DerivedBase::Compute() {
        bool changed = false;
        Observation::Track(*this, [&] { changed = Recompute(); });
        m_HasComputed = true;
        m_DepVersions.clear();
        for (StateObject* dep : Reads()) m_DepVersions[dep] = dep->Version();
        if (changed) ++m_Version;
        if (!m_AnnouncedVersionSet) { m_AnnouncedVersion = m_Version; m_AnnouncedVersionSet = true; }
    }

    void DerivedBase::Refresh() {
        if (Stale()) Compute();
    }

    void DerivedBase::OnStateChanged() {
        // A dependency changed: if the value differs, readers of the derived value are notified
        // on the next apply pass (the Recomposer applies until nothing is pending).
        // Compare against what readers last heard, not against the pre-call version: a Get()
        // between the write and this apply may already have recomputed.
        Refresh();
        if (m_Version != m_AnnouncedVersion) {
            m_AnnouncedVersion = m_Version;
            Observation::OnWrite(this);
        }
    }

}
