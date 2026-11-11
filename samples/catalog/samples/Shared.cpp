#include "Shared.h"
#include "Common.h"

#include <kiln/material3/Tooltip.h>

namespace catalog::samples {
    void PlainTooltipAbove(const std::string& text, std::function<void()> anchor, bool shown) {
        m3::TooltipBox(m3::TooltipDefaults::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Above),
                       [text, shown](const m3::TooltipScope& scope) {
                           if (!shown) return;
                           m3::PlainTooltip(scope, Semantics({}, [text](SemanticsConfiguration& c) {
                                                c.liveRegion = LiveRegionMode::Assertive;
                                                c.paneTitle = text;
                                            }),
                                            {}, [text] { m3::Text(text); });
                       },
                       m3::RememberTooltipState(), {}, {}, std::move(anchor));
    }
}
