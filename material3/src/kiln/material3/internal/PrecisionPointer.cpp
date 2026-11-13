#include "kiln/material3/internal/PrecisionPointer.h"
#include <kiln/Function.h>

#include "kiln/material3/MaterialTheme.h"
#include "kiln/ui/Locals.h"

namespace kiln::material3::internal {

    namespace {
        rt::Local<bool> LocalListenerRegistered{ false };
        rt::Local<bool> LocalPrecisionPointerSizing{ false };
    }

    bool ShouldUsePrecisionPointerComponentSizing() {
        return rt::Composition::Current()->Read(LocalPrecisionPointerSizing);
    }

    void EnsurePrecisionPointerListenersRegistered(const kiln::Function<void()>& content) {
        auto& c = *rt::Composition::Current();
        if (!ComposeMaterial3Flags::isPrecisionPointerComponentSizingEnabled || c.Read(LocalListenerRegistered)) {
            content();
            return;
        }
        ui::Owner* owner = c.Read(ui::LocalOwner);
        c.DisposableEffect(rt::Keys(std::hash<const void*>{}(owner)), [owner]() -> kiln::Function<void()> {
            if (!owner) return {};
            owner->Devices().AcquireListener();
            return [owner] { owner->Devices().ReleaseListener(); };
        });
        const bool usePrecisionPointer = owner && owner->Devices().HasKeyboardAndMouse();
        c.Provide(LocalListenerRegistered, true, [&c, usePrecisionPointer, content] {
            c.Provide(LocalPrecisionPointerSizing, usePrecisionPointer, content);
        });
    }

}
