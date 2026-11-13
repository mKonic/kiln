#include "Shared.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/material3/Tooltip.h>

namespace catalog::samples {
    void PlainTooltipAbove(const std::string& text, kiln::Function<void()> anchor, bool shown) {
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

    const char* LoremIpsum() {
        return "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer sodales laoreet commodo. Phasellus a purus eu "
            "risus elementum consequat. Aenean eu elit ut nunc convallis laoreet non ut libero. Suspendisse interdum placera"
            "t risus vel ornare. Donec vehicula, turpis sed consectetur ullamcorper, ante nunc egestas quam, ultricies adipi"
            "scing velit enim at nunc. Aenean id diam neque. Praesent ut lacus sed justo viverra fermentum et ut sem. Fusce "
            "convallis gravida lacinia. Integer semper dolor ut elit sagittis lacinia. Praesent sodales scelerisque eros at "
            "rhoncus. Duis posuere sapien vel ipsum ornare interdum at eu quam. Vestibulum vel massa erat. Aenean quis sagit"
            "tis purus. Phasellus arcu purus, rutrum id consectetur non, bibendum at nibh. \n\nDuis nec erat dolor. Nulla vi"
            "tae consectetur ligula. Quisque nec mi est. Ut quam ante, rutrum at pellentesque gravida, pretium in dui. Cras "
            "eget sapien velit. Suspendisse ut sem nec tellus vehicula eleifend sit amet quis velit. Phasellus quis suscipit"
            " nisi. Nam elementum malesuada tincidunt. Curabitur iaculis pretium eros, malesuada faucibus leo eleifend a. Cu"
            "rabitur congue orci in neque euismod a blandit libero vehicula. Lorem ipsum dolor sit amet, consectetur adipisc"
            "ing elit. Integer sodales laoreet commodo. Phasellus a purus eu risus elementum consequat. Aenean eu elit ut nu"
            "nc convallis laoreet non ut libero. Suspendisse interdum placerat risus vel ornare. Donec vehicula, turpis sed "
            "consectetur ullamcorper, ante nunc egestas quam, ultricies adipiscing velit enim at nunc. Aenean id diam neque."
            " Praesent ut lacus sed justo viverra fermentum et ut sem. Fusce convallis gravida lacinia. Integer semper dolor"
            " ut elit sagittis lacinia. Praesent sodales scelerisque eros at rhoncus. Duis posuere sapien vel ipsum ornare i"
            "nterdum at eu quam. Vestibulum vel massa erat. Aenean quis sagittis purus. Phasellus arcu purus, rutrum id cons"
            "ectetur non, bibendum at nibh. \n\nDuis nec erat dolor. Nulla vitae consectetur ligula. Quisque nec mi est. Ut "
            "quam ante, rutrum at pellentesque gravida, pretium in dui. Cras eget sapien velit. Suspendisse ut sem nec tellu"
            "s vehicula eleifend sit amet quis velit. Phasellus quis suscipit nisi. Nam elementum malesuada tincidunt. Curab"
            "itur iaculis pretium eros, malesuada faucibus leo eleifend a. Curabitur congue orci in neque euismod a blandit "
            "libero vehicula. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer sodales laoreet commodo. Phas"
            "ellus a purus eu risus elementum consequat. Aenean eu elit ut nunc convallis laoreet non ut libero. Suspendisse"
            " interdum placerat risus vel ornare. Donec vehicula, turpis sed consectetur ullamcorper, ante nunc egestas quam"
            ", ultricies adipiscing velit enim at nunc. Aenean id diam neque. Praesent ut lacus sed justo viverra fermentum "
            "et ut sem. Fusce convallis gravida lacinia. Integer semper dolor ut elit sagittis lacinia. Praesent sodales sce"
            "lerisque eros at rhoncus. Duis posuere sapien vel ipsum ornare interdum at eu quam. Vestibulum vel massa erat. "
            "Aenean quis sagittis purus. Phasellus arcu purus, rutrum id consectetur non, bibendum at nibh. \n\nDuis nec era"
            "t dolor. Nulla vitae consectetur ligula. Quisque nec mi est. Ut quam ante, rutrum at pellentesque gravida, pret"
            "ium in dui. Cras eget sapien velit. Suspendisse ut sem nec tellus vehicula eleifend sit amet quis velit. Phasel"
            "lus quis suscipit nisi. Nam";
    }
}
