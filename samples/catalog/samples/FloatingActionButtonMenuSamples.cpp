// FloatingActionButtonMenuSamples.kt
#include "FloatingActionButtonMenuSamples.h"
#include "Common.h"

#include <kiln/foundation/Back.h>
#include <kiln/foundation/Focus.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/material3/FloatingActionButton.h>
#include <kiln/material3/FloatingActionButtonMenu.h>
#include <kiln/material3/Tooltip.h>
#include <kiln/text/Paragraph.h>

#include <cmath>
#include <optional>

namespace catalog::samples {
    void FloatingActionButtonMenuSample() {
        LazyListState& listState = RememberLazyListState();
        const bool fabVisible = listState.FirstVisibleItemIndex() == 0 || !listState.CanScrollForward();
        auto& focusRequester = Here().Remember<std::shared_ptr<FocusRequester>>([] { return std::make_shared<FocusRequester>(); });

        Box({}, Alignment::TopStart(), false, [&listState, fabVisible, focusRequester] {
            LazyColumn(FillMax({}, Axis::Both), listState, [](LazyListScope& scope) {
                for (int index = 0; index < 100; ++index) {
                    scope.Item([index] { m3::Text("List item - " + std::to_string(index), Padding(FillMax(Clickable({}, [] {}), Axis::Horizontal), 24_dp)); },
                               kiln::rt::Key(index));
                }
            });

            static const ImageVectorPtr* const icons[6] = { &Icons::AutoMirrored::Filled::Message(), &Icons::Filled::People(), &Icons::Filled::Contacts(),
                                                            &Icons::Filled::Snooze(), &Icons::Filled::Archive(), &Icons::AutoMirrored::Filled::Label() };
            static const char* const labels[6] = { "Reply", "Reply all", "Forward", "Snooze", "Archive", "Label" };

            auto& fabMenuExpanded = RememberSaveableState(false);
            BackHandler(fabMenuExpanded.Get(), [&fabMenuExpanded] { fabMenuExpanded.Set(false); });
            const bool expanded = fabMenuExpanded.Get();

            m3::FloatingActionButtonMenu(
                expanded,
                [&fabMenuExpanded, expanded, fabVisible, focusRequester] {
                    // A FAB should have a tooltip associated with it.
                    m3::TooltipBox(m3::TooltipDefaults::rememberTooltipPositionProvider(expanded ? m3::TooltipAnchorPosition::Start
                                                                                                 : m3::TooltipAnchorPosition::Above),
                                   [](const m3::TooltipScope& s) {
                                       m3::PlainTooltip(s, Semantics({}, [](SemanticsConfiguration& c) {
                                                            c.liveRegion = LiveRegionMode::Assertive;
                                                            c.paneTitle = "Toggle menu";
                                                        }),
                                                        {}, [] { m3::Text("Toggle menu"); });
                                   },
                                   m3::RememberTooltipState(), {}, {}, [&fabMenuExpanded, expanded, fabVisible, focusRequester] {
                                       Modifier m = Semantics({}, [expanded](SemanticsConfiguration& c) {
                                           c.traversalIndex = -1.f;
                                           c.stateDescription = expanded ? "Expanded" : "Collapsed";
                                           c.contentDescription = { "Toggle menu" };
                                       });
                                       m = FocusRequesterModifier(m3::AnimateFloatingActionButton(m, fabVisible || expanded, Alignment::BottomEnd()), *focusRequester);
                                       m3::ToggleFloatingActionButton(expanded, [&fabMenuExpanded](bool) { fabMenuExpanded.Set(!fabMenuExpanded.Peek()); }, m, {},
                                                                      [](const m3::ToggleFloatingActionButtonScope& scope) {
                                                                          const ImageVectorPtr& imageVector =
                                                                              scope.checkedProgress > 0.5f ? Icons::Filled::Close() : Icons::Filled::Add();
                                                                          m3::Icon(imageVector, std::nullopt,
                                                                                   m3::ToggleFloatingActionButtonDefaults::AnimateIcon({}, scope.checkedProgress));
                                                                      });
                                   });
                },
                AlignInBox({}, Alignment::BottomEnd()), AlignmentH::End(),
                [&fabMenuExpanded, focusRequester](const m3::FloatingActionButtonMenuScope& scope) {
                    for (int i = 0; i < 6; ++i) {
                        Modifier m = Semantics({}, [i, &fabMenuExpanded](SemanticsConfiguration& c) {
                            c.isTraversalGroup = true;
                            // A custom a11y action to allow closing the menu when focusing the last menu
                            // item, since the close button comes before the first menu item in the
                            // traversal order.
                            if (i == 5) c.customActions = { { "Close menu", [&fabMenuExpanded] { fabMenuExpanded.Set(false); return true; } } };
                        });
                        if (i == 0) {
                            // Navigating back from the first item should go back to the FAB menu button.
                            // (Compose's Key.NumPadDirectionUp is a synthetic key Android never reports.)
                            m = OnKeyEvent(m, [focusRequester](const KeyEvent& e) {
                                if (e.type == KeyEventType::KeyDown && (e.keyCode == keys::DpadUp || (e.Shift() && e.keyCode == keys::Tab))) {
                                    focusRequester->RequestFocus();
                                    return true;
                                }
                                return false;
                            });
                        }
                        const ImageVectorPtr* icon = icons[i];
                        const std::string label = labels[i];
                        m3::FloatingActionButtonMenuItem(
                            scope, [&fabMenuExpanded] { fabMenuExpanded.Set(false); },
                            [label] {
                                // Adjust Text width to match longest line if multiline, since Text doesn't
                                // do that automatically (https://issuetracker.google.com/issues/206039942).
                                auto& layoutResult = Here().Remember<std::shared_ptr<std::optional<kiln::text::TextLayoutResult>>>(
                                    [] { return std::make_shared<std::optional<kiln::text::TextLayoutResult>>(); });
                                auto result = layoutResult;
                                m3::TextOptions o;
                                o.onTextLayout = [result](const kiln::text::TextLayoutResult& r) { *result = r; };
                                m3::Text(label, LayoutMod({}, [result](MeasureScope& s, Measurable& measurable, const Constraints& constraints) {
                                             Placeable& placeable = measurable.Measure(constraints);
                                             const auto& r = **result;
                                             float minX = INFINITY, maxX = -INFINITY;
                                             for (int line = 0; line < r.LineCount(); ++line) {
                                                 minX = std::min(minX, r.LineLeft(line));
                                                 maxX = std::max(maxX, r.LineRight(line));
                                             }
                                             return s.Layout(int(std::ceil(maxX - minX)), placeable.Height(),
                                                             [&placeable, minX](PlacementScope& p) { p.Place(placeable, -int(std::floor(minX)), 0); });
                                         }),
                                         o);
                            },
                            [icon] { m3::Icon(*icon, std::nullopt); }, m);
                    }
                });
        });
    }
}
