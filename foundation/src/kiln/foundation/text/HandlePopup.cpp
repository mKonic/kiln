#include "kiln/foundation/text/HandlePopup.h"
#include <kiln/Function.h>

#include "kiln/foundation/Popup.h"

#include <cmath>
#include <memory>

namespace kiln::foundation {

    namespace {
        struct HandlePositionProvider final : PopupPositionProvider {
            float referenceX = 0;
            kiln::Function<Offset()> positionProvider;
            mutable Offset prevPosition{ 0, 0 };
            IntOffset CalculatePosition(ui::IntRect anchor, ui::IntSize, LayoutDirection, ui::IntSize content) const override {
                Offset position = positionProvider ? positionProvider() : Offset{ NAN, NAN };
                if (std::isnan(position.x) || std::isnan(position.y)) position = prevPosition;
                prevPosition = position;
                // handleReferencePoint.align(popupContentSize, IntSize.Zero): minus the reference
                // point's distance from the popup's left.
                const int adjustment = ui::RoundJava(-float(content.width) / 2.0f * (referenceX * 2.0f));
                return { anchor.left + ui::RoundJava(position.x) + adjustment, anchor.top + ui::RoundJava(position.y) };
            }
        };
    }

    void HandlePopup(kiln::Function<Offset()> positionProvider, float referenceX, kiln::Function<void()> content, Key key) {
        rt::Composition::Current()->Child([positionProvider, referenceX, content] {
            auto& c = *rt::Composition::Current();
            auto& provider = c.Remember<std::shared_ptr<HandlePositionProvider>>(
                [&] { auto p = std::make_shared<HandlePositionProvider>(); p->referenceX = referenceX; return p; },
                rt::Keys(referenceX));
            provider->positionProvider = positionProvider;
            PopupProperties properties;
            properties.excludeFromSystemGesture = true;
            properties.clippingEnabled = false;
            Popup(provider, {}, properties, content);
        }, key);
    }

}
