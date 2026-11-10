#include "MaterialComponents.h"

#include <cstdlib>
#include <vector>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Flow.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/material3/Checkbox.h>
#include <kiln/material3/Card.h>
#include <kiln/material3/Chip.h>
#include <kiln/material3/ListItem.h>
#include <kiln/material3/Slider.h>
#include <kiln/material3/SegmentedButton.h>
#include <kiln/material3/SplitButton.h>
#include <kiln/material3/Tooltip.h>
#include <kiln/material3/Menu.h>
#include <kiln/material3/ButtonGroup.h>
#include <kiln/material3/NavigationBar.h>
#include <kiln/material3/NavigationRail.h>
#include <kiln/material3/ShortNavigationBar.h>
#include <kiln/material3/Tab.h>
#include <kiln/material3/TabRow.h>
#include <kiln/material3/WideNavigationRail.h>
#include <kiln/material3/Scaffold.h>
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

        void SegmentedButtons() {
            auto* choice = &RememberState(0);
            auto* checked = &RememberState(1);
            auto* enabled = &RememberState(true);
            m3::Text("Single choice");
            m3::SingleChoiceSegmentedButtonRow({}, [=] {
                const char* labels[] = { "Day", "Week", "Month" };
                for (int i = 0; i < 3; ++i) m3::SegmentedButton(choice->Get() == i, [=] { choice->Set(i); },
                    m3::SegmentedButtonDefaults::itemShape(i, 3), [=] { m3::Text(labels[i]); }, {}, { .enabled = enabled->Get() });
            });
            m3::Text("Multiple choice");
            m3::MultiChoiceSegmentedButtonRow({}, [=] {
                const char* labels[] = { "Walk", "Bike", "Bus" };
                for (int i = 0; i < 3; ++i) m3::SegmentedButton(bool(checked->Get() & (1 << i)), [=](bool value) {
                    checked->Set(value ? checked->Peek() | (1 << i) : checked->Peek() & ~(1 << i));
                }, m3::SegmentedButtonDefaults::itemShape(i, 3), [=] { m3::Text(labels[i]); }, {}, { .enabled = enabled->Get() });
            });
            m3::Text("Custom icons");
            m3::SingleChoiceSegmentedButtonRow({}, [=] {
                for (int i = 0; i < 2; ++i) {
                    m3::SegmentedButtonOptions o; o.enabled = enabled->Get();
                    o.icon = std::function<void()>([=] {
                        m3::SegmentedButtonDefaults::Icon(choice->Get() == i, m3::SegmentedButtonDefaults::ActiveIcon,
                            [] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt, SizeMod({}, 18_dp, 18_dp)); });
                    });
                    m3::SegmentedButton(choice->Get() == i, [=] { choice->Set(i); }, m3::SegmentedButtonDefaults::itemShape(i, 2),
                        [=] { m3::Text(i == 0 ? "Draft" : "Final"); }, {}, o);
                }
            });
            m3::Text("Label only");
            m3::SingleChoiceSegmentedButtonRow({}, [=] {
                for (int i = 0; i < 2; ++i) {
                    m3::SegmentedButtonOptions o; o.enabled = enabled->Get(); o.icon = std::function<void()>{};
                    m3::SegmentedButton(choice->Get() == i, [=] { choice->Set(i); }, m3::SegmentedButtonDefaults::itemShape(i, 2),
                        [=] { m3::Text(i == 0 ? "Open" : "Closed"); }, {}, o);
                }
            });
            m3::TextButton([=] { enabled->Set(!enabled->Peek()); }, {}, {}, [=] { m3::Text(enabled->Get() ? "Disable segments" : "Enable segments"); });
            m3::Text("Single selection: " + std::to_string(choice->Get()));
            m3::Text("Multiple selection: " + std::to_string(checked->Get()));
        }

        void SplitButtons() {
            namespace sb = m3::SplitButtonDefaults;
            auto* open = &RememberState(0);
            auto* clicks = &RememberState(0);
            auto* enabled = &RememberState(true);
            const char* names[] = { "Filled", "Tonal", "Outlined", "Elevated" };
            FlowRow({}, Arrangement::SpacedBy(12_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                for (int i = 0; i < 4; ++i) {
                    const m3::SplitButtonOptions o{ .enabled = enabled->Get() };
                    const bool checked = open->Get() & (1 << i);
                    const auto click = [=] { clicks->Set(clicks->Peek() + 1); };
                    const auto toggle = [=](bool value) { open->Set(value ? open->Peek() | (1 << i) : open->Peek() & ~(1 << i)); };
                    const auto label = [=] {
                        m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt, SizeMod({}, sb::LeadingIconSize, sb::LeadingIconSize));
                        Spacer(Width({}, m3::ButtonDefaults::IconSpacing));
                        m3::Text(names[i]);
                    };
                    const auto arrow = [=] {
                        const float turn = AnimateFloatAsState(checked ? 180.0f : 0.0f);
                        GraphicsLayerParams layer; layer.rotationZ = turn;
                        m3::Icon(m3::internal::Icons::Filled::ArrowDropDown(), std::string(names[i]) + " options",
                                 GraphicsLayer(SizeMod({}, sb::TrailingIconSize, sb::TrailingIconSize), layer));
                    };
                    m3::SplitButtonLayout([=] {
                        if (i == 0) sb::LeadingButton(click, {}, o, label);
                        else if (i == 1) sb::TonalLeadingButton(click, {}, o, label);
                        else if (i == 2) sb::OutlinedLeadingButton(click, {}, o, label);
                        else sb::ElevatedLeadingButton(click, {}, o, label);
                    }, [=] {
                        if (i == 0) sb::TrailingButton(checked, toggle, {}, o, arrow);
                        else if (i == 1) sb::TonalTrailingButton(checked, toggle, {}, o, arrow);
                        else if (i == 2) sb::OutlinedTrailingButton(checked, toggle, {}, o, arrow);
                        else sb::ElevatedTrailingButton(checked, toggle, {}, o, arrow);
                    });
                }
            });
            m3::TextButton([=] { enabled->Set(!enabled->Peek()); }, {}, {}, [=] { m3::Text(enabled->Get() ? "Disable split buttons" : "Enable split buttons"); });
            m3::Text("Split clicks: " + std::to_string(clicks->Get()));
            m3::Text("Split open: " + std::to_string(open->Get()));
        }

        void Tooltips() {
            auto& c = *rt::Composition::Current();
            auto* actions = &RememberState(0);
            auto* job = &c.Remember<std::shared_ptr<rt::Job>>([] { return std::make_shared<rt::Job>(); });
            const auto plain = m3::RememberTooltipState();
            const auto below = m3::RememberTooltipState(false, true);
            const auto rich = m3::RememberTooltipState(false, true);
            const auto icon = [](const char* description) {
                return [description] { m3::Icon(m3::internal::Icons::Filled::Check(), std::string(description)); };
            };
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                m3::TooltipBox(
                    m3::TooltipDefaults::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Above),
                    [](const m3::TooltipScope& scope) {
                        m3::PlainTooltip(scope, {}, { .caretShape = m3::TooltipDefaults::caretShape() }, [] { m3::Text("Add to favorites"); });
                    },
                    plain, {}, {}, [=] { m3::IconButton([] {}, {}, {}, icon("Plain tooltip anchor")); });
                m3::TooltipBox(
                    m3::TooltipDefaults::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Below),
                    [](const m3::TooltipScope& scope) { m3::PlainTooltip(scope, {}, {}, [] { m3::Text("Shown below"); }); },
                    below, {}, {}, [=] { m3::OutlinedIconButton([] {}, {}, {}, icon("Below tooltip anchor")); });
                m3::TooltipBox(
                    m3::TooltipDefaults::rememberTooltipPositionProvider(m3::TooltipAnchorPosition::Above),
                    [=](const m3::TooltipScope& scope) {
                        m3::RichTooltipOptions o;
                        o.title = [] { m3::Text("Camera settings"); };
                        o.action = [=] {
                            m3::TextButton([=] { actions->Set(actions->Peek() + 1); rich.Dismiss(); }, {}, {}, [] { m3::Text("Learn more"); });
                        };
                        o.caretShape = m3::TooltipDefaults::caretShape();
                        m3::RichTooltip(scope, {}, o, [] { m3::Text("Rich tooltips explain a control and can offer one action."); });
                    },
                    rich, {}, { .hasAction = true }, [=] { m3::FilledTonalIconButton([] {}, {}, {}, icon("Rich tooltip anchor")); });
                m3::TextButton([=] { **job = rt::Launch(rt::Dispatcher::Main(), plain.Show()); }, {}, {}, [] { m3::Text("Show plain tooltip"); });
            });
            m3::Text("Tooltip actions: " + std::to_string(actions->Get()));
            m3::Text(std::string("Tooltips shown: ") + (plain.IsVisible() ? "plain " : "") + (below.IsVisible() ? "below " : "") +
                     (rich.IsVisible() ? "rich" : ""));
        }

        void Menus() {
            namespace Icons = m3::internal::Icons;
            namespace md = m3::MenuDefaults;
            auto* classic = &RememberState(false);
            auto* expressive = &RememberState(false);
            auto* choice = &RememberState(std::string("none"));
            auto* styles = &RememberState(0);
            auto* align = &RememberState(0);
            const auto icon = [](const Icons::ImageVectorPtr& v) { return [&v] { m3::Icon(v, std::nullopt); }; };
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [=] {
                Box({}, Alignment::TopStart(), false, [=] {
                    m3::OutlinedButton([=] { classic->Set(true); }, {}, {}, [] { m3::Text("Classic menu"); });
                    m3::DropdownMenu(classic->Get(), [=] { classic->Set(false); }, {}, {}, [=] {
                        const auto pick = [=](const char* name) { return [=] { choice->Set(name); classic->Set(false); }; };
                        m3::DropdownMenuItem([] { m3::Text("Edit"); }, pick("Edit"), {}, { .leadingIcon = icon(Icons::Filled::Edit()) });
                        m3::DropdownMenuItem([] { m3::Text("Schedule"); }, pick("Schedule"), {},
                                             { .leadingIcon = icon(Icons::Filled::Schedule()), .trailingIcon = [] { m3::Text("Ctrl+S"); } });
                        m3::HorizontalDivider();
                        m3::DropdownMenuItem([] { m3::Text("Archive"); }, pick("Archive"), {}, { .enabled = false });
                    });
                });
                Box({}, Alignment::TopStart(), false, [=] {
                    m3::OutlinedButton([=] { expressive->Set(true); }, {}, {}, [] { m3::Text("Expressive menu"); });
                    m3::DropdownMenuPopup(expressive->Get(), [=] { expressive->Set(false); }, {},
                                          md::rememberDropdownMenuPopupPositionProvider(m3::MenuAnchorPosition::Below()), [=] {
                        const char* names[] = { "Bold", "Italic" };
                        m3::DropdownMenuGroup(md::groupShape(0, 2), {}, {}, [=] {
                            for (int i = 0; i < 2; ++i) {
                                const bool on = styles->Get() & (1 << i);
                                m3::CheckableDropdownMenuItem(on, [=](bool v) { styles->Set(v ? styles->Peek() | (1 << i) : styles->Peek() & ~(1 << i)); },
                                                              [=] { m3::Text(names[i]); }, md::itemShape(i, 2), {},
                                                              { .selectedLeadingIcon = icon(Icons::Filled::Check()) });
                            }
                        });
                        Spacer(Height({}, md::GroupSpacing));
                        const char* sides[] = { "Left", "Right" };
                        m3::DropdownMenuGroup(md::groupShape(1, 2), {}, {}, [=] {
                            for (int i = 0; i < 2; ++i) {
                                m3::SelectableDropdownMenuItem(align->Get() == i, [=] { align->Set(i); }, [=] { m3::Text(sides[i]); }, md::itemShape(i, 2), {},
                                                               { .selectedLeadingIcon = icon(Icons::Filled::Check()) });
                            }
                        });
                    });
                });
            });
            m3::Text("Menu choice: " + choice->Get());
            m3::Text("Menu styles: " + std::to_string(styles->Get()));
            m3::Text(std::string("Menu align: ") + (align->Get() == 0 ? "left" : "right"));
        }

        void ButtonGroups() {
            namespace bg = m3::ButtonGroupDefaults;
            auto* clicks = &RememberState(0);
            auto* styles = &RememberState(0);
            auto* size = &RememberState(0);
            m3::ButtonGroup([](m3::ButtonGroupMenuState& menu) { bg::OverflowIndicator(menu); }, FillMax({}, Axis::Horizontal),
                            [=](m3::ButtonGroupScope& scope) {
                const char* toggles[] = { "Mute", "Pin", "Star" };
                for (int i = 0; i < 3; ++i) {
                    scope.ToggleableItem(styles->Get() & (1 << i), toggles[i],
                                         [=](bool v) { styles->Set(v ? styles->Peek() | (1 << i) : styles->Peek() & ~(1 << i)); });
                }
                const char* actions[] = { "Share", "Print", "Copy" };
                for (const char* action : actions) scope.ClickableItem([=] { clicks->Set(clicks->Peek() + 1); }, action);
            });
            Row(FillMax({}, Axis::Horizontal), Arrangement::SpacedBy(bg::ConnectedSpaceBetween), AlignmentV::CenterVertically(), [=] {
                const char* sizes[] = { "Small", "Medium", "Large" };
                for (int i = 0; i < 3; ++i) {
                    m3::ToggleButtonOptions o;
                    o.shapes = i == 0 ? bg::connectedLeadingButtonShapes() : i == 2 ? bg::connectedTrailingButtonShapes() : bg::connectedMiddleButtonShapes();
                    const Modifier m = Semantics(Weight({}, 1), [](SemanticsConfiguration& c) { c.role = Role::RadioButton; });
                    m3::ToggleButton(size->Get() == i, [=](bool) { size->Set(i); }, m, o, [=] { m3::Text(sizes[i]); });
                }
            });
            m3::Text("Group clicks: " + std::to_string(clicks->Get()));
            m3::Text("Group styles: " + std::to_string(styles->Get()));
            m3::Text("Connected size: " + std::to_string(size->Get()));
        }

        void Navigation() {
            auto* tab = &RememberState(0);
            auto* labelled = &RememberState(0);
            auto* fab = &RememberState(2);
            const char* tabs[] = { "Home", "Search", "Library" };
            const char* positions[] = { "Start", "Center", "End", "EndOverlay" };
            const auto navIcon = [] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt); };
            m3::ScaffoldOptions o;
            o.floatingActionButtonPosition = m3::FabPosition(fab->Get());
            o.contentWindowInsets = WindowInsets{};
            o.topBar = [] {
                m3::Surface(FillMax({}, Axis::Horizontal), { .color = m3::MaterialTheme::colorScheme().primaryContainer }, [] {
                    Box(Padding(Height(FillMax({}, Axis::Horizontal), 64_dp), 16_dp), Alignment::CenterStart(), false, [] { m3::Text("Top bar"); });
                });
            };
            o.bottomBar = [=] {
                m3::NavigationBarOptions bar;
                bar.windowInsets = WindowInsets{};
                m3::NavigationBar({}, bar, [=] {
                    for (int i = 0; i < 3; ++i)
                        m3::NavigationBarItem(tab->Get() == i, [=] { tab->Set(i); }, navIcon, {}, { .label = [=] { m3::Text(tabs[i]); } });
                });
            };
            o.floatingActionButton = [] {
                m3::FloatingActionButton([] {}, {}, {}, [] { m3::Icon(m3::internal::Icons::Filled::Edit(), "Compose"); });
            };
            m3::Scaffold(Height(FillMax({}, Axis::Horizontal), 360_dp), o, [=](const PaddingValues& padding) {
                Box(Padding(FillMax({}, Axis::Both), padding), Alignment::Center(), false, [=] { m3::Text(std::string("Showing: ") + tabs[tab->Get()]); });
            });
            m3::TextButton([fab] { fab->Set((fab->Peek() + 1) % 4); }, {}, {}, [=] { m3::Text(std::string("FAB: ") + positions[fab->Get()]); });
            m3::Text("Labels when selected");
            const char* mail[] = { "Mail", "Chat", "Rooms", "Meet" };
            m3::NavigationBarOptions bar;
            bar.windowInsets = WindowInsets{};
            m3::NavigationBar({}, bar, [=] {
                for (int i = 0; i < 4; ++i)
                    m3::NavigationBarItem(labelled->Get() == i, [=] { labelled->Set(i); },
                                          [=] { m3::Icon(m3::internal::Icons::Filled::Edit(), mail[i]); }, {},
                                          { .label = [=] { m3::Text(mail[i]); }, .alwaysShowLabel = false });
            });
            m3::Text(std::string("Selected: ") + mail[labelled->Get()]);

            auto* shortItem = &RememberState(0);
            const char* shortTabs[] = { "Feed", "Saved", "Shop", "You" };
            for (auto arrangement : { m3::ShortNavigationBarArrangement::EqualWeight, m3::ShortNavigationBarArrangement::Centered }) {
                const bool centered = arrangement == m3::ShortNavigationBarArrangement::Centered;
                m3::Text(centered ? "Short bar, centered" : "Short bar");
                m3::ShortNavigationBarOptions options;
                options.windowInsets = WindowInsets{};
                options.arrangement = arrangement;
                m3::ShortNavigationBar({}, options, [=] {
                    for (int i = 0; i < (centered ? 3 : 4); ++i)
                        m3::ShortNavigationBarItem(shortItem->Get() == i, [=] { shortItem->Set(i); }, navIcon, [=] { m3::Text(shortTabs[i]); }, {},
                                                   { .iconPosition = centered ? m3::NavigationItemIconPosition::Start : m3::NavigationItemIconPosition::Top });
                });
            }
            m3::Text(std::string("Short: ") + shortTabs[shortItem->Get()]);

            auto* wideItem = &RememberState(0);
            const auto wideState = m3::RememberWideNavigationRailState();
            auto& toggleJob = rt::Composition::Current()->Remember<std::shared_ptr<rt::Job>>([] { return std::make_shared<rt::Job>(); });
            const auto jobHolder = toggleJob;
            const bool wideExpanded = wideState->TargetValue() == m3::WideNavigationRailValue::Expanded;
            const char* wide[] = { "Home", "Photos", "Shared", "Trash" };
            Row(Height({}, 400_dp), Arrangement::Start(), AlignmentV::Top(), [=] {
                m3::WideNavigationRailOptions options;
                options.state = wideState;
                options.windowInsets = WindowInsets{};
                options.header = [=] {
                    m3::TextButton([=] { *jobHolder = rt::Launch(rt::Dispatcher::Main(), wideState->Toggle()); }, {}, {},
                                   [=] { m3::Text(wideExpanded ? "Collapse rail" : "Expand rail"); });
                };
                m3::WideNavigationRail({}, options, [=] {
                    for (int i = 0; i < 4; ++i)
                        m3::WideNavigationRailItem(wideItem->Get() == i, [=] { wideItem->Set(i); }, navIcon, [=] { m3::Text(wide[i]); }, wideExpanded, {}, {});
                });
                Box(Padding({}, 16_dp), Alignment::TopStart(), false, [=] { m3::Text(std::string("Wide: ") + wide[wideItem->Get()]); });
            });

            auto* modalItem = &RememberState(0);
            const auto modalState = m3::RememberWideNavigationRailState();
            auto& modalJob = rt::Composition::Current()->Remember<std::shared_ptr<rt::Job>>([] { return std::make_shared<rt::Job>(); });
            const auto modalJobHolder = modalJob;
            const bool modalExpanded = modalState->TargetValue() == m3::WideNavigationRailValue::Expanded;
            const char* modal[] = { "Mail", "Notes", "Tasks" };
            Row(Height({}, 360_dp), Arrangement::Start(), AlignmentV::Top(), [=] {
                m3::ModalWideNavigationRailOptions options;
                options.state = modalState;
                options.windowInsets = WindowInsets{};
                options.header = [=] {
                    m3::TextButton([=] { *modalJobHolder = rt::Launch(rt::Dispatcher::Main(), modalState->Toggle()); }, {}, {},
                                   [=] { m3::Text(modalExpanded ? "Close modal rail" : "Open modal rail"); });
                };
                m3::ModalWideNavigationRail({}, options, [=] {
                    for (int i = 0; i < 3; ++i)
                        m3::WideNavigationRailItem(modalItem->Get() == i, [=] { modalItem->Set(i); }, navIcon, [=] { m3::Text(modal[i]); }, modalExpanded, {}, {});
                });
                Box(Padding({}, 16_dp), Alignment::TopStart(), false, [=] { m3::Text(std::string("Modal: ") + modal[modalItem->Get()]); });
            });

            auto* railItem = &RememberState(0);
            auto* railLabels = &RememberState(true);
            const char* rail[] = { "Inbox", "Starred", "Sent", "Drafts" };
            Row(Height({}, 360_dp), Arrangement::Start(), AlignmentV::Top(), [=] {
                m3::NavigationRailOptions options;
                options.windowInsets = WindowInsets{};
                options.header = [] {
                    m3::FloatingActionButton([] {}, {}, {}, [] { m3::Icon(m3::internal::Icons::Filled::Edit(), "Compose"); });
                };
                m3::NavigationRail({}, options, [=] {
                    for (int i = 0; i < 4; ++i)
                        m3::NavigationRailItem(railItem->Get() == i, [=] { railItem->Set(i); }, [=] { m3::Icon(m3::internal::Icons::Filled::Edit(), rail[i]); },
                                               {}, { .label = [=] { m3::Text(rail[i]); }, .alwaysShowLabel = railLabels->Get() });
                });
                Column(Padding({}, 16_dp), [=] {
                    m3::Text(std::string("Rail: ") + rail[railItem->Get()]);
                    m3::TextButton([railLabels] { railLabels->Set(!railLabels->Peek()); }, {}, {},
                                   [railLabels] { m3::Text(railLabels->Get() ? "Rail labels: always" : "Rail labels: selected"); });
                });
            });
        }

        void Tabs() {
            auto* primary = &RememberState(0);
            auto* secondary = &RememberState(0);
            auto* scrolling = &RememberState(0);
            const char* titles[] = { "Flights", "Trips", "Explore" };
            m3::Text("Primary tabs");
            m3::PrimaryTabRow(primary->Get(), {}, {}, [=] {
                for (int i = 0; i < 3; ++i) m3::Tab(primary->Get() == i, [=] { primary->Set(i); }, {}, [=] { m3::Text(titles[i]); }, nullptr);
            });
            m3::Text(std::string("Primary: ") + titles[primary->Get()]);
            m3::Text("Secondary tabs with icons");
            const char* sections[] = { "Video", "Photos", "Audio" };
            m3::SecondaryTabRow(secondary->Get(), {}, {}, [=] {
                for (int i = 0; i < 3; ++i)
                    m3::Tab(secondary->Get() == i, [=] { secondary->Set(i); }, {}, [=] { m3::Text(sections[i]); },
                            [] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt); });
            });
            m3::Text(std::string("Secondary: ") + sections[secondary->Get()]);
            m3::Text("Leading icon tabs");
            auto* leading = &RememberState(0);
            m3::SecondaryTabRow(leading->Get(), {}, {}, [=] {
                for (int i = 0; i < 2; ++i)
                    m3::LeadingIconTab(leading->Get() == i, [=] { leading->Set(i); }, [=] { m3::Text(i == 0 ? "Inbox" : "Sent"); },
                                       [] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt); }, {});
            });
            m3::Text("Scrollable tabs");
            m3::PrimaryScrollableTabRow(scrolling->Get(), {}, {}, [=] {
                for (int i = 0; i < 10; ++i)
                    m3::Tab(scrolling->Get() == i, [=] { scrolling->Set(i); }, {}, [=] { m3::Text("Topic " + std::to_string(i + 1)); }, nullptr);
            });
            m3::Text("Scrolled to: Topic " + std::to_string(scrolling->Get() + 1));
        }

        void Sliders() {
            auto* continuous = &m3::RememberSliderState(0.35f);
            auto* stepped = &m3::RememberSliderState(0.5f, 7);
            auto* centered = &m3::RememberSliderState(0, 0, { -1, 1 });
            auto* range = &m3::RememberRangeSliderState(0.25f, 0.75f, 3);
            auto* down = &m3::RememberSliderState(0.35f);
            auto* up = &m3::RememberSliderState(0.65f);
            auto* enabled = &RememberState(true);
            auto* finished = &RememberState(0);
            const auto done = [finished] { finished->Set(finished->Peek() + 1); };
            const auto named = [](const char* label) { return Semantics({}, [label](SemanticsConfiguration& c) { c.contentDescription = { label }; }); };
            m3::SliderOptions options;
            options.enabled = enabled->Get(); options.onValueChangeFinished = done;
            m3::Text("Continuous");
            m3::Slider(*continuous, [continuous](float value) { continuous->SetValue(value); }, FillMax(named("Continuous slider"), Axis::Horizontal), options);
            m3::Text("Stepped");
            m3::Slider(*stepped, [stepped](float value) { stepped->SetValue(value); }, FillMax(named("Stepped slider"), Axis::Horizontal), options);
            m3::Text("Centered");
            auto centeredOptions = options;
            centeredOptions.track = [enabled](m3::SliderState& state) { m3::SliderDefaults::CenteredTrack(state, {}, { .enabled = enabled->Get() }); };
            m3::Slider(*centered, [centered](float value) { centered->SetValue(value); }, FillMax(named("Centered slider"), Axis::Horizontal), centeredOptions);
            m3::Text("Range");
            m3::RangeSlider(*range, [range](m3::SliderRange value) { range->SetStartValue(value.start); range->SetEndValue(value.endInclusive); }, FillMax({}, Axis::Horizontal),
                { .enabled = enabled->Get(), .onValueChangeFinished = done });
            m3::Text("Vertical sliders");
            Row({}, Arrangement::SpacedBy(32_dp), AlignmentV::Top(), [=] {
                m3::VerticalSlider(*down, [down](float value) { down->SetValue(value); }, Height(named("Top to bottom slider"), 160_dp), options);
                m3::VerticalSlider(*up, [up](float value) { up->SetValue(value); }, Height(named("Bottom to top slider"), 160_dp), options, false);
            });
            m3::TextButton([enabled] { enabled->Set(!enabled->Peek()); }, {}, {}, [enabled] { m3::Text(enabled->Get() ? "Disable sliders" : "Enable sliders"); });
            m3::Text("Slider value: " + std::to_string(RoundJava(continuous->Value() * 100)) + "%");
            m3::Text("Range: " + std::to_string(RoundJava(range->StartValue() * 100)) + "-" + std::to_string(RoundJava(range->EndValue() * 100)) + "%");
            m3::Text("Slider changes finished: " + std::to_string(finished->Get()));
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

    namespace {
        struct Section { const char* key; const char* title; void (*content)(); };
        const std::vector<Section>& Sections() {
            static const std::vector<Section> sections{
                { "navigation", "Navigation bars and rails", Navigation }, { "tabs", "Tabs", Tabs }, { "segmented", "Segmented buttons", SegmentedButtons }, { "split", "Split buttons", SplitButtons },
                { "tooltips", "Tooltips", Tooltips }, { "menus", "Menus", Menus }, { "groups", "Button groups", ButtonGroups },
                { "sliders", "Sliders", Sliders }, { "progress", "Progress indicators", ProgressIndicators }, { "lists", "Lists", Lists },
                { "chips", "Chips", Chips }, { "cards", "Cards", Cards }, { "selection", "", SelectionControls },
                { "fabmenus", "FAB menus", FabMenus }, { "fabs", "Floating action buttons", FloatingActionButtons },
                { "toggles", "Toggle buttons", ToggleButtons }, { "buttons", "Material buttons", Buttons },
                { "iconbuttons", "Icon buttons", IconButtons },
            };
            return sections;
        }
    }

    std::string RequestedSection() {
#if defined(__ANDROID__)
        char value[PROP_VALUE_MAX] = {};
        __system_property_get("debug.kiln.gallery.section", value);
        return value;
#else
        const char* value = std::getenv("KILN_GALLERY_SECTION");
        return value ? value : "";
#endif
    }

    void MaterialComponents(const std::string& only) {
        auto& theme = RememberState(0);
        const bool dark = IsSystemInDarkTheme();
        m3::MaterialThemeArgs args;
        if (theme.Get() == 1) args.colorScheme = m3::DarkColorScheme();
        else if (theme.Get() == 2) {
            auto dynamic = dark ? m3::DynamicDarkColorScheme() : m3::DynamicLightColorScheme();
            args.colorScheme = dynamic.value_or(dark ? m3::DarkColorScheme() : m3::LightColorScheme());
        } else args.colorScheme = m3::LightColorScheme();
        auto* selection = &theme;
        m3::MaterialTheme(args, [selection, only] {
            m3::Surface(FillMax({}, Axis::Horizontal), {}, [selection, only] {
                Column(Padding({}, 16_dp), Arrangement::SpacedBy(8_dp), AlignmentH::Start(), [selection, only] {
                    FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [selection] {
                        m3::TextButton([selection] { selection->Set(0); }, {}, {}, [] { m3::Text("Light"); });
                        m3::TextButton([selection] { selection->Set(1); }, {}, {}, [] { m3::Text("Dark"); });
                        m3::TextButton([selection] { selection->Set(2); }, {}, {}, [] { m3::Text("Dynamic"); });
                    });
                    // The sections, or only the one named (tools/gallery-smoke tests one at a time).
                    bool first = true;
                    for (const auto& section : Sections()) {
                        if (!only.empty() && only != section.key) continue;
                        if (!first) m3::HorizontalDivider();
                        first = false;
                        if (*section.title) m3::Text(section.title);
                        section.content();
                    }
                });
            });
        });
    }

}
