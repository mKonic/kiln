#include "kiln/material3/Scrim.h"

#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/ScrimTokens.h"

#include <algorithm>

namespace kiln::material3 {

    using namespace foundation;

    namespace ScrimDefaults {
        ui::Color color() { return Value(tokens::ScrimTokens::ContainerColor).WithAlpha(tokens::ScrimTokens::ContainerOpacity); }
    }

    namespace {
        rt::Task<> Taps(PointerInputScope& s, std::function<void()> onClick) {
            TapCallbacks tap;
            tap.onTap = [onClick](ui::Offset) { onClick(); };
            co_await DetectTapGestures(s, std::move(tap));
        }
    }

    void Scrim(std::optional<std::string> contentDescription, const ui::Modifier& modifier, const ScrimOptions& o) {
        const ui::Color color = o.color.value_or(ScrimDefaults::color());
        if (!color.Specified()) return;
        ui::Modifier m = FillMax(modifier, Axis::Both);
        if (o.onClick) {
            // The latest onClick, for the gesture handler that outlives this composition.
            auto& latest = rt::Composition::Current()->Remember<std::shared_ptr<std::function<void()>>>([] { return std::make_shared<std::function<void()>>(); });
            *latest = o.onClick;
            const auto onClick = [latest] { if (*latest) (*latest)(); };
            m = PointerInput(std::move(m), rt::Keys(), [onClick](PointerInputScope& s) { return Taps(s, onClick); });
            m = Semantics(std::move(m), [contentDescription, onClick](ui::SemanticsConfiguration& c) {
                c.traversalIndex = 1.0f;
                if (contentDescription) c.contentDescription = { *contentDescription };
                c.onClick = [onClick] { onClick(); return true; };
            }, true);
        }
        const auto alpha = o.alpha;
        Canvas(m, [color, alpha](ui::DrawScope& d) {
            const float a = std::clamp(alpha ? alpha() : 1.0f, 0.0f, 1.0f);
            d.DrawRect(color.WithAlpha(color.Alpha() * a), {}, d.GetSize());
        });
    }

}
