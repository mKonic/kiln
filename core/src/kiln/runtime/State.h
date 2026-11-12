#pragma once

// Observable state (spec 14 req. 1).
//
// kiln's UI lives on one thread (architecture 3.2), so this is not Compose's MVCC snapshot system:
// a write lands immediately and is recorded as a change; ApplyChanges() (frame step 3) hands the
// set of changed objects to every subscriber that read one of them. Reads are recorded per
// Subscriber, so composition, measure, layout, draw and semantics each invalidate only on what
// they actually read.

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kiln::rt {

    class StateObject;

    // Something that reads state and wants to hear when it changes: a recompose scope, a layout
    // node's measure, a draw layer.
    class Subscriber {
    public:
        virtual ~Subscriber();
        // Called from ApplyChanges for each subscriber that read at least one changed object.
        virtual void OnStateChanged() = 0;

        // Drops every subscription; the next tracked run records a fresh set.
        void Unsubscribe();

    protected:
        const std::unordered_set<StateObject*>& Reads() const { return m_Reads; }

    private:
        friend class Observation;
        std::unordered_set<StateObject*> m_Reads;
        int m_TrackDepth = 0;   // nested Track runs of this subscriber
    };

    // Common base of every observable value.
    class StateObject {
    public:
        StateObject() = default;
        StateObject(const StateObject&) = delete;
        StateObject& operator=(const StateObject&) = delete;
        virtual ~StateObject();

        std::uint64_t Version() const { return m_Version; }

    protected:
        void RecordRead() const;
        void RecordWrite();

    private:
        friend class Observation;
        friend class DerivedBase;
        std::uint64_t m_Version = 0;
    };

    // The per-thread bookkeeping: who is reading right now, who read what, what changed.
    class Observation {
    public:
        // Runs fn with reads attributed to subscriber (nested runs restore the outer reader).
        // The subscriber's previous reads are replaced.
        static void Track(Subscriber& subscriber, const std::function<void()>& fn);
        // Runs fn with no reader: reads inside are not recorded (event handlers, effects).
        static void Untracked(const std::function<void()>& fn);

        // Frame step 3: notifies each subscriber that read a changed object, once. Returns
        // whether anything changed. Writes made by notified subscribers are applied next call.
        static bool ApplyChanges();
        static bool HasPendingChanges();

        // Called whenever the first change after an apply is recorded, so the host can schedule
        // a frame (Compose's global snapshot apply observer, 14 §Recomposer).
        static void SetChangeListener(std::function<void()> listener);

    private:
        friend class StateObject;
        friend class Subscriber;
        friend class DerivedBase;
        static void OnRead(const StateObject* object);
        static void OnWrite(StateObject* object);
        static void Forget(StateObject* object);
        static void Forget(Subscriber* subscriber);
    };

    // Equality policies (SnapshotMutationPolicy).
    struct StructuralEquality {
        template<typename T> bool operator()(const T& a, const T& b) const { return a == b; }
    };
    struct NeverEqual {
        template<typename T> bool operator()(const T&, const T&) const { return false; }
    };

    template<typename T, typename Policy = StructuralEquality>
    class State final : public StateObject {
    public:
        State() = default;
        explicit State(T value) : m_Value(std::move(value)) {}

        const T& Get() const { RecordRead(); return m_Value; }
        const T& Peek() const { return m_Value; }   // read without subscribing
        void Set(T value) {
            if (Policy{}(m_Value, value)) return;
            m_Value = std::move(value);
            RecordWrite();
        }
        // Mutate in place; always counts as a change.
        template<typename Fn> void Update(Fn&& fn) { fn(m_Value); RecordWrite(); }

    private:
        T m_Value{};
    };

    // Base of derived (computed) values: tracks its own dependencies and is notified by them.
    class DerivedBase : public StateObject, private Subscriber {
    public:
        ~DerivedBase() override;

    protected:
        // Recomputes if a dependency changed since the last computation.
        void Refresh();
        virtual bool Recompute() = 0;   // returns whether the value changed (policy)

    private:
        friend class Observation;
        void OnStateChanged() override;
        bool Stale();
        void Compute();
        bool m_HasComputed = false;
        std::uint64_t m_AnnouncedVersion = 0;   // last version readers were notified of
        bool m_AnnouncedVersionSet = false;
        std::unordered_map<StateObject*, std::uint64_t> m_DepVersions;
    };

    template<typename T, typename Policy = StructuralEquality>
    class Derived final : public DerivedBase {
    public:
        explicit Derived(std::function<T()> compute) : m_Compute(std::move(compute)) {}
        const T& Get() { Refresh(); RecordRead(); return m_Value; }

    private:
        bool Recompute() override {
            T next = m_Compute();
            if (m_HasValue && Policy{}(m_Value, next)) return false;
            m_Value = std::move(next);
            m_HasValue = true;
            return true;
        }
        std::function<T()> m_Compute;
        T m_Value{};
        bool m_HasValue = false;
    };

    // Observable list (SnapshotStateList): any mutation is one change of the whole list.
    template<typename T>
    class StateList final : public StateObject {
    public:
        const std::vector<T>& Get() const { RecordRead(); return m_Items; }
        std::size_t Size() const { RecordRead(); return m_Items.size(); }
        const T& operator[](std::size_t i) const { RecordRead(); return m_Items[i]; }
        void Add(T item) { m_Items.push_back(std::move(item)); RecordWrite(); }
        void Insert(std::size_t at, T item) { m_Items.insert(m_Items.begin() + std::ptrdiff_t(at), std::move(item)); RecordWrite(); }
        void RemoveAt(std::size_t i) { m_Items.erase(m_Items.begin() + std::ptrdiff_t(i)); RecordWrite(); }
        void Clear() { if (!m_Items.empty()) { m_Items.clear(); RecordWrite(); } }
        template<typename Fn> void Update(Fn&& fn) { fn(m_Items); RecordWrite(); }

    private:
        std::vector<T> m_Items;
    };

}
