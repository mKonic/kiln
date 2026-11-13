// ListSamples.kt
#include "ListSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/foundation/AnimatedVisibility.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/material3/Checkbox.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/ListItem.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/RadioButton.h>

#include <optional>

namespace catalog::samples {
    namespace LD = m3::ListItemDefaults;

    namespace {
        constexpr Dp legacyExtraPaddingForLeadingTrailing = 4_dp;

        kiln::Function<void()> TextOf(std::string text, Modifier modifier = {}) {
            return [text, modifier] { m3::Text(text, modifier); };
        }
        void FavoriteIcon(std::optional<std::string> description = std::nullopt, Modifier modifier = {}) {
            m3::Icon(Icons::Filled::Favorite(), description, modifier);
        }

        // The legacy samples: one item between dividers, with the old paddings.
        void Legacy(PaddingValues padding, const char* headline, kiln::Function<void()> overline, kiln::Function<void()> supporting, bool meta) {
            Column({}, [=] {
                m3::HorizontalDivider();
                m3::ListItemOptions o;
                o.leadingContent = [] {
                    FavoriteIcon(std::string("Localized description"), Padding({}, PaddingValues{ 0_dp, 0_dp, legacyExtraPaddingForLeadingTrailing, 0_dp }));
                };
                o.overlineContent = overline;
                o.supportingContent = supporting;
                if (meta) o.trailingContent = TextOf("meta", Padding({}, PaddingValues{ legacyExtraPaddingForLeadingTrailing, 0_dp, 0_dp, 0_dp }));
                o.contentPadding = padding;
                m3::ListItem({}, o, TextOf(headline));
                m3::HorizontalDivider();
            });
        }

        // The four standard items: one, two, three (overline) and three (two supporting lines).
        void FourItems(kiln::Function<void(int index, m3::ListItemOptions& o, kiln::Function<void()> headline)> item) {
            static const char* const headlines[4] = { "One line list item", "Two line list item", "Three line list item", "Another three line list item" };
            for (int i = 0; i < 4; ++i) {
                m3::ListItemOptions o;
                o.leadingContent = [] { FavoriteIcon(); };
                if (i == 1 || i == 2) o.supportingContent = TextOf("Supporting text");
                if (i == 2) o.overlineContent = TextOf("Overline text");
                if (i == 3) o.supportingContent = TextOf("Supporting text\nthat is multiple lines");
                item(i, o, TextOf(headlines[i]));
            }
        }

        m3::ListItemColors SurfaceContainerColors() {
            m3::ListItemColors c;
            c.containerColor = m3::MaterialTheme::colorScheme().surfaceContainer;
            return LD::colors(c);
        }
        Modifier Scrolling() { return VerticalScroll({}, RememberScrollState()); }
    }

    void LegacyOneLineListItem() { Legacy(PaddingValues{ 16_dp, 8_dp, 16_dp, 8_dp }, "One line list item with 24x24 icon", {}, {}, false); }
    void LegacyTwoLineListItem() { Legacy(PaddingValues{ 16_dp, 8_dp, 16_dp, 8_dp }, "Two line list item with trailing", {}, TextOf("Secondary text"), true); }
    void LegacyThreeLineListItemWithOverlineAndSupporting() {
        Legacy(PaddingValues{ 16_dp, 12_dp, 16_dp, 12_dp }, "Three line list item", TextOf("OVERLINE"), TextOf("Secondary text"), true);
    }
    void LegacyThreeLineListItemWithExtendedSupporting() {
        Legacy(PaddingValues{ 16_dp, 12_dp, 16_dp, 12_dp }, "Three line list item", {}, TextOf("Secondary text that\nspans multiple lines"), true);
    }

    void StandardListItems() {
        Column(Scrolling(), [] {
            m3::HorizontalDivider();
            FourItems([](int, m3::ListItemOptions& o, kiln::Function<void()> headline) {
                m3::ListItem({}, o, headline);
                m3::HorizontalDivider();
            });
        });
    }

    void SegmentedListItems() {
        const auto colors = SurfaceContainerColors();
        Column(Scrolling(), Arrangement::SpacedBy(LD::SegmentedGap), AlignmentH::Start(), [colors] {
            FourItems([colors](int i, m3::ListItemOptions& o, kiln::Function<void()> headline) {
                o.colors = colors;
                m3::SegmentedListItem(LD::segmentedShapes(i, 4), {}, o, headline);
            });
        });
    }

    void ClickableListItemSample() {
        Column(Scrolling(), [] {
            m3::HorizontalDivider();
            for (int idx = 0; idx < 3; ++idx) {
                Here().Child([idx] {
                    auto& count = RememberSaveableState(0);
                    m3::ListItemOptions o;
                    o.leadingContent = [] { m3::Icon(Icons::Filled::Home(), std::nullopt); };
                    o.trailingContent = TextOf(std::to_string(count.Get()));
                    o.supportingContent = TextOf("Additional info");
                    m3::ListItem([&count] { count.Set(count.Peek() + 1); }, {}, o, TextOf("Item " + std::to_string(idx + 1)));
                    m3::HorizontalDivider();
                }, kiln::rt::Key(idx));
            }
        });
    }

    void ClickableListItemWithClickableChildSample() {
        Column(Scrolling(), [] {
            m3::HorizontalDivider();
            for (int idx = 0; idx < 3; ++idx) {
                m3::ListItemOptions o;
                o.leadingContent = [] { m3::Icon(Icons::Filled::Home(), std::nullopt); };
                o.trailingContent = [] {
                    m3::IconButton([] { /* Child onClick callback */ }, {}, {}, [] { FavoriteIcon(std::string("Localized description")); });
                };
                o.supportingContent = TextOf("The trailing icon has a separate click action");
                m3::ListItem([] { /* ListItem onClick callback */ }, {}, o, TextOf("Item " + std::to_string(idx + 1)));
                m3::HorizontalDivider();
            }
        });
    }

    void SingleSelectionListItemSample() {
        Column(SelectableGroup(Scrolling()), [] {
            m3::HorizontalDivider();
            auto& selectedIndex = RememberSaveableState(-1);   // -1: none
            for (int idx = 0; idx < 3; ++idx) {
                const bool selected = selectedIndex.Get() == idx;
                m3::ListItemOptions o;
                o.leadingContent = [selected] { m3::RadioButton(selected, nullptr); };
                o.trailingContent = [] { FavoriteIcon(); };
                o.supportingContent = TextOf("Additional info");
                m3::ListItem(selected, [&selectedIndex, selected, idx] { selectedIndex.Set(selected ? -1 : idx); }, {}, o,
                             TextOf("Item " + std::to_string(idx + 1)));
                m3::HorizontalDivider();
            }
        });
    }

    void MultiSelectionListItemSample() {
        Column(Scrolling(), [] {
            m3::HorizontalDivider();
            for (int idx = 0; idx < 3; ++idx) {
                Here().Child([idx] {
                    auto& checked = RememberSaveableState(false);
                    const bool c = checked.Get();
                    m3::ListItemOptions o;
                    o.leadingContent = [c] { m3::Checkbox(c, nullptr); };
                    o.trailingContent = [] { FavoriteIcon(); };
                    o.supportingContent = TextOf("Additional info");
                    m3::ListItem(c, [&checked](bool v) { checked.Set(v); }, {}, o, TextOf("Item " + std::to_string(idx + 1)));
                    m3::HorizontalDivider();
                }, kiln::rt::Key(idx));
            }
        });
    }

    void ListItemWithModeChangeOnLongClickSample() {
        Column(Scrolling(), [] {
            m3::HorizontalDivider();
            auto& inClickMode = RememberSaveableState(true);
            auto& counts = RememberSaveableList(std::vector<int>{ 0, 0, 0 });
            auto& checked = RememberSaveableList(std::vector<bool>{ false, false, false });
            for (int idx = 0; idx < 3; ++idx) {
                m3::ListItemOptions o;
                o.supportingContent = TextOf("Long-click to change interaction mode.");
                const auto headline = TextOf("Item " + std::to_string(idx + 1));
                if (inClickMode.Get()) {
                    o.onLongClick = [&checked, &inClickMode, idx] {
                        auto v = checked.Peek();
                        v[std::size_t(idx)] = true;
                        checked.Set(v);
                        inClickMode.Set(false);
                    };
                    o.leadingContent = [] { m3::Icon(Icons::Filled::Home(), std::nullopt); };
                    o.trailingContent = TextOf(std::to_string(counts.Get()[std::size_t(idx)]));
                    m3::ListItem([&counts, idx] {
                        auto v = counts.Peek();
                        v[std::size_t(idx)]++;
                        counts.Set(v);
                    }, {}, o, headline);
                } else {
                    const bool c = checked.Get()[std::size_t(idx)];
                    o.onLongClick = [&checked, &inClickMode] {
                        inClickMode.Set(true);
                        checked.Set({ false, false, false });
                    };
                    o.leadingContent = [c] { m3::Checkbox(c, nullptr); };
                    o.trailingContent = [] { FavoriteIcon(); };
                    m3::ListItem(c, [&checked, idx](bool v) {
                        auto list = checked.Peek();
                        list[std::size_t(idx)] = v;
                        checked.Set(list);
                    }, {}, o, headline);
                }
                m3::HorizontalDivider();
            }
        });
    }

    void SingleSelectionSegmentedListItemSample() {
        constexpr int count = 4;
        const auto colors = SurfaceContainerColors();
        Column(SelectableGroup(Scrolling()), Arrangement::SpacedBy(LD::SegmentedGap), AlignmentH::Start(), [colors] {
            auto& selectedIndex = RememberSaveableState(-1);
            for (int idx = 0; idx < count; ++idx) {
                const bool selected = selectedIndex.Get() == idx;
                m3::ListItemOptions o;
                o.colors = colors;
                o.leadingContent = [selected] { m3::RadioButton(selected, nullptr); };
                o.trailingContent = [] { FavoriteIcon(); };
                o.supportingContent = TextOf("Additional info");
                m3::SegmentedListItem(selected, [&selectedIndex, selected, idx] { selectedIndex.Set(selected ? -1 : idx); }, LD::segmentedShapes(idx, count),
                                      {}, o, TextOf("Item " + std::to_string(idx + 1)));
            }
        });
    }

    void MultiSelectionSegmentedListItemSample() {
        constexpr int count = 4;
        const auto colors = SurfaceContainerColors();
        Column(Scrolling(), Arrangement::SpacedBy(LD::SegmentedGap), AlignmentH::Start(), [colors] {
            for (int idx = 0; idx < count; ++idx) {
                Here().Child([idx, colors] {
                    auto& checked = RememberSaveableState(false);
                    const bool c = checked.Get();
                    m3::ListItemOptions o;
                    o.colors = colors;
                    o.leadingContent = [c] { m3::Checkbox(c, nullptr); };
                    o.trailingContent = [] { FavoriteIcon(); };
                    o.supportingContent = TextOf("Additional info");
                    m3::SegmentedListItem(c, [&checked](bool v) { checked.Set(v); }, LD::segmentedShapes(idx, count), {}, o,
                                          TextOf("Item " + std::to_string(idx + 1)));
                }, kiln::rt::Key(idx));
            }
        });
    }

    void SegmentedListItemWithExpansionSample() {
        auto& expanded = RememberSaveableState(false);
        constexpr int numChildren = 3;
        const int itemCount = 1 + (expanded.Get() ? numChildren : 0);
        auto& childrenChecked = RememberSaveableList(std::vector<bool>(numChildren, false));
        const auto colors = SurfaceContainerColors();
        Column(VerticalScroll(FillMax({}, Axis::Both), RememberScrollState()), Arrangement::SpacedBy(LD::SegmentedGap), AlignmentH::Start(),
               [&expanded, &childrenChecked, itemCount, colors] {
                   Spacer(Height({}, 50_dp));
                   const bool isExpanded = expanded.Get();
                   m3::ListItemOptions o;
                   o.colors = colors;
                   o.leadingContent = [] { FavoriteIcon(); };
                   o.trailingContent = [isExpanded] { m3::Icon(isExpanded ? Icons::Filled::ExpandLess() : Icons::Filled::ExpandMore(), std::nullopt); };
                   m3::SegmentedListItem([&expanded] { expanded.Set(!expanded.Peek()); }, LD::segmentedShapes(0, itemCount),
                                         Semantics({}, [isExpanded](SemanticsConfiguration& c) { c.stateDescription = isExpanded ? "Expanded" : "Collapsed"; }),
                                         o, TextOf("Click to expand/collapse"));
                   const auto spec = m3::MaterialTheme::motionScheme()->FastSpatialSpec();
                   AnimatedVisibility(isExpanded, {}, [&childrenChecked, itemCount, colors] {
                       Column({}, Arrangement::SpacedBy(LD::SegmentedGap), AlignmentH::Start(), [&childrenChecked, itemCount, colors] {
                           for (int idx = 0; idx < numChildren; ++idx) {
                               const bool c = childrenChecked.Get()[std::size_t(idx)];
                               m3::ListItemOptions child;
                               child.colors = colors;
                               child.leadingContent = [] { FavoriteIcon(); };
                               child.trailingContent = [c] { m3::Checkbox(c, nullptr); };
                               m3::SegmentedListItem(c, [&childrenChecked, idx](bool v) {
                                   auto list = childrenChecked.Peek();
                                   list[std::size_t(idx)] = v;
                                   childrenChecked.Set(list);
                               }, LD::segmentedShapes(idx + 1, itemCount), {}, child, TextOf("Child " + std::to_string(idx + 1)));
                           }
                       });
                   }, ExpandVertically(spec), ShrinkVertically(spec));
               });
    }
}
