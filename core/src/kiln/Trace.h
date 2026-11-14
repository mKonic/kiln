#pragma once

// Trace sections for system tracing (Perfetto, systrace): androidx.tracing's trace(name) { }.
// On Android they go to ATrace; elsewhere nowhere unless a tracer is installed. A section costs
// one check when no trace is being recorded. kiln marks its frame phases with Compose's names
// (Recomposer:recompose, AndroidOwner:measureAndLayout, ...); apps add their own:
//
//   { kiln::trace::Section s("Notes:load"); LoadNotes(); }

#include <cstdint>

namespace kiln::trace {

    // What the host provides (ATrace_beginSection / endSection / isEnabled / setCounter and
    // the async calls). Null members do nothing.
    struct Tracer {
        bool (*enabled)() = nullptr;
        void (*begin)(const char* name) = nullptr;
        void (*end)() = nullptr;
        void (*counter)(const char* name, std::int64_t value) = nullptr;
        void (*asyncBegin)(const char* name, std::int32_t cookie) = nullptr;
        void (*asyncEnd)(const char* name, std::int32_t cookie) = nullptr;
    };
    void SetTracer(const Tracer& tracer);
    const Tracer& CurrentTracer();

    // Whether a trace is being recorded now.
    inline bool Enabled() {
        const Tracer& t = CurrentTracer();
        return t.begin && (!t.enabled || t.enabled());
    }
    inline void Begin(const char* name) { if (Enabled()) CurrentTracer().begin(name); }
    inline void End() { if (Enabled()) CurrentTracer().end(); }

    // A section for the scope it lives in. Sections nest; they end on the thread they began on.
    class Section {
    public:
        explicit Section(const char* name) : m_On(Enabled()) { if (m_On) CurrentTracer().begin(name); }
        ~Section() { if (m_On && CurrentTracer().end) CurrentTracer().end(); }
        Section(const Section&) = delete;
        Section& operator=(const Section&) = delete;
    private:
        bool m_On;
    };

    // A value plotted over time (ATrace_setCounter).
    inline void Counter(const char* name, std::int64_t value) {
        if (Enabled() && CurrentTracer().counter) CurrentTracer().counter(name, value);
    }
    // A section that may begin and end on different threads (ATrace_beginAsyncSection);
    // the cookie tells overlapping ones of the same name apart.
    inline void AsyncBegin(const char* name, std::int32_t cookie) {
        if (Enabled() && CurrentTracer().asyncBegin) CurrentTracer().asyncBegin(name, cookie);
    }
    inline void AsyncEnd(const char* name, std::int32_t cookie) {
        if (Enabled() && CurrentTracer().asyncEnd) CurrentTracer().asyncEnd(name, cookie);
    }

}
