#include "MaterialComponents.h"

#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Flow.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/material3/Checkbox.h>
#include <kiln/material3/Card.h>
#include <kiln/material3/Chip.h>
#include <kiln/material3/ListItem.h>
#include <kiln/material3/ProgressIndicator.h>
#include <kiln/material3/WavyProgressIndicator.h>
#include <kiln/material3/LoadingIndicator.h>
#include <kiln/material3/RadioButton.h>
#include <kiln/material3/Switch.h>
#include <kiln/material3/Badge.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/DynamicColorScheme.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/FloatingActionButton.h>
#include <kiln/material3/FloatingActionButtonMenu.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Surface.h>
#include <kiln/material3/ToggleButton.h>
#include <kiln/material3/Text.h>
#include <kiln/material3/internal/Icons.h>

namespace kiln::gallery {

    using namespace kiln::foundation;
    using namespace kiln::ui;
    namespace m3 = kiln::material3;

    namespace {
        void Buttons() {
            auto& clicks = RememberState(0);
            auto* count = &clicks;
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [count] {
                auto onClick = [count] { count->Set(count->Peek() + 1); };
                m3::Button(onClick, {}, {}, [] { m3::Text("Filled"); });
                m3::ElevatedButton(onClick, {}, {}, [] { m3::Text("Elevated"); });
                m3::FilledTonalButton(onClick, {}, {}, [] { m3::Text("Tonal"); });
                m3::OutlinedButton(onClick, {}, {}, [] { m3::Text("Outlined"); });
                m3::TextButton(onClick, {}, {}, [] { m3::Text("Text"); });
                m3::ButtonOptions expressive;
                expressive.shapes = m3::ButtonDefaults::shapesFor(m3::ButtonDefaults::MediumContainerHeight);
                expressive.contentPadding = m3::ButtonDefaults::contentPaddingFor(m3::ButtonDefaults::MediumContainerHeight);
                // A minimum height, not a fixed height: let the label grow at large font scales.
                m3::Button(onClick, DefaultMinSize({}, Dp::Unspecified(), m3::ButtonDefaults::MediumContainerHeight), expressive, [] {
                    m3::TextOptions text;
                    text.style = m3::ButtonDefaults::textStyleFor(m3::ButtonDefaults::MediumContainerHeight);
                    m3::Text("Expressive", {}, text);
                });
                m3::ButtonOptions disabled;
                disabled.enabled = false;
                m3::Button(onClick, {}, disabled, [] { m3::Text("Disabled"); });
                m3::BadgedBox([count] { m3::Badge({}, std::nullopt, std::nullopt, [count] { m3::Text(std::to_string(count->Get())); }); },
                              Padding({}, 8_dp, 16_dp), [] { m3::Icon(m3::internal::Icons::Filled::Check(), std::string("Clicks")); });
            });
        }

        void ToggleButtons() {
            auto& checked = RememberState(false);
            auto* on = &checked;
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [on] {
                auto change = [on](bool value) { on->Set(value); };
                m3::ToggleButtonOptions icon;
                icon.icon = [] { m3::Icon(m3::internal::Icons::Filled::Check(), std::nullopt); };
                m3::ToggleButton(on->Get(), change, {}, icon, [] { m3::Text("Filled toggle"); });
                m3::ElevatedToggleButton(on->Get(), change, {}, {}, [] { m3::Text("Elevated toggle"); });
                m3::ToggleButtonOptions medium;
                medium.buttonSize = m3::ToggleButtonSize::Medium;
                m3::FilledTonalToggleButton(on->Get(), change, {}, medium, [] { m3::Text("Tonal toggle"); });
                m3::OutlinedToggleButton(on->Get(), change, {}, {}, [] { m3::Text("Outlined toggle"); });
                m3::ToggleButtonOptions disabled;
                disabled.enabled = false;
                m3::ToggleButton(on->Get(), change, {}, disabled, [] { m3::Text("Disabled toggle"); });
            });
            m3::Text(checked.Get() ? "Text toggles: on" : "Text toggles: off");
        }

        void ProgressIndicators() {
            auto* progress = &RememberState(0.35f);
            auto* unknown = &RememberState(true);
            auto named = [](const char* label) { return Semantics({}, [label](SemanticsConfiguration& c) { c.contentDescription = { label }; }); };
            const auto value = [progress] { return progress->Get(); };
            const auto changing = [=] { return unknown->Get() ? NAN : progress->Get(); };
            m3::Text("Linear progress");
            Column({}, Arrangement::SpacedBy(24_dp), AlignmentH::Start(), [=] {
                m3::LinearProgressIndicator(value, named("Linear progress"));
                m3::LinearProgressIndicator(named("Indeterminate linear progress"));
                m3::LinearWavyProgressIndicator(value, named("Wavy linear progress"));
                m3::LinearWavyProgressIndicator(named("Indeterminate wavy linear progress"));
                m3::LinearWavyProgressIndicator(changing, named("Handoff linear progress"));
            });
            m3::Text("Circular progress");
            FlowRow({}, Arrangement::SpacedBy(16_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [=] {
                m3::CircularProgressIndicator(value, named("Circular progress"));
                m3::CircularProgressIndicator(named("Indeterminate circular progress"));
                m3::CircularWavyProgressIndicator(value, named("Wavy circular progress"));
                m3::CircularWavyProgressIndicator(named("Indeterminate wavy circular progress"));
                m3::CircularWavyProgressIndicator(changing, named("Handoff circular progress"));
            });
            m3::Text("Loading indicators");
            FlowRow({}, Arrangement::SpacedBy(16_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [=] {
                m3::LoadingIndicator(value, named("Determinate loading"));
                m3::ContainedLoadingIndicator(value, named("Determinate contained loading"));
                m3::LoadingIndicator(named("Loading"));
                m3::ContainedLoadingIndicator(named("Contained loading"));
            });
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                m3::TextButton([progress] { progress->Set(progress->Peek() >= 0.99f ? 0.0f : std::min(1.0f, progress->Peek() + 0.15f)); }, {}, {}, [] { m3::Text("Advance progress"); });
                m3::TextButton([unknown] { unknown->Set(!unknown->Peek()); }, {}, {}, [unknown] { m3::Text(unknown->Get() ? "Make progress known" : "Make progress unknown"); });
            });
            m3::Text("Progress: " + std::to_string(RoundJava(progress->Get() * 100)) + "%");
        }

        void Lists() {
            auto icon = [] { m3::Icon(m3::internal::Icons::Filled::Check(), std::nullopt, SizeMod({}, 20_dp, 20_dp)); };
            auto meta = [] { m3::Text("Meta"); };
            Column({}, [=] {
                m3::ListItem([] { m3::Text("Classic one line"); }, {}, { .leadingContent = icon, .trailingContent = meta });
                m3::ListItem([] { m3::Text("Classic two lines"); }, {}, {
                    .supportingContent = [] { m3::Text("Supporting text"); }, .leadingContent = icon, .trailingContent = meta });
                m3::ListItem([] { m3::Text("Classic three lines"); }, {}, {
                    .overlineContent = [] { m3::Text("Overline"); }, .supportingContent = [] { m3::Text("Supporting text"); },
                    .leadingContent = icon, .trailingContent = meta });
            });
            auto* clicks = &RememberState(0);
            auto* longs = &RememberState(0);
            auto* selected = &RememberState(false);
            auto* checked = &RememberState(false);
            Column({}, Arrangement::SpacedBy(2_dp), AlignmentH::Start(), [=] {
                m3::ListItem({}, { .leadingContent = icon, .trailingContent = meta }, [] { m3::Text("Expressive plain"); });
                m3::ListItemOptions action;
                action.leadingContent = icon;
                action.trailingContent = meta;
                action.supportingContent = [] { m3::Text("Tap or long-press this item"); };
                action.onLongClick = [longs] { longs->Set(longs->Peek() + 1); };
                action.onLongClickLabel = "Select list item";
                m3::ListItem([clicks] { clicks->Set(clicks->Peek() + 1); }, {}, action, [] { m3::Text("Expressive action"); });
                m3::ListItemOptions radio;
                radio.leadingContent = icon;
                radio.trailingContent = [selected] { m3::RadioButton(selected->Get(), {}); };
                m3::ListItem(selected->Get(), std::function<void()>([selected] { selected->Set(!selected->Peek()); }), {}, radio, [] { m3::Text("Expressive radio"); });
                m3::ListItemOptions toggle;
                toggle.leadingContent = icon;
                toggle.trailingContent = [checked] { m3::Checkbox(checked->Get(), {}); };
                m3::ListItem(checked->Get(), std::function<void(bool)>([checked](bool value) { checked->Set(value); }), {}, toggle, [] { m3::Text("Expressive checkbox"); });
                m3::ListItem({}, { .enabled = false, .leadingContent = icon, .trailingContent = meta,
                    .supportingContent = [] { m3::Text("Disabled content"); } }, [] { m3::Text("Disabled list item"); });
            });
            m3::Text("Segmented list");
            Column({}, Arrangement::SpacedBy(m3::ListItemDefaults::SegmentedGap), AlignmentH::Start(), [=] {
                m3::SegmentedListItem(m3::ListItemDefaults::segmentedShapes(0, 3), {}, {
                    .leadingContent = icon, .overlineContent = [] { m3::Text("Overline"); },
                    .supportingContent = [] { m3::Text("Supporting text wraps naturally with large fonts"); } }, [] { m3::Text("Segmented plain"); });
                m3::SegmentedListItem([clicks] { clicks->Set(clicks->Peek() + 1); }, m3::ListItemDefaults::segmentedShapes(1, 3), {}, {
                    .leadingContent = icon, .trailingContent = meta }, [] { m3::Text("Segmented action"); });
                m3::SegmentedListItem(checked->Get(), std::function<void(bool)>([checked](bool value) { checked->Set(value); }),
                    m3::ListItemDefaults::segmentedShapes(2, 3), {}, {
                    .leadingContent = icon, .trailingContent = [checked] { m3::Checkbox(checked->Get(), {}); } }, [] { m3::Text("Segmented checkbox"); });
            });
            m3::Text("List actions: " + std::to_string(clicks->Get()));
            m3::Text("List long clicks: " + std::to_string(longs->Get()));
        }

        void Chips() {
            auto* actions = &RememberState(0);
            auto icon = [] { m3::Icon(m3::internal::Icons::Filled::Check(), std::nullopt, SizeMod({}, 18_dp, 18_dp)); };
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                auto click = [actions] { actions->Set(actions->Peek() + 1); };
                m3::ChipOptions options;
                options.leadingIcon = icon;
                options.trailingIcon = [] { m3::Icon(m3::internal::Icons::Filled::Close(), std::nullopt, SizeMod({}, 18_dp, 18_dp)); };
                m3::AssistChip(click, [] { m3::Text("Assist"); }, {}, options);
                options.trailingIcon = {};
                m3::ElevatedAssistChip(click, [] { m3::Text("Elevated assist"); }, {}, options);
                m3::SuggestionChip(click, [] { m3::Text("Suggestion"); }, {}, options);
                m3::ElevatedSuggestionChip(click, [] { m3::Text("Elevated suggestion"); });
                options.enabled = false;
                m3::AssistChip(click, [] { m3::Text("Disabled chip"); }, {}, options);
            });
            auto* selected = &RememberState(false);
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                auto click = [selected] { selected->Set(!selected->Peek()); };
                m3::SelectableChipOptions options;
                if (selected->Get()) options.leadingIcon = icon;
                m3::FilterChip(selected->Get(), click, [] { m3::Text("Filter"); }, {}, options);
                m3::ElevatedFilterChip(selected->Get(), click, [] { m3::Text("Elevated filter"); }, {}, options);
                options.shapes = m3::FilterChipDefaults::shapes();
                m3::FilterChip(selected->Get(), click, [] { m3::Text("Expressive filter"); }, {}, options);
                options.enabled = false;
                m3::FilterChip(true, [] {}, [] { m3::Text("Disabled filter"); }, {}, options);
            });
            auto* inputSelected = &RememberState(false);
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                auto click = [inputSelected] { inputSelected->Set(!inputSelected->Peek()); };
                m3::SelectableChipOptions options;
                options.leadingIcon = icon;
                options.trailingIcon = [] { m3::Icon(m3::internal::Icons::Filled::Close(), std::nullopt, SizeMod({}, 18_dp, 18_dp)); };
                m3::InputChip(inputSelected->Get(), click, [] { m3::Text("Input"); }, {}, options);
                options.avatar = [] {
                    Box(Background(SizeMod({}, 24_dp, 24_dp), m3::MaterialTheme::colorScheme().primaryContainer, CircleShape()), Alignment::Center(), false, [] {
                        m3::TextOptions text;
                        text.color = m3::MaterialTheme::colorScheme().onPrimaryContainer;
                        text.style = m3::MaterialTheme::typography().labelSmall;
                        m3::Text("K", {}, text);
                    });
                };
                m3::InputChip(inputSelected->Get(), click, [] { m3::Text("Avatar input"); }, {}, options);
                options.shapes = m3::InputChipDefaults::shapes();
                m3::InputChip(inputSelected->Get(), click, [] { m3::Text("Expressive input"); }, {}, options);
            });
            m3::Text("Chip actions: " + std::to_string(actions->Get()));
        }

        void Cards() {
            auto* enabled = &RememberState(true);
            auto* clicks = &RememberState(0);
            auto body = [](const char* title) {
                return [title] {
                    Column(Padding({}, 16_dp), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [title] {
                        m3::Text(title);
                        m3::Text("Details");
                    });
                };
            };
            const auto size = SizeIn({}, 128_dp, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified());
            FlowRow({}, Arrangement::SpacedBy(12_dp), Arrangement::SpacedBy(12_dp), AlignmentV::Top(), [=] {
                m3::Card(size, {}, body("Filled"));
                m3::ElevatedCard(size, {}, body("Elevated"));
                m3::OutlinedCard(size, {}, body("Outlined"));
            });
            FlowRow({}, Arrangement::SpacedBy(12_dp), Arrangement::SpacedBy(12_dp), AlignmentV::Top(), [=] {
                auto click = [clicks] { clicks->Set(clicks->Peek() + 1); };
                m3::CardOptions options;
                options.enabled = enabled->Get();
                m3::Card(click, size, options, body("Filled action"));
                m3::ElevatedCard(click, size, options, body("Elevated action"));
                m3::OutlinedCard(click, size, options, body("Outlined action"));
            });
            m3::TextButton([enabled] { enabled->Set(!enabled->Peek()); }, {}, {}, [enabled] { m3::Text(enabled->Get() ? "Disable cards" : "Enable cards"); });
            m3::Text("Card clicks: " + std::to_string(clicks->Get()));
        }

        void SelectionControls() {
            auto named = [](const char* label) {
                return Semantics({}, [label](SemanticsConfiguration& config) { config.contentDescription = { label }; });
            };
            auto* checked = &RememberState(false);
            auto* tri = &RememberState(m3::ToggleableState::Indeterminate);
            m3::Text("Checkboxes");
            FlowRow({}, Arrangement::SpacedBy(16_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                m3::Checkbox(checked->Get(), [checked](bool value) { checked->Set(value); }, named("Checkbox"));
                m3::TriStateCheckbox(tri->Get(), [tri] {
                    tri->Set(tri->Peek() == m3::ToggleableState::Indeterminate ? m3::ToggleableState::On :
                             tri->Peek() == m3::ToggleableState::On ? m3::ToggleableState::Off : m3::ToggleableState::Indeterminate);
                }, named("Tri-state checkbox"));
                m3::Checkbox(true, [](bool) {}, named("Disabled checked checkbox"), { .enabled = false });
                m3::Checkbox(false, [](bool) {}, named("Disabled unchecked checkbox"), { .enabled = false });
            });
            m3::Text("Radio buttons");
            auto* selected = &RememberState(0);
            FlowRow(SelectableGroup({}), Arrangement::SpacedBy(16_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                m3::RadioButton(selected->Get() == 0, [selected] { selected->Set(0); }, named("Radio first"));
                m3::RadioButton(selected->Get() == 1, [selected] { selected->Set(1); }, named("Radio second"));
                m3::RadioButton(true, [] {}, named("Disabled selected radio"), { .enabled = false });
                m3::RadioButton(false, [] {}, named("Disabled unselected radio"), { .enabled = false });
            });
            m3::Text("Switches");
            auto* on = &RememberState(false);
            FlowRow({}, Arrangement::SpacedBy(16_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                auto change = [on](bool value) { on->Set(value); };
                m3::Switch(on->Get(), change, named("Switch"));
                m3::SwitchOptions icon;
                icon.thumbContent = [on] {
                    m3::Icon(on->Get() ? m3::internal::Icons::Filled::Check() : m3::internal::Icons::Filled::Close(), std::nullopt,
                             SizeMod({}, m3::SwitchDefaults::IconSize, m3::SwitchDefaults::IconSize));
                };
                m3::Switch(on->Get(), change, named("Icon switch"), icon);
                m3::Switch(true, [](bool) {}, named("Disabled checked switch"), { .enabled = false });
                m3::Switch(false, [](bool) {}, named("Disabled unchecked switch"), { .enabled = false });
            });
            m3::Text("Decorative controls");
            Row({}, Arrangement::SpacedBy(24_dp), AlignmentV::CenterVertically(), [] {
                m3::Checkbox(true, {});
                m3::RadioButton(true, {});
                m3::Switch(true, {});
            });
        }

        void FabMenus() {
            auto* expanded = &RememberState(false);
            auto* selected = &RememberState(std::string("No menu action"));
            m3::FloatingActionButtonMenu(expanded->Get(), [=] {
                m3::ToggleFloatingActionButton(expanded->Get(), [expanded](bool value) { expanded->Set(value); }, {}, {}, [](const m3::ToggleFloatingActionButtonScope& scope) {
                    m3::Icon(scope.checkedProgress < 0.5f ? m3::internal::Icons::Filled::Edit() : m3::internal::Icons::Filled::Close(),
                             std::string("Toggle FAB menu"), m3::ToggleFloatingActionButtonDefaults::AnimateIcon({}, scope.checkedProgress));
                });
            }, SizeIn({}, Dp::Unspecified(), Dp::Unspecified(), Dp::Unspecified(), 320_dp), AlignmentH::End(), [=](const m3::FloatingActionButtonMenuScope& scope) {
                const char* labels[] = { "Edit draft", "Schedule", "Choose date", "Keyboard" };
                for (const auto* label : labels) m3::FloatingActionButtonMenuItem(scope, [=] {
                    selected->Set(label);
                    expanded->Set(false);
                }, [label] { m3::Text(label); }, [] { m3::Icon(m3::internal::Icons::Filled::Check(), std::nullopt); });
            });
            m3::Text(selected->Get());
            auto* checked = &RememberState(false);
            FlowRow({}, Arrangement::SpacedBy(16_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [checked] {
                m3::ToggleFloatingActionButtonOptions medium;
                medium.containerSize = m3::ToggleFloatingActionButtonDefaults::containerSizeMedium();
                medium.containerCornerRadius = m3::ToggleFloatingActionButtonDefaults::containerCornerRadiusMedium();
                m3::ToggleFloatingActionButton(checked->Get(), [checked](bool value) { checked->Set(value); }, {}, medium, [](const m3::ToggleFloatingActionButtonScope& scope) {
                    m3::Icon(scope.checkedProgress < 0.5f ? m3::internal::Icons::Filled::Edit() : m3::internal::Icons::Filled::Close(), std::string("Medium toggle FAB"),
                             m3::ToggleFloatingActionButtonDefaults::AnimateIcon({}, scope.checkedProgress, {}, m3::ToggleFloatingActionButtonDefaults::iconSizeMedium()));
                });
                m3::ToggleFloatingActionButtonOptions large;
                large.containerSize = m3::ToggleFloatingActionButtonDefaults::containerSizeLarge();
                large.containerCornerRadius = m3::ToggleFloatingActionButtonDefaults::containerCornerRadiusLarge();
                m3::ToggleFloatingActionButton(checked->Get(), [checked](bool value) { checked->Set(value); }, {}, large, [](const m3::ToggleFloatingActionButtonScope& scope) {
                    m3::Icon(scope.checkedProgress < 0.5f ? m3::internal::Icons::Filled::Edit() : m3::internal::Icons::Filled::Close(), std::string("Large toggle FAB"),
                             m3::ToggleFloatingActionButtonDefaults::AnimateIcon({}, scope.checkedProgress, {}, m3::ToggleFloatingActionButtonDefaults::iconSizeLarge()));
                });
            });
        }

        void FloatingActionButtons() {
            auto* expanded = &RememberState(true);
            auto* visible = &RememberState(true);
            auto* clicks = &RememberState(0);
            auto click = [clicks] { clicks->Set(clicks->Peek() + 1); };
            auto icon = [](const char* description, Dp size = 24_dp) {
                return [description, size] { m3::Icon(m3::internal::Icons::Filled::Check(), std::string(description), SizeMod({}, size, size)); };
            };
            FlowRow({}, Arrangement::SpacedBy(16_dp), Arrangement::SpacedBy(16_dp), AlignmentV::CenterVertically(), [=] {
                m3::SmallFloatingActionButton(click, {}, {}, icon("Small FAB"));
                m3::FloatingActionButton(click, {}, {}, icon("Standard FAB"));
                m3::MediumFloatingActionButton(click, {}, {}, icon("Medium FAB", m3::FloatingActionButtonDefaults::MediumIconSize));
                m3::LargeFloatingActionButton(click, {}, {}, icon("Large FAB", m3::FloatingActionButtonDefaults::LargeIconSize));
            });
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                m3::ExtendedFloatingActionButton(click, {}, {}, [] { m3::Text("Create"); });
                m3::SmallExtendedFloatingActionButton(click, {}, {}, [] { m3::Text("Small"); });
                m3::MediumExtendedFloatingActionButton(click, {}, {}, [] { m3::Text("Medium"); });
                m3::LargeExtendedFloatingActionButton(click, {}, {}, [] { m3::Text("Large"); });
            });
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                m3::ExtendedFloatingActionButton([] { m3::Text("Create"); }, icon("Expandable standard FAB"), click, {}, expanded->Get());
                m3::SmallExtendedFloatingActionButton([] { m3::Text("Small"); }, icon("Expandable small FAB"), click, {}, expanded->Get());
                m3::MediumExtendedFloatingActionButton([] { m3::Text("Medium"); }, icon("Expandable medium FAB", m3::FloatingActionButtonDefaults::MediumIconSize), click, {}, expanded->Get());
                m3::LargeExtendedFloatingActionButton([] { m3::Text("Large"); }, icon("Expandable large FAB", m3::FloatingActionButtonDefaults::LargeIconSize), click, {}, expanded->Get());
            });
            m3::TextButton([expanded] { expanded->Set(!expanded->Peek()); }, {}, {}, [expanded] { m3::Text(expanded->Get() ? "Collapse FAB labels" : "Expand FAB labels"); });
            m3::TextButton([visible] { visible->Set(!visible->Peek()); }, {}, {}, [visible] { m3::Text(visible->Get() ? "Hide animated FAB" : "Show animated FAB"); });
            m3::FloatingActionButton(click, m3::AnimateFloatingActionButton({}, visible->Get(), Alignment::BottomEnd()), {}, icon("Animated FAB"));
            m3::Text("FAB clicks: " + std::to_string(clicks->Get()));
        }

        void IconButtons() {
            auto& checked = RememberState(false);
            auto* on = &checked;
            auto icon = [](const char* description) {
                return [description] { m3::Icon(m3::internal::Icons::Filled::Check(), std::string(description)); };
            };
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [on, icon] {
                auto change = [on](bool value) { on->Set(value); };
                m3::IconButton([] {}, {}, {}, icon("Standard icon button"));
                m3::FilledIconButton([] {}, {}, {}, icon("Filled icon button"));
                m3::FilledTonalIconButton([] {}, {}, {}, icon("Tonal icon button"));
                m3::OutlinedIconButton([] {}, {}, {}, icon("Outlined icon button"));
                m3::IconToggleButtonOptions toggle;
                toggle.shapes = m3::IconButtonDefaults::toggleableShapes();
                m3::IconToggleButton(on->Get(), change, {}, toggle, icon("Standard icon toggle"));
                m3::FilledIconToggleButton(on->Get(), change, {}, toggle, icon("Filled icon toggle"));
                m3::FilledTonalIconToggleButton(on->Get(), change, {}, toggle, icon("Tonal icon toggle"));
                m3::OutlinedIconToggleButton(on->Get(), change, {}, toggle, icon("Outlined icon toggle"));
                m3::IconButtonOptions disabled;
                disabled.enabled = false;
                m3::FilledIconButton([] {}, {}, disabled, icon("Disabled icon button"));
            });
            m3::Text(checked.Get() ? "Icon toggles: on" : "Icon toggles: off");
        }
    }

    void MaterialComponents() {
        auto& theme = RememberState(0);
        const bool dark = IsSystemInDarkTheme();
        m3::MaterialThemeArgs args;
        if (theme.Get() == 1) args.colorScheme = m3::DarkColorScheme();
        else if (theme.Get() == 2) {
            auto dynamic = dark ? m3::DynamicDarkColorScheme() : m3::DynamicLightColorScheme();
            args.colorScheme = dynamic.value_or(dark ? m3::DarkColorScheme() : m3::LightColorScheme());
        } else args.colorScheme = m3::LightColorScheme();
        auto* selection = &theme;
        m3::MaterialTheme(args, [selection] {
            m3::Surface(FillMax({}, Axis::Horizontal), {}, [selection] {
                Column(Padding({}, 16_dp), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [selection] {
                    FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [selection] {
                        m3::TextButton([selection] { selection->Set(0); }, {}, {}, [] { m3::Text("Light"); });
                        m3::TextButton([selection] { selection->Set(1); }, {}, {}, [] { m3::Text("Dark"); });
                        m3::TextButton([selection] { selection->Set(2); }, {}, {}, [] { m3::Text("Dynamic"); });
                    });
                    m3::Text("Progress indicators");
                    ProgressIndicators();
                    m3::HorizontalDivider();
                    m3::Text("Lists");
                    Lists();
                    m3::HorizontalDivider();
                    m3::Text("Chips");
                    Chips();
                    m3::HorizontalDivider();
                    m3::Text("Cards");
                    Cards();
                    m3::HorizontalDivider();
                    SelectionControls();
                    m3::HorizontalDivider();
                    m3::Text("FAB menus");
                    FabMenus();
                    m3::HorizontalDivider();
                    m3::Text("Floating action buttons");
                    FloatingActionButtons();
                    m3::HorizontalDivider();
                    m3::Text("Toggle buttons");
                    ToggleButtons();
                    m3::HorizontalDivider();
                    m3::Text("Material buttons");
                    Buttons();
                    m3::HorizontalDivider();
                    m3::Text("Icon buttons");
                    IconButtons();
                });
            });
        });
    }

}
