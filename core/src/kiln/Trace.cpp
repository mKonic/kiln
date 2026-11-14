#include "kiln/Trace.h"

namespace kiln::trace {

    namespace {
        Tracer g_Tracer;
    }

    // Installed once by the host before frames run; read on any thread afterwards.
    void SetTracer(const Tracer& tracer) { g_Tracer = tracer; }
    const Tracer& CurrentTracer() { return g_Tracer; }

}
