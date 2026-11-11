// FloatingActionButtonSamples.kt
#include "FloatingActionButtonSamples.h"
#include "Common.h"

#include <kiln/foundation/Lazy.h>
#include <kiln/material3/FloatingActionButton.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Tooltip.h>

namespace catalog::samples {
    namespace FD = m3::FloatingActionButtonDefaults;

    namespace {
        using FabFn = void (*)(std::function<void()>, const Modifier&, const m3::FloatingActionButtonOptions&, std::function<void()>);
        using ExtendedFn = void (*)(std::function<void()>, std::function<void()>, std::function<void()>, const Modifier&, bool,
                                    const m3::FloatingActionButtonOptions&);

        // A FAB should have a tooltip associated with it. `shown` false: the tooltip is empty.
        void WithTooltip(std::function<void()> anchor, bool shown = true) {
            m3::TooltipBox(m3::TooltipDefaults::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Above),
                           [shown](const m3::TooltipScope& scope) {
                               if (!shown) return;
                               // TODO(b/496338253) upstream: the semantics make the tooltip text announced.
                               m3::PlainTooltip(scope, Semantics({}, [](SemanticsConfiguration& c) {
                                                    c.liveRegion = LiveRegionMode::Assertive;
                                                    c.paneTitle = "Localized description";
                                                }),
                                                {}, [] { m3::Text("Localized description"); });
                           },
                           m3::RememberTooltipState(), {}, {}, std::move(anchor));
        }

        void AddIcon(std::optional<Dp> size = std::nullopt, const char* description = "Localized description") {
            m3::Icon(Icons::Filled::Add(), std::string(description), size ? Size({}, *size) : Modifier{});
        }

        // The list under the animated samples' FAB.
        void HundredItems(LazyListState& listState) {
            LazyColumn(FillMax({}, Axis::Both), listState, [](LazyListScope& s) {
                for (int index = 0; index < 100; ++index)
                    s.Item([index] { m3::Text("List item - " + std::to_string(index), Padding({}, 24_dp)); });
            });
        }

        // The FAB shows (or expands) while the first item is the first visible one.
        bool FirstItemVisible(LazyListState& listState) {
            auto& visible = Here().RememberNew<kiln::rt::Derived<bool>>(kiln::rt::Keys(&listState), {}, [&listState] {
                return listState.FirstVisibleItemIndex() == 0;
            });
            return visible.Get();
        }

        void AnimatedExtended(ExtendedFn fab, const char* text, std::optional<Dp> iconSize) {
            auto& listState = RememberLazyListState();
            m3::ScaffoldOptions o;
            o.floatingActionButton = [&listState, fab, text, iconSize] {
                const bool expandedFab = FirstItemVisible(listState);
                // A collapsed FAB should have a tooltip associated with it.
                WithTooltip([fab, text, iconSize, expandedFab] {
                    fab([text] { m3::Text(text); }, [iconSize] { AddIcon(iconSize, "Localized Description"); }, [] {}, {}, expandedFab, {});
                }, !expandedFab);
            };
            o.floatingActionButtonPosition = m3::FabPosition::End;
            m3::Scaffold({}, o, [&listState](const PaddingValues&) { HundredItems(listState); });
        }
    }

    void FloatingActionButtonSample() {
        WithTooltip([] { m3::FloatingActionButton([] {}, {}, {}, [] { AddIcon(); }); });
    }
    void SmallFloatingActionButtonSample() {
        WithTooltip([] { m3::SmallFloatingActionButton([] {}, {}, {}, [] { AddIcon(); }); });
    }
    void MediumFloatingActionButtonSample() {
        WithTooltip([] { m3::MediumFloatingActionButton([] {}, {}, {}, [] { AddIcon(FD::MediumIconSize); }); });
    }
    void LargeFloatingActionButtonSample() {
        WithTooltip([] { m3::LargeFloatingActionButton([] {}, {}, {}, [] { AddIcon(FD::LargeIconSize); }); });
    }

    void AnimatedFloatingActionButtonSample() {
        auto& listState = RememberLazyListState();
        m3::ScaffoldOptions o;
        o.floatingActionButton = [&listState] {
            const bool fabVisible = FirstItemVisible(listState);
            WithTooltip([fabVisible] {
                m3::MediumFloatingActionButton([] {}, m3::AnimateFloatingActionButton({}, fabVisible, Alignment::BottomEnd()), {},
                                               [] { AddIcon(FD::MediumIconSize); });
            });
        };
        o.floatingActionButtonPosition = m3::FabPosition::End;
        m3::Scaffold({}, o, [&listState](const PaddingValues&) { HundredItems(listState); });
    }

    void ExtendedFloatingActionButtonTextSample() { m3::ExtendedFloatingActionButton([] {}, {}, {}, [] { m3::Text("Extended FAB"); }); }
    void SmallExtendedFloatingActionButtonTextSample() {
        m3::SmallExtendedFloatingActionButton([] {}, {}, {}, [] { m3::Text("Small Extended FAB"); });
    }
    void MediumExtendedFloatingActionButtonTextSample() {
        m3::MediumExtendedFloatingActionButton([] {}, {}, {}, [] { m3::Text("Medium Extended FAB"); });
    }
    void LargeExtendedFloatingActionButtonTextSample() {
        m3::LargeExtendedFloatingActionButton([] {}, {}, {}, [] { m3::Text("Large Extended FAB"); });
    }

    void ExtendedFloatingActionButtonSample() {
        m3::ExtendedFloatingActionButton([] { m3::Text("Extended FAB"); }, [] { AddIcon(); }, [] {});
    }
    void SmallExtendedFloatingActionButtonSample() {
        m3::SmallExtendedFloatingActionButton([] { m3::Text("Small Extended FAB"); }, [] { AddIcon(); }, [] {});
    }
    void MediumExtendedFloatingActionButtonSample() {
        m3::MediumExtendedFloatingActionButton([] { m3::Text("Medium Extended FAB"); }, [] { AddIcon(FD::MediumIconSize); }, [] {});
    }
    void LargeExtendedFloatingActionButtonSample() {
        m3::LargeExtendedFloatingActionButton([] { m3::Text("Large Extended FAB"); }, [] { AddIcon(FD::LargeIconSize); }, [] {});
    }

    void AnimatedExtendedFloatingActionButtonSample() { AnimatedExtended(m3::ExtendedFloatingActionButton, "Extended FAB", std::nullopt); }
    void SmallAnimatedExtendedFloatingActionButtonSample() {
        AnimatedExtended(m3::SmallExtendedFloatingActionButton, "Small Extended FAB", std::nullopt);
    }
    void MediumAnimatedExtendedFloatingActionButtonSample() {
        AnimatedExtended(m3::MediumExtendedFloatingActionButton, "Medium Extended FAB", FD::MediumIconSize);
    }
    void LargeAnimatedExtendedFloatingActionButtonSample() {
        AnimatedExtended(m3::LargeExtendedFloatingActionButton, "Large Extended FAB", FD::LargeIconSize);
    }
}
