// TooltipSamples.kt
#include "TooltipSamples.h"
#include "Common.h"

#include <kiln/material3/Button.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/Tooltip.h>

namespace catalog::samples {
    namespace TD = m3::TooltipDefaults;

    namespace {
        const char* const richTooltipSubheadText = "Permissions";
        const char* const richTooltipText =
            "Configure permissions for selected service accounts. You can add and remove service account members and assign roles to them. "
            "Visit go/permissions for details";
        const char* const richTooltipActionText = "Request Access";

        void IconAnchor(const ImageVectorPtr& icon) {
            m3::IconButton([] {}, {}, {}, [&icon] { m3::Icon(icon, std::string("Localized Description")); });
        }

        // A plain tooltip announced as `text` (the samples' workaround for b/496338253).
        void Plain(const m3::TooltipScope& scope, const char* text, kiln::foundation::ShapePtr caret = nullptr) {
            m3::PlainTooltipOptions o;
            o.caretShape = std::move(caret);
            m3::PlainTooltip(scope, Semantics({}, [text](SemanticsConfiguration& c) {
                                 c.liveRegion = LiveRegionMode::Assertive;
                                 c.paneTitle = text;
                             }),
                             o, [text] { m3::Text(text); });
        }

        // The plain "Add to favorites" samples: a favourite icon, the tooltip on `position`.
        void Favorites(m3::TooltipAnchorPosition position, kiln::foundation::ShapePtr caret) {
            m3::TooltipBox(TD::rememberTooltipPositionProvider(position),
                           [caret](const m3::TooltipScope& scope) { Plain(scope, "Add to favorites", caret); }, m3::RememberTooltipState(), {}, {},
                           [] { IconAnchor(Icons::Filled::Favorite()); });
        }

        // The rich samples: persistent, with an action that dismisses it.
        void Rich(const m3::TooltipState& tooltipState, kiln::foundation::ShapePtr caret) {
            m3::TooltipBoxOptions box;
            box.hasAction = true;
            m3::TooltipBox(TD::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Above),
                           [tooltipState, caret](const m3::TooltipScope& s) {
                               m3::RichTooltipOptions o;
                               o.title = [] { m3::Text(richTooltipSubheadText); };
                               o.action = [tooltipState] {
                                   m3::TextButton([tooltipState] { tooltipState.Dismiss(); }, {}, {}, [] { m3::Text(richTooltipActionText); });
                               };
                               o.caretShape = caret;
                               m3::RichTooltip(s, {}, o, [] { m3::Text(richTooltipText); });
                           },
                           tooltipState, {}, box, [] { IconAnchor(Icons::Filled::Info()); });
        }

        // The "Display tooltip" button under the manual samples.
        void DisplayButton(const m3::TooltipState& tooltipState, const LaunchScope& scope) {
            Spacer(SizeIn({}, Dp::Unspecified(), 30_dp, Dp::Unspecified(), 30_dp, false));   // requiredHeight
            m3::OutlinedButton([tooltipState, scope] { scope.Launch(tooltipState.Show()); }, {}, {}, [] { m3::Text("Display tooltip"); });
        }
    }

    void PlainTooltipSample() { Favorites(m3::TooltipAnchorPosition::Above, nullptr); }

    void PlainTooltipWithManualInvocationSample() {
        const m3::TooltipState tooltipState = m3::RememberTooltipState();
        const LaunchScope scope = RememberCoroutineScope();
        Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), [tooltipState, scope] {
            m3::TooltipBox(TD::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Above),
                           [](const m3::TooltipScope& s) { Plain(s, "Add to list"); }, tooltipState, {}, {},
                           [] { IconAnchor(Icons::Filled::AddCircle()); });
            DisplayButton(tooltipState, scope);
        });
    }

    // Despite the name, upstream's sample passes no caret shape.
    void PlainTooltipWithCaret() { Favorites(m3::TooltipAnchorPosition::Above, nullptr); }
    void PlainTooltipWithCaretBelowAnchor() { Favorites(m3::TooltipAnchorPosition::Below, TD::caretShape()); }
    void PlainTooltipWithCaretLeftOfAnchor() { Favorites(m3::TooltipAnchorPosition::Left, TD::caretShape()); }
    void PlainTooltipWithCaretRightOfAnchor() { Favorites(m3::TooltipAnchorPosition::Right, TD::caretShape()); }
    void PlainTooltipWithCaretStartOfAnchor() { Favorites(m3::TooltipAnchorPosition::Start, TD::caretShape()); }
    void PlainTooltipWithCaretEndOfAnchor() { Favorites(m3::TooltipAnchorPosition::End, TD::caretShape()); }
    void PlainTooltipWithCustomCaret() { Favorites(m3::TooltipAnchorPosition::Above, TD::caretShape({ 24_dp, 12_dp })); }

    void RichTooltipSample() {
        const m3::TooltipState tooltipState = m3::RememberTooltipState(false, true);
        Rich(tooltipState, nullptr);
    }

    void RichTooltipWithManualInvocationSample() {
        const m3::TooltipState tooltipState = m3::RememberTooltipState(false, true);
        const LaunchScope scope = RememberCoroutineScope();
        Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), [tooltipState, scope] {
            Rich(tooltipState, nullptr);
            DisplayButton(tooltipState, scope);
        });
    }

    void RichTooltipWithCaretSample() {
        const m3::TooltipState tooltipState = m3::RememberTooltipState(false, true);
        Rich(tooltipState, TD::caretShape());
    }

    void RichTooltipWithCustomCaretSample() {
        const m3::TooltipState tooltipState = m3::RememberTooltipState(false, true);
        Rich(tooltipState, TD::caretShape({ 32_dp, 16_dp }));
    }
}
