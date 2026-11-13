#include "MaterialComponents.h"
#include <kiln/Function.h>

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
#include <kiln/foundation/AnimatedVisibility.h>
#include <kiln/foundation/Back.h>
#include <kiln/foundation/Shapes.h>
#include <kiln/foundation/SharedTransition.h>
#include <kiln/foundation/AndroidView.h>
#if defined(__ANDROID__)
#include <jni.h>
#endif
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
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/FloatingToolbar.h>
#include <kiln/material3/Scrollbar.h>
#include <kiln/material3/Snackbar.h>
#include <kiln/material3/TextField.h>
#include <kiln/material3/ExposedDropdownMenu.h>
#include <kiln/material3/AlertDialog.h>
#include <kiln/material3/BottomSheetScaffold.h>
#include <kiln/material3/ModalBottomSheet.h>
#include <kiln/material3/NavigationDrawer.h>
#include <kiln/material3/SearchBar.h>
#include <kiln/material3/SwipeToDismissBox.h>
#include <kiln/material3/PullToRefresh.h>
#include <kiln/material3/Carousel.h>
#include <kiln/material3/DatePicker.h>
#include <kiln/material3/DateRangePicker.h>
#include <kiln/material3/ScrollField.h>
#include <kiln/material3/TimePicker.h>
#include <kiln/material3/TimePickerDialog.h>
#include <kiln/text/DateFormat.h>
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
                    o.icon = kiln::Function<void()>([=] {
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
                    m3::SegmentedButtonOptions o; o.enabled = enabled->Get(); o.icon = kiln::Function<void()>{};
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

        void AppBars() {
            const auto icon = [](const char* description) {
                return [description] { m3::IconButton([] {}, {}, {}, [description] { m3::Icon(m3::internal::Icons::Filled::Edit(), description); }); };
            };
            m3::TopAppBarOptions small;
            small.navigationIcon = icon("Menu");
            small.actions = icon("Search");
            small.windowInsets = WindowInsets{};
            m3::TopAppBar([] { m3::Text("Small top app bar"); }, {}, small);
            m3::TopAppBarOptions centered = small;
            centered.subtitle = [] { m3::Text("With a subtitle"); };
            m3::CenterAlignedTopAppBar([] { m3::Text("Centered"); }, {}, centered);

            // A large bar collapsing with the list and a bottom bar hiding with it.
            auto& list = RememberLazyListState();
            const auto top = m3::TopAppBarDefaults::exitUntilCollapsedScrollBehavior();
            const auto bottom = m3::BottomAppBarDefaults::exitAlwaysScrollBehavior();
            // Reported as text so a test can read it.
            m3::Text("Top bar collapsed: " + std::to_string(RoundJava(top->State().CollapsedFraction() * 100)) + "%");
            m3::ScaffoldOptions o;
            o.contentWindowInsets = WindowInsets{};
            o.topBar = [=] {
                m3::TwoRowsTopAppBarOptions bar;
                bar.navigationIcon = icon("Back");
                bar.actions = icon("Archive");
                bar.windowInsets = WindowInsets{};
                bar.scrollBehavior = top;
                m3::LargeTopAppBar([] { m3::Text("Inbox"); }, {}, bar);
            };
            o.bottomBar = [=] {
                m3::BottomAppBarOptions bar;
                bar.windowInsets = WindowInsets{};
                bar.scrollBehavior = bottom;
                m3::BottomAppBar([=] { icon("Check")(); icon("Edit")(); }, {}, [] {
                    m3::FloatingActionButton([] {}, {}, { .containerColor = m3::BottomAppBarDefaults::bottomAppBarFabColor(),
                                                          .elevation = m3::FloatingActionButtonDefaults::bottomAppBarFabElevation() },
                                             [] { m3::Icon(m3::internal::Icons::Filled::Edit(), "Compose"); });
                }, bar);
            };
            Box(NestedScroll(NestedScroll(Height(FillMax({}, Axis::Horizontal), 420_dp), top->NestedScrollConnection()), bottom->NestedScrollConnection()),
                Alignment::TopStart(), false, [&] {
                    m3::Scaffold({}, o, [lp = &list](const PaddingValues& padding) {
                        auto& list = *lp;
                        LazyColumn(FillMax({}, Axis::Both), list, [](LazyListScope& s) {
                            s.Items(30, [](int i) { m3::Text("Message " + std::to_string(i + 1), Padding(FillMax({}, Axis::Horizontal), 16_dp)); });
                        }, { .contentPadding = padding });
                    });
                });
        }

        void FloatingToolbars() {
            const auto icon = [](const char* description) {
                m3::IconButton([] {}, {}, {}, [description] { m3::Icon(m3::internal::Icons::Filled::Edit(), description); });
            };
            // Expands as the list scrolls back, collapses as it scrolls on (Compose's
            // ExpandableHorizontalFloatingToolbarSample).
            auto* expanded = &RememberState(true);
            // Reported as text so a test can read it.
            m3::Text(std::string("Toolbar expanded: ") + (expanded->Get() ? "yes" : "no"));
            Box(Height(FillMax({}, Axis::Horizontal), 360_dp), Alignment::TopStart(), false, [=] {
                const auto nested = m3::FloatingToolbarDefaults::FloatingToolbarVerticalNestedScroll(
                    FillMax({}, Axis::Both), expanded->Get(), [=] { expanded->Set(true); }, [=] { expanded->Set(false); });
                LazyColumn(nested, RememberLazyListState(), [](LazyListScope& s) {
                    s.Items(30, [](int i) { m3::Text("Row " + std::to_string(i + 1), Padding(FillMax({}, Axis::Horizontal), 16_dp)); });
                });
                m3::FloatingToolbarOptions o;
                o.colors = m3::FloatingToolbarDefaults::vibrantFloatingToolbarColors();
                o.leadingContent = [=] { icon("Back"); };
                o.trailingContent = [=] { icon("More"); };
                m3::HorizontalFloatingToolbar(expanded->Get(), OffsetMod(AlignInBox({}, Alignment::BottomCenter()), 0_dp, Dp(-16)), o,
                                              [=] { icon("Share"); icon("Favorite"); });
            });

            // Beside a FAB, sliding off the bottom as the list scrolls on.
            const auto behavior = m3::FloatingToolbarDefaults::exitAlwaysScrollBehavior(m3::FloatingToolbarExitDirection::Bottom);
            auto* withFab = &RememberState(true);
            m3::Text(std::string("Toolbar with FAB expanded: ") + (withFab->Get() ? "yes" : "no"));
            Box(NestedScroll(Height(FillMax({}, Axis::Horizontal), 360_dp), behavior->NestedScrollConnection()), Alignment::TopStart(), false, [=] {
                LazyColumn(FillMax({}, Axis::Both), RememberLazyListState(), [](LazyListScope& s) {
                    s.Items(30, [](int i) { m3::Text("Note " + std::to_string(i + 1), Padding(FillMax({}, Axis::Horizontal), 16_dp)); });
                });
                m3::FloatingToolbarWithFabOptions o;
                o.scrollBehavior = behavior;
                m3::HorizontalFloatingToolbar(
                    withFab->Get(),
                    [=] {
                        m3::FloatingToolbarDefaults::StandardFloatingActionButton([=] { withFab->Set(!withFab->Get()); }, {}, {},
                                                                                [] { m3::Icon(m3::internal::Icons::Filled::Edit(), "Toggle toolbar"); });
                    },
                    OffsetMod(AlignInBox({}, Alignment::BottomEnd()), Dp(-16), Dp(-16)), o, [=] { icon("Archive"); icon("Label"); icon("Delete"); });
            });
        }

        void Scrollbars() {
            // A list whose scrollbar shows while it scrolls, then fades.
            m3::Text("Fading scrollbar");
            auto& list = RememberLazyListState();
            LazyColumn(m3::NonInteractiveScrollbar(Height(FillMax({}, Axis::Horizontal), 280_dp), *list.ScrollIndicator(), Orientation::Vertical),
                       list, [](LazyListScope& s) {
                           s.Items(50, [](int i) { m3::Text("Line " + std::to_string(i + 1), Padding(FillMax({}, Axis::Horizontal), 12_dp)); });
                       });
            // A scrolled column whose scrollbar stays, on a visible track.
            m3::Text("Always visible, with a track");
            auto& column = RememberScrollState();
            m3::NonInteractiveScrollbarOptions always;
            always.isFadeEnabled = false;
            always.trackColor = m3::MaterialTheme::colorScheme().surfaceContainerHighest;
            // The scrollbar goes outside the scroll, or it would be drawn along the scrolled content.
            Column(VerticalScroll(m3::NonInteractiveScrollbar(Height(FillMax({}, Axis::Horizontal), 200_dp), *column.ScrollIndicator(),
                                                              Orientation::Vertical, always), column),
                   Arrangement::Top(), AlignmentH::Start(), [] {
                       for (int i = 0; i < 20; ++i) m3::Text("Paragraph " + std::to_string(i + 1), Padding(FillMax({}, Axis::Horizontal), 12_dp));
                   });
            // Along the bottom of a row.
            m3::Text("Horizontal");
            auto& row = RememberScrollState();
            Row(HorizontalScroll(m3::NonInteractiveScrollbar(Height(FillMax({}, Axis::Horizontal), 64_dp), *row.ScrollIndicator(),
                                                             Orientation::Horizontal, always), row),
                Arrangement::Start(), AlignmentV::CenterVertically(), [] {
                    for (int i = 0; i < 20; ++i) m3::Text("Tag " + std::to_string(i + 1), Padding({}, 16_dp));
                });
        }

        rt::Task<> ShowAndReport(m3::SnackbarHostState* host, m3::SnackbarVisuals visuals, rt::State<std::string>* result) {
            const m3::SnackbarResult r = co_await host->ShowSnackbar(std::move(visuals));
            result->Set(std::string(r == m3::SnackbarResult::ActionPerformed ? "action" : "dismissed"));
        }

        void Snackbars() {
            auto& host = m3::RememberSnackbarHostState();
            auto* result = &RememberState(std::string("none"));
            auto jobs = rt::Composition::Current()->Remember<std::shared_ptr<std::vector<rt::Job>>>([] { return std::make_shared<std::vector<rt::Job>>(); });
            const auto show = [&host, result, jobs](m3::SnackbarVisuals v) {
                return [&host, result, jobs, v] { jobs->push_back(rt::Launch(rt::Dispatcher::Main(), ShowAndReport(&host, v, result))); };
            };
            // Reported as text so a test can read it.
            m3::Text("Snackbar result: " + result->Get());
            m3::TextButton(show({ "Message sent", std::nullopt, false, m3::SnackbarDuration::Short }), {}, {}, [] { m3::Text("Show message"); });
            m3::TextButton(show({ "Photo deleted", std::string("Undo"), false, m3::SnackbarDuration::Long }), {}, {}, [] { m3::Text("Show with action"); });
            m3::TextButton(show({ "Connection lost", std::nullopt, true, m3::SnackbarDuration::Indefinite }), {}, {}, [] { m3::Text("Show until closed"); });
            // A snackbar whose long action sits on its own line.
            m3::SnackbarOptions o;
            o.actionOnNewLine = true;
            o.action = [] {
                // The colour Compose suggests for a snackbar's own action.
                m3::ButtonOptions bo;
                bo.colors = m3::ButtonDefaults::textButtonColors().Copy(Color::Unspecified(), m3::MaterialTheme::colorScheme().inversePrimary);
                m3::TextButton([] {}, {}, bo, [] { m3::Text("Open the settings"); });
            };
            m3::Snackbar(Padding({}, 12_dp), o, [] { m3::Text("Sync is paused while offline"); });
            // Where the host shows them.
            Box(Height(FillMax({}, Axis::Horizontal), 160_dp), Alignment::BottomCenter(), false, [&host] { m3::SnackbarHost(host); });
        }

        void TextFields() {
            auto& name = foundation::RememberTextFieldState();
            auto& email = foundation::RememberTextFieldState("me@example");
            auto& price = foundation::RememberTextFieldState("12");
            auto& password = foundation::RememberTextFieldState("secret");
            const auto icon = [](const char* description) {
                return [description] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::string(description)); };
            };
            // A read-only field choosing from a menu.
            auto& flavour = foundation::RememberTextFieldState("Vanilla");
            auto* expanded = &RememberState(false);
            m3::Text("Flavour: " + flavour.TextUtf8());
            m3::ExposedDropdownMenuBox(expanded->Get(), [expanded](bool v) { expanded->Set(v); }, Padding({}, 8_dp),
                                       [&flavour, expanded](const m3::ExposedDropdownMenuBoxScope& scope) {
                m3::TextFieldOptions o;
                o.readOnly = true;
                o.lineLimits = foundation::TextFieldLineLimits::SingleLine();
                o.label = [](const m3::TextFieldLabelScope&) { m3::Text("Flavour"); };
                const bool open = expanded->Get();
                o.trailingIcon = [open] { m3::ExposedDropdownMenuDefaults::TrailingIcon(open); };
                o.colors = m3::ExposedDropdownMenuDefaults::textFieldColors();
                m3::TextField(flavour, scope.MenuAnchor({}, m3::ExposedDropdownMenuAnchorType::PrimaryNotEditable), o);
                m3::ExposedDropdownMenu(scope, open, [expanded] { expanded->Set(false); }, {}, {}, [&flavour, expanded] {
                    for (const char* option : { "Vanilla", "Chocolate", "Mint" }) {
                        m3::DropdownMenuItem([option] { m3::Text(option); }, [&flavour, expanded, option] {
                            flavour.SetTextAndPlaceCursorAtEnd(option);
                            expanded->Set(false);
                        }, {}, { .contentPadding = m3::ExposedDropdownMenuDefaults::ItemContentPadding() });
                    }
                });
            });
            // Reported as text so a test can read it.
            m3::Text("Name: " + (name.TextUtf8().empty() ? std::string("(empty)") : name.TextUtf8()));
            m3::TextFieldOptions filled;
            filled.label = [](const m3::TextFieldLabelScope&) { m3::Text("Name"); };
            filled.placeholder = [] { m3::Text("Your full name"); };
            filled.supportingText = [] { m3::Text("As on your ID"); };
            filled.lineLimits = foundation::TextFieldLineLimits::SingleLine();
            m3::TextField(name, Padding({}, 8_dp), filled);
            m3::TextFieldOptions outlined;
            outlined.label = [](const m3::TextFieldLabelScope&) { m3::Text("Email"); };
            outlined.leadingIcon = icon("Email icon");
            outlined.trailingIcon = icon("Clear email");
            outlined.isError = true;
            outlined.supportingText = [] { m3::Text("Enter a full address"); };
            m3::OutlinedTextField(email, Padding({}, 8_dp), outlined);
            m3::TextFieldOptions affixes;
            affixes.label = [](const m3::TextFieldLabelScope&) { m3::Text("Price"); };
            affixes.prefix = [] { m3::Text("$"); };
            affixes.suffix = [] { m3::Text(".00"); };
            m3::OutlinedTextField(price, Padding({}, 8_dp), affixes);
            m3::SecureTextFieldOptions secure;
            secure.label = [](const m3::TextFieldLabelScope&) { m3::Text("Password"); };
            m3::SecureTextField(password, Padding({}, 8_dp), secure);
        }

        void Dialogs() {
            auto* open = &RememberState(false);
            auto* result = &RememberState(std::string("none"));
            m3::Text("Dialog result: " + result->Get());
            m3::TextButton([open] { open->Set(true); }, {}, {}, [] { m3::Text("Show alert dialog"); });
            if (open->Get()) {
                m3::AlertDialogOptions o;
                o.icon = [] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt); };
                o.title = [] { m3::Text("Reset settings?"); };
                o.text = [] { m3::Text("This will reset your app preferences back to their default settings."); };
                o.dismissButton = [open, result] { m3::TextButton([open, result] { result->Set("dismissed"); open->Set(false); }, {}, {}, [] { m3::Text("Cancel"); }); };
                m3::AlertDialog([open, result] { result->Set("outside"); open->Set(false); },
                                [open, result] { m3::TextButton([open, result] { result->Set("confirmed"); open->Set(false); }, {}, {}, [] { m3::Text("Accept"); }); },
                                {}, o);
            }
        }

        // Hides the modal sheet, then closes it (and reports why) once it is out of sight.
        rt::Task<> HideSheetThen(std::shared_ptr<m3::SheetState> state, rt::State<bool>* open, rt::State<std::string>* result,
                                 std::string why) {
            co_await state->Hide();
            if (!state->IsVisible()) {
                result->Set(why);
                open->Set(false);
            }
        }

        void Sheets() {
            auto* open = &RememberState(false);
            auto* result = &RememberState(std::string("none"));
            auto jobs = rt::Composition::Current()->Remember<std::shared_ptr<std::vector<rt::Job>>>([] { return std::make_shared<std::vector<rt::Job>>(); });
            // Reported as text so a test can read it.
            m3::Text("Sheet result: " + result->Get());
            m3::TextButton([open] { open->Set(true); }, {}, {}, [] { m3::Text("Show bottom sheet"); });
            if (open->Get()) {
                auto state = m3::RememberBottomSheetState(m3::SheetValue::Hidden);
                m3::ModalBottomSheetOptions o;
                o.sheetState = state;
                m3::ModalBottomSheet([open, result] { result->Set("dismissed"); open->Set(false); }, {}, o, [=] {
                    m3::Text("Share with", Padding({}, 16_dp, 8_dp));
                    for (const char* target : { "Copy link", "Messages", "Email" }) {
                        m3::TextButton([=] { jobs->push_back(rt::Launch(rt::Dispatcher::Main(), HideSheetThen(state, open, result, target))); },
                                       Padding({}, 8_dp, 0_dp), {}, [target] { m3::Text(target); });
                    }
                    for (int i = 1; i <= 12; ++i) m3::Text("Contact " + std::to_string(i), Padding({}, 16_dp, 12_dp));
                });
            }
            // A standard sheet peeking over its screen.
            auto sheet = m3::RememberBottomSheetState(m3::SheetValue::PartiallyExpanded, m3::SheetValues{ false, true });
            m3::Text(std::string("Standard sheet: ") + (sheet->CurrentValue() == m3::SheetValue::Expanded ? "expanded" : "peeking"));
            Box(Height(FillMax({}, Axis::Horizontal), 420_dp), Alignment::TopStart(), false, [sheet] {
                m3::BottomSheetScaffoldOptions o;
                o.scaffoldState = m3::RememberBottomSheetScaffoldState(sheet);
                o.sheetPeekHeight = 128_dp;
                m3::BottomSheetScaffold(
                    [] {
                        m3::Text("Nearby places", Padding({}, 16_dp, 8_dp));
                        for (const char* place : { "Bakery", "Library", "Park", "Station" }) m3::Text(place, Padding({}, 16_dp, 12_dp));
                    },
                    {}, o, [](const PaddingValues& padding) {
                        Box(Padding(FillMax({}, Axis::Both), padding), Alignment::Center(), false, [] { m3::Text("Map area"); });
                    });
            });
        }

        rt::Task<> CloseDrawerThen(std::shared_ptr<m3::DrawerState> state) { co_await state->Close(); }
        rt::Task<> OpenDrawer(std::shared_ptr<m3::DrawerState> state) { co_await state->Open(); }

        void Drawers() {
            auto modal = m3::RememberDrawerState(m3::DrawerValue::Closed);
            auto dismissible = m3::RememberDrawerState(m3::DrawerValue::Closed);
            auto* selected = &RememberState(std::string("Inbox"));
            auto jobs = rt::Composition::Current()->Remember<std::shared_ptr<std::vector<rt::Job>>>([] { return std::make_shared<std::vector<rt::Job>>(); });
            // Reported as text so a test can read it.
            m3::Text(std::string("Drawer: ") + (modal->IsOpen() ? "open" : "closed") + ", selected: " + selected->Get());
            const auto items = [=](std::shared_ptr<m3::DrawerState> state) {
                return [=] {
                    m3::Text("Mail", Padding({}, 28_dp, 16_dp));
                    for (const char* item : { "Inbox", "Outbox", "Favorites" }) {
                        m3::NavigationDrawerItemOptions o;
                        o.icon = [] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt); };
                        if (std::string(item) == "Inbox") o.badge = [] { m3::Text("24"); };
                        m3::NavigationDrawerItem([item] { m3::Text(item); }, selected->Get() == item, [=] {
                            selected->Set(item);
                            jobs->push_back(rt::Launch(rt::Dispatcher::Main(), CloseDrawerThen(state)));
                        }, Padding({}, m3::NavigationDrawerItemDefaults::ItemPadding), o);
                    }
                };
            };
            Box(Height(FillMax({}, Axis::Horizontal), 420_dp), Alignment::TopStart(), false, [=] {
                m3::ModalNavigationDrawer([=] { m3::ModalDrawerSheet({}, {}, items(modal), modal); }, {}, { .drawerState = modal }, [=] {
                    Box(FillMax({}, Axis::Both), Alignment::Center(), false, [=] {
                        m3::TextButton([=] { jobs->push_back(rt::Launch(rt::Dispatcher::Main(), OpenDrawer(modal))); }, {}, {}, [] { m3::Text("Open drawer"); });
                    });
                });
            });
            m3::Text(std::string("Dismissible: ") + (dismissible->IsOpen() ? "open" : "closed"));
            Box(Height(FillMax({}, Axis::Horizontal), 360_dp), Alignment::TopStart(), false, [=] {
                m3::DismissibleNavigationDrawer([=] { m3::DismissibleDrawerSheet({}, {}, items(dismissible), dismissible); }, {}, { .drawerState = dismissible }, [] {
                    Box(FillMax({}, Axis::Both), Alignment::Center(), false, [] { m3::Text("Swipe to reveal"); });
                });
            });
        }

        rt::Task<> CollapseSearch(std::shared_ptr<m3::SearchBarState> state) { co_await state->AnimateToCollapsed(); }

        void Search() {
            // Results sit on the bar's container (ListItemDefaults.colors(containerColor = Transparent)).
            m3::LegacyListItemOptions row;
            m3::ListItemColors transparent;
            transparent.containerColor = Color::Transparent();
            row.colors = m3::ListItemDefaults::colors(transparent);
            auto* picked = &RememberState(std::string("none"));
            auto jobs = rt::Composition::Current()->Remember<std::shared_ptr<std::vector<rt::Job>>>([] { return std::make_shared<std::vector<rt::Job>>(); });
            // Reported as text so a test can read it.
            m3::Text("Searched: " + picked->Get());
            // A bar that expands to full screen.
            auto fruitBar = m3::RememberSearchBarState();
            auto& fruit = foundation::RememberTextFieldState();
            const auto fruitField = [fruitBar, &fruit, picked] {
                m3::SearchBarInputFieldOptions o;
                o.placeholder = [] { m3::Text("Search fruit"); };
                o.leadingIcon = [] { m3::Icon(m3::internal::Icons::Filled::Edit(), std::nullopt); };
                m3::SearchBarDefaults::InputField(fruit, fruitBar, [picked](std::string q) { picked->Set(q.empty() ? std::string("(empty)") : q); }, {}, o);
            };
            m3::SearchBar(fruitBar, fruitField, Padding({}, 8_dp));
            m3::ExpandedFullScreenSearchBar(fruitBar, fruitField, {}, {}, [=, &fruit] {
                for (const char* item : { "Apple", "Banana", "Cherry", "Mango" }) {
                    m3::ListItem([item] { m3::Text(item); }, Clickable({}, [=, &fruit] {
                        fruit.SetTextAndPlaceCursorAtEnd(item);
                        picked->Set(item);
                        jobs->push_back(rt::Launch(rt::Dispatcher::Main(), CollapseSearch(fruitBar)));
                    }), row);
                }
            });
            // A docked bar: the results drop down below it.
            auto placeBar = m3::RememberSearchBarState();
            auto& place = foundation::RememberTextFieldState();
            const auto placeField = [placeBar, &place, picked] {
                m3::SearchBarInputFieldOptions o;
                o.placeholder = [] { m3::Text("Search places"); };
                m3::SearchBarDefaults::InputField(place, placeBar, [picked](std::string q) { picked->Set(q); }, {}, o);
            };
            m3::SearchBar(placeBar, placeField, Padding({}, 8_dp));
            m3::ExpandedDockedSearchBar(placeBar, placeField, {}, {}, [=, &place] {
                for (const char* item : { "Library", "Station", "Park" }) {
                    m3::ListItem([item] { m3::Text(item); }, Clickable({}, [=, &place] {
                        place.SetTextAndPlaceCursorAtEnd(item);
                        picked->Set(item);
                        jobs->push_back(rt::Launch(rt::Dispatcher::Main(), CollapseSearch(placeBar)));
                    }), row);
                }
            });
        }

        void SwipeRows() {
            auto* swiped = &RememberState(std::string("none"));
            auto* shown = &RememberState(std::vector<std::string>{ "Message from Ada", "Message from Grace", "Message from Alan" });
            m3::Text("Swiped: " + swiped->Get());
            for (const std::string& title : shown->Get()) {
                // Keyed by title: a row's state stays with it as rows above are dismissed.
                rt::Composition::Current()->Child([=] {
                    auto state = m3::RememberSwipeToDismissBoxState();
                    m3::SwipeToDismissBoxOptions o;
                    o.onDismiss = [=](m3::SwipeToDismissBoxValue v) {
                        swiped->Set(title + (v == m3::SwipeToDismissBoxValue::StartToEnd ? " archived" : " deleted"));
                        auto rest = shown->Peek();
                        std::erase(rest, title);
                        shown->Set(std::move(rest));
                    };
                    // The background names what a release will do, on the side it is revealed from.
                    m3::SwipeToDismissBox(state, [state] {
                        const auto dir = state->DismissDirection();
                        const auto& cs = m3::MaterialTheme::colorScheme();
                        const Color bg = dir == m3::SwipeToDismissBoxValue::StartToEnd ? cs.tertiaryContainer
                                       : dir == m3::SwipeToDismissBoxValue::EndToStart ? cs.errorContainer : Color::Transparent();
                        Box(Padding(Background(FillMax({}, Axis::Both), bg), 16_dp, 0_dp),
                            dir == m3::SwipeToDismissBoxValue::EndToStart ? Alignment::CenterEnd() : Alignment::CenterStart(), false, [dir] {
                                if (dir != m3::SwipeToDismissBoxValue::Settled) m3::Text(dir == m3::SwipeToDismissBoxValue::StartToEnd ? "Archive" : "Delete");
                            });
                    }, {}, o, [title] { m3::ListItem([title] { m3::Text(title); }); });
                }, rt::Key(std::string_view(title)));
            }
        }

        rt::Task<> FinishRefresh(rt::State<bool>* refreshing, rt::State<int>* count) {
            co_await rt::Delay(1500);
            count->Set(count->Peek() + 1);
            refreshing->Set(false);
        }

        void Refresh() {
            auto* refreshing = &RememberState(false);
            auto* count = &RememberState(0);
            auto job = rt::Composition::Current()->Remember<std::shared_ptr<rt::Job>>([] { return std::make_shared<rt::Job>(); });
            m3::Text("Refreshes: " + std::to_string(count->Get()) + (refreshing->Get() ? " (refreshing)" : ""));
            auto& list = foundation::RememberLazyListState();
            m3::PullToRefreshBox(refreshing->Get(), [=] {
                refreshing->Set(true);
                *job = rt::Launch(rt::Dispatcher::Main(), FinishRefresh(refreshing, count));
            }, Height(FillMax({}, Axis::Horizontal), 320_dp), {}, [&list] {
                LazyColumn(FillMax({}, Axis::Both), list, [](LazyListScope& s) {
                    s.Items(30, [](int i) { m3::ListItem([i] { m3::Text("Update " + std::to_string(i + 1)); }); });
                });
            });
        }

        void CarouselPhoto(const m3::CarouselItemScope& scope, int i) {
            const auto& cs = m3::MaterialTheme::colorScheme();
            const Color colors[] = { cs.primaryContainer, cs.secondaryContainer, cs.tertiaryContainer, cs.errorContainer };
            Box(Background(scope.MaskClip(FillMax({}, Axis::Both), m3::MaterialTheme::shapes().extraLarge), colors[i % 4]),
                Alignment::Center(), false, [i] { m3::Text("Photo " + std::to_string(i + 1)); });
        }

        void Carousels() {
            auto multi = m3::RememberCarouselState([] { return 10; });
            m3::Text("Multi-browse item: " + std::to_string(multi->CurrentItem() + 1));
            m3::CarouselOptions spaced;
            spaced.itemSpacing = 8_dp;
            spaced.contentPadding = { 16_dp, 0_dp, 16_dp, 0_dp };
            m3::HorizontalMultiBrowseCarousel(*multi, 186_dp, Height(FillMax({}, Axis::Horizontal), 221_dp), CarouselPhoto, spaced);
            auto hero = m3::RememberCarouselState([] { return 10; });
            m3::Text("Hero item: " + std::to_string(hero->CurrentItem() + 1));
            m3::HorizontalCenteredHeroCarousel(*hero, Height(FillMax({}, Axis::Horizontal), 221_dp), CarouselPhoto, spaced);
            auto uncontained = m3::RememberCarouselState([] { return 10; });
            m3::Text("Uncontained");
            m3::HorizontalUncontainedCarousel(*uncontained, 240_dp, Height(FillMax({}, Axis::Horizontal), 221_dp), CarouselPhoto, spaced);
        }

        std::string IsoDate(std::optional<std::int64_t> millis) {
            if (!millis) return "none";
            const text::CivilDate d = text::CivilFromUtcMillis(*millis);
            char buffer[16];
            std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", d.year, d.month, d.day);
            return buffer;
        }
        std::string ClockTime(const m3::TimePickerState& s) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "%02d:%02d", s.Hour(), s.Minute());
            return buffer;
        }

        void DatePickers() {
            const std::int64_t january15 = text::UtcMillis({ 2026, 1, 15 });
            auto inline_ = m3::RememberDatePickerState(january15);
            m3::Text("Inline date: " + IsoDate(inline_->SelectedDateMillis()));
            m3::DatePicker(*inline_);
            auto* open = &RememberState(0);   // 0 closed, 1 date, 2 range
            auto* picked = &RememberState(std::string("none"));
            m3::Text("Dialog date: " + picked->Get());
            FlowRow({}, Arrangement::SpacedBy(8_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [open] {
                m3::TextButton([open] { open->Set(1); }, {}, {}, [] { m3::Text("Pick a date"); });
                m3::TextButton([open] { open->Set(2); }, {}, {}, [] { m3::Text("Pick a range"); });
            });
            if (open->Get() == 1) {
                auto state = m3::RememberDatePickerState(january15);
                m3::DatePickerDialogOptions o;
                o.dismissButton = [open] { m3::TextButton([open] { open->Set(0); }, {}, {}, [] { m3::Text("Cancel"); }); };
                m3::DatePickerDialog([open] { open->Set(0); },
                                     [open, picked, state] {
                                         m3::TextButton([open, picked, state] { picked->Set(IsoDate(state->SelectedDateMillis())); open->Set(0); }, {}, {}, [] { m3::Text("OK"); });
                                     },
                                     {}, o, [state] { m3::DatePicker(*state); });
            } else if (open->Get() == 2) {
                auto state = m3::RememberDateRangePickerState(january15, text::UtcMillis({ 2026, 1, 20 }));
                m3::DatePickerDialogOptions o;
                o.dismissButton = [open] { m3::TextButton([open] { open->Set(0); }, {}, {}, [] { m3::Text("Cancel"); }); };
                m3::DatePickerDialog([open] { open->Set(0); },
                                     [open, picked, state] {
                                         m3::TextButton([open, picked, state] {
                                             picked->Set(IsoDate(state->SelectedStartDateMillis()) + " to " + IsoDate(state->SelectedEndDateMillis()));
                                             open->Set(0);
                                         }, {}, {}, [] { m3::Text("OK"); });
                                     },
                                     {}, o, [state] { m3::DateRangePicker(*state, Height({}, 500_dp)); });
            }
        }

        void TimePickers() {
            // The dialog first: the inline time input below holds focus (and the keyboard).
            auto* open = &RememberState(false);
            auto* picked = &RememberState(std::string("none"));
            m3::Text("Dialog time: " + picked->Get());
            m3::TextButton([open] { open->Set(true); }, {}, {}, [] { m3::Text("Pick a time"); });
            if (open->Get()) {
                auto state = m3::RememberTimePickerState(18, 30, false);
                auto* mode = &RememberState(m3::TimePickerDisplayMode::Picker);
                m3::TimePickerDialogOptions o;
                o.dismissButton = [open] { m3::TextButton([open] { open->Set(false); }, {}, {}, [] { m3::Text("Cancel"); }); };
                o.modeToggleButton = [mode] {
                    m3::TimePickerDialogDefaults::DisplayModeToggle([mode] {
                        mode->Set(mode->Peek() == m3::TimePickerDisplayMode::Picker ? m3::TimePickerDisplayMode::Input : m3::TimePickerDisplayMode::Picker);
                    }, mode->Get());
                };
                m3::TimePickerDialog([open] { open->Set(false); },
                                     [open, picked, state] {
                                         m3::TextButton([open, picked, state] { picked->Set(ClockTime(*state)); open->Set(false); }, {}, {}, [] { m3::Text("OK"); });
                                     },
                                     [mode] { m3::TimePickerDialogDefaults::Title(mode->Get()); }, {}, o,
                                     [mode, state] {
                                         if (mode->Get() == m3::TimePickerDisplayMode::Picker) m3::TimePicker(*state);
                                         else m3::TimeInput(*state);
                                     });
            }
            auto clock = m3::RememberTimePickerState(14, 23, false);
            m3::Text("Clock time: " + ClockTime(*clock));
            m3::TimePicker(*clock);
            auto input = m3::RememberTimePickerState(9, 41, true);
            m3::Text("Typed time: " + ClockTime(*input));
            m3::TimeInput(*input);
            auto wheels = m3::RememberTimePickerState(7, 5, false);
            m3::Text("Wheel time: " + ClockTime(*wheels));
            m3::TimeScroll(*wheels, m3::TimePickerDefaults::shapes());
        }

        void ScrollFields() {
            auto state = m3::RememberScrollFieldState(60, 15);
            m3::Text("Scroll field: " + std::to_string(state->SelectedOption()));
            m3::ScrollField(*state, std::string("Minutes"), SizeMod({}, 100_dp, m3::ScrollFieldDefaults::ScrollFieldHeight));
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
                m3::ListItem(selected->Get(), kiln::Function<void()>([selected] { selected->Set(!selected->Peek()); }), {}, radio, [] { m3::Text("Expressive radio"); });
                m3::ListItemOptions toggle;
                toggle.leadingContent = icon;
                toggle.trailingContent = [checked] { m3::Checkbox(checked->Get(), {}); };
                m3::ListItem(checked->Get(), kiln::Function<void(bool)>([checked](bool value) { checked->Set(value); }), {}, toggle, [] { m3::Text("Expressive checkbox"); });
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
                m3::SegmentedListItem(checked->Get(), kiln::Function<void(bool)>([checked](bool value) { checked->Set(value); }),
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

        // Shared elements: a card opens into a detail page. Its bounds animate between the two places
        // (sharedBounds) and its title moves with it (sharedElement); back closes it.
        void SharedElements() {
            auto& open = RememberState(0);   // 0: the cards, n: card n opened
            static const Color colors[] = { Color(0xFF6750A4u), Color(0xFF7D5260u), Color(0xFF386A20u) };
            auto* opened = &open;
            Column(Modifier{}, Arrangement::Top(), AlignmentH::Start(), [opened] {
                SharedTransitionLayout(Height(FillMax(Modifier{}, Axis::Horizontal), 220_dp), [opened](SharedTransitionScope& shared) {
                    BackHandler(opened->Get() != 0, [opened] { opened->Set(0); });
                    AnimatedContent(kiln::rt::Key(std::int64_t(opened->Get())), FillMax(Modifier{}, Axis::Both), [opened, &shared](kiln::rt::Key k) {
                        auto* scope = kiln::rt::Composition::Current()->Read(LocalAnimatedVisibilityScope);
                        int shown = 0;
                        for (int n = 1; n <= 3; ++n) if (k == kiln::rt::Key(std::int64_t(n))) shown = n;
                        auto card = [&shared, scope](int n, Modifier m) {
                            auto& bounds = shared.RememberSharedContentState(kiln::rt::Key("card-" + std::to_string(n)));
                            return shared.SharedBounds(Background(std::move(m), colors[n - 1], RoundedCorner(16_dp)), bounds, *scope);
                        };
                        auto title = [&shared, scope](int n, float size) {
                            auto& text = shared.RememberSharedContentState(kiln::rt::Key("title-" + std::to_string(n)));
                            m3::Text("Card " + std::to_string(n), shared.SharedElement(Modifier{}, text, *scope),
                                     { .color = Color(0xFFFFFFFFu), .fontSize = text::Sp(size) });
                        };
                        if (shown == 0) {
                            Row(Modifier{}, Arrangement::SpacedBy(12_dp), AlignmentV::Top(), [card, title, opened] {
                                for (int n = 1; n <= 3; ++n)
                                    Box(Padding(card(n, Clickable(SizeMod(Modifier{}, 96_dp, 96_dp), [opened, n] { opened->Set(n); })), 12_dp),
                                        Alignment::BottomStart(), false, [title, n] { title(n, 16); }, kiln::rt::Key(n));
                            });
                        } else {
                            Box(Padding(card(shown, FillMax(Modifier{}, Axis::Both)), 20_dp), Alignment::TopStart(), false, [title, shown, opened] {
                                Column(Modifier{}, Arrangement::SpacedBy(12_dp), AlignmentH::Start(), [title, shown, opened] {
                                    title(shown, 28);
                                    m3::Text("Close", Clickable(Modifier{}, [opened] { opened->Set(0); }), { .color = Color(0xFFFFFFFFu) });
                                });
                            });
                        }
                    });
                });
            });
        }

        // Android Views inside kiln: a WebView with a local page, a SeekBar to drag, and a
        // platform ScrollView whose scrolling chains into the page's (nested scrolling).
        void NativeViews() {
            auto make = [](const char* cls, kiln::Function<void(void* env, void* view)> setup) -> ui::InteropViewFactory {
                auto base = AndroidViewClass(cls);
                return [base, setup](void* env, void* context) -> void* {
                    void* view = base(env, context);
                    if (view && setup) setup(env, view);
                    return view;
                };
            };
            m3::Text("WebView, SeekBar and ScrollView");
            AndroidView(make("android.webkit.WebView", [](void* e, void* v) {
#if defined(__ANDROID__)
                auto* env = static_cast<JNIEnv*>(e);
                auto view = static_cast<jobject>(v);
                jclass c = env->GetObjectClass(view);
                // loadDataWithBaseURL: loadData would take the colour's '#' for a fragment.
                jmethodID load = env->GetMethodID(c, "loadDataWithBaseURL",
                                                  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
                jstring html = env->NewStringUTF("<html><body style='font-family:sans-serif;background:#EADDFF'>"
                                                 "<h2>Hello from WebView</h2><p>A web page drawn inside kiln.</p></body></html>");
                jstring type = env->NewStringUTF("text/html");
                jstring enc = env->NewStringUTF("utf-8");
                env->CallVoidMethod(view, load, nullptr, html, type, enc, nullptr);
                for (jobject r : { static_cast<jobject>(c), static_cast<jobject>(html), static_cast<jobject>(type), static_cast<jobject>(enc) }) env->DeleteLocalRef(r);
#else
                (void)e; (void)v;
#endif
            }), Height(FillMax(Modifier{}, Axis::Horizontal), 140_dp));
            AndroidView(AndroidViewClass("android.widget.SeekBar"), FillMax(Modifier{}, Axis::Horizontal));
            AndroidView(make("android.widget.ScrollView", [](void* e, void* v) {
#if defined(__ANDROID__)
                auto* env = static_cast<JNIEnv*>(e);
                auto view = static_cast<jobject>(v);
                jclass scrollClass = env->GetObjectClass(view);
                jmethodID getContext = env->GetMethodID(scrollClass, "getContext", "()Landroid/content/Context;");
                jobject context = env->CallObjectMethod(view, getContext);
                // A column of lines, one TextView each.
                jclass columnClass = env->FindClass("android/widget/LinearLayout");
                jobject column = env->NewObject(columnClass, env->GetMethodID(columnClass, "<init>", "(Landroid/content/Context;)V"), context);
                env->CallVoidMethod(column, env->GetMethodID(columnClass, "setOrientation", "(I)V"), jint(1));
                jclass textClass = env->FindClass("android/widget/TextView");
                jmethodID textInit = env->GetMethodID(textClass, "<init>", "(Landroid/content/Context;)V");
                jmethodID setText = env->GetMethodID(textClass, "setText", "(Ljava/lang/CharSequence;)V");
                jmethodID addView = env->GetMethodID(columnClass, "addView", "(Landroid/view/View;)V");
                for (int i = 1; i <= 40; ++i) {
                    jobject text = env->NewObject(textClass, textInit, context);
                    jstring js = env->NewStringUTF(("Native line " + std::to_string(i)).c_str());
                    env->CallVoidMethod(text, setText, js);
                    env->CallVoidMethod(column, addView, text);
                    env->DeleteLocalRef(js);
                    env->DeleteLocalRef(text);
                }
                env->CallVoidMethod(view, env->GetMethodID(scrollClass, "addView", "(Landroid/view/View;)V"), column);
                env->CallVoidMethod(view, env->GetMethodID(scrollClass, "setNestedScrollingEnabled", "(Z)V"), jboolean(true));
                for (jobject r : { static_cast<jobject>(scrollClass), context, static_cast<jobject>(textClass), static_cast<jobject>(columnClass), column })
                    env->DeleteLocalRef(r);
#else
                (void)e; (void)v;
#endif
            }), Height(FillMax(Modifier{}, Axis::Horizontal), 120_dp));
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
                { "navigation", "Navigation bars and rails", Navigation }, { "tabs", "Tabs", Tabs }, { "appbars", "App bars", AppBars }, { "toolbars", "Floating toolbars", FloatingToolbars }, { "scrollbars", "Scrollbars", Scrollbars }, { "snackbars", "Snackbars", Snackbars }, { "textfields", "Text fields", TextFields }, { "dialogs", "Dialogs", Dialogs }, { "sheets", "Bottom sheets", Sheets }, { "drawers", "Navigation drawers", Drawers }, { "search", "Search bars", Search }, { "swipe", "Swipe to dismiss", SwipeRows }, { "refresh", "Pull to refresh", Refresh }, { "carousel", "Carousels", Carousels }, { "datepickers", "Date pickers", DatePickers }, { "timepickers", "Time pickers", TimePickers }, { "scrollfield", "Scroll field", ScrollFields }, { "segmented", "Segmented buttons", SegmentedButtons }, { "split", "Split buttons", SplitButtons },
                { "tooltips", "Tooltips", Tooltips }, { "menus", "Menus", Menus }, { "groups", "Button groups", ButtonGroups },
                { "sliders", "Sliders", Sliders }, { "progress", "Progress indicators", ProgressIndicators }, { "lists", "Lists", Lists },
                { "chips", "Chips", Chips }, { "cards", "Cards", Cards }, { "selection", "", SelectionControls },
                { "fabmenus", "FAB menus", FabMenus }, { "fabs", "Floating action buttons", FloatingActionButtons },
                { "toggles", "Toggle buttons", ToggleButtons }, { "buttons", "Material buttons", Buttons },
                { "iconbuttons", "Icon buttons", IconButtons }, { "shared", "Shared elements", SharedElements },
                { "views", "Android Views", NativeViews },
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
