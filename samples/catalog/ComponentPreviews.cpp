// ComponentPreviews.kt (material3 catalog): the small live preview each home screen tile shows.
#include "ComponentPreviews.h"
#include "samples/Common.h"

#include <kiln/foundation/Border.h>
#include <kiln/material3/Badge.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/Card.h>
#include <kiln/material3/Checkbox.h>
#include <kiln/material3/Chip.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/FloatingActionButton.h>
#include <kiln/material3/FloatingToolbar.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/LoadingIndicator.h>
#include <kiln/material3/ProgressIndicator.h>
#include <kiln/material3/RadioButton.h>
#include <kiln/material3/SegmentedButton.h>
#include <kiln/material3/Slider.h>
#include <kiln/material3/SplitButton.h>
#include <kiln/material3/Surface.h>
#include <kiln/material3/Switch.h>
#include <kiln/material3/Tab.h>
#include <kiln/material3/TabRow.h>
#include <kiln/material3/ToggleButton.h>

#include <map>

namespace catalog {
    using namespace samples;

    namespace {
        const m3::ColorScheme& Colors() { return m3::MaterialTheme::colorScheme(); }

        // Surface(modifier, shape, color, border)
        void Panel(Modifier m, ShapePtr shape, Color color, std::function<void()> content, bool border = true, Dp shadow = 0_dp,
                   std::optional<Color> borderColor = std::nullopt) {
            m3::SurfaceOptions o;
            o.shape = std::move(shape);
            o.color = color;
            o.shadowElevation = shadow;
            if (border) o.border = BorderStroke(1_dp, borderColor.value_or(Colors().outlineVariant));
            m3::Surface(m, o, std::move(content));
        }
        // Box(Modifier.size(...).background(color, CircleShape))
        void Pill(Modifier m, Color color) { Box(Background(std::move(m), color, CircleShape())); }
        void SmallIcon(const ImageVectorPtr& icon, Dp size) { m3::Icon(icon, std::nullopt, Size({}, size)); }
        void Label(const std::string& text, const kiln::text::TextStyle& style, Modifier m = {}, std::optional<kiln::text::FontWeight> weight = std::nullopt) {
            m3::TextOptions o;
            o.style = style;
            o.fontWeight = weight;
            m3::Text(text, std::move(m), o);
        }

        void AdaptivePreview() {
            Panel(SizeMod({}, 88_dp, 56_dp), RoundedCorner(10_dp), Colors().surfaceContainerLow, [] {
                Row(Padding(FillMax({}, Axis::Both), 6_dp), Arrangement::SpacedBy(5_dp), AlignmentV::Top(), [] {
                    Panel(FillMax(Weight({}, 0.4f), Axis::Vertical), RoundedCorner(6_dp), Colors().secondaryContainer, [] {}, false);
                    Panel(FillMax(Weight({}, 0.6f), Axis::Vertical), RoundedCorner(6_dp), Colors().primaryContainer, [] {}, false);
                });
            });
        }

        void BadgePreview() {
            Box(Padding({}, 12_dp), Alignment::Center(), false, [] {
                m3::BadgedBox([] { m3::Badge({}, std::nullopt, std::nullopt, [] { m3::Text("3"); }); }, {},
                              [] { m3::Icon(Icons::Filled::Notifications(), std::nullopt, Size({}, 32_dp)); });
            });
        }

        void BottomAppBarPreview() {
            Panel(SizeMod({}, 96_dp, 40_dp), RoundedCorner(12_dp), Colors().surfaceContainer, [] {
                Row(Padding(FillMax({}, Axis::Both), 8_dp, 0_dp), Arrangement::SpaceBetween(), AlignmentV::CenterVertically(), [] {
                    Row({}, Arrangement::SpacedBy(6_dp), AlignmentV::Top(), [] {
                        SmallIcon(Icons::Filled::Menu(), 16_dp);
                        SmallIcon(Icons::Filled::Search(), 16_dp);
                    });
                    Panel(Size({}, 24_dp), RoundedCorner(7_dp), Colors().primaryContainer, [] {
                        Box({}, Alignment::Center(), false, [] { SmallIcon(Icons::Filled::Add(), 14_dp); });
                    }, false);
                });
            });
        }

        void BottomSheetPreview() {
            Panel(SizeMod({}, 84_dp, 54_dp), RoundedCorner(CornerSize::Of(14_dp), CornerSize::Of(14_dp), CornerSize::Of(4_dp), CornerSize::Of(4_dp)),
                  Colors().surfaceContainerHigh, [] {
                Column(Padding(FillMax({}, Axis::Both), { 12_dp, 8_dp, 12_dp, 0_dp }), Arrangement::SpacedBy(8_dp), AlignmentH::CenterHorizontally(), [] {
                    Pill(SizeMod({}, 24_dp, 4_dp), Colors().onSurfaceVariant);
                    Pill(Height(FillMax({}, Axis::Horizontal), 6_dp), Colors().outlineVariant);
                    Pill(Height(AlignInColumn(FillMax({}, Axis::Horizontal, 0.7f), AlignmentH::Start()), 6_dp), Colors().outlineVariant);
                });
            });
        }

        void ButtonsPreview() { m3::Button([] {}, {}, {}, [] { m3::Text("Button"); }); }

        void ButtonGroupsPreview() {
            Row({}, Arrangement::SpacedBy(4_dp), AlignmentV::Top(), [] {
                m3::ButtonOptions o;
                o.contentPadding = PaddingValues{ 12_dp, 6_dp, 12_dp, 6_dp };
                m3::FilledTonalButton([] {}, {}, o, [] { m3::Text("One"); });
                m3::Button([] {}, {}, o, [] { m3::Text("Two"); });
            });
        }

        void CardPreview() {
            m3::ElevatedCard(SizeMod({}, 84_dp, 56_dp), {}, [] {
                Column(Padding(FillMax({}, Axis::Both), 10_dp), Arrangement::SpaceBetween(), AlignmentH::Start(), [] {
                    Pill(SizeMod({}, 36_dp, 8_dp), Colors().primary);
                    Column({}, Arrangement::SpacedBy(4_dp), AlignmentH::Start(), [] {
                        Pill(Height(FillMax({}, Axis::Horizontal), 5_dp), Colors().outlineVariant);
                        Pill(Height(FillMax({}, Axis::Horizontal, 0.65f), 5_dp), Colors().outlineVariant);
                    });
                });
            });
        }

        void CarouselPreview() {
            Row({}, Arrangement::SpacedBy(4_dp), AlignmentV::CenterVertically(), [] {
                Panel(SizeMod({}, 44_dp, 54_dp), RoundedCorner(12_dp), Colors().primaryContainer, [] {}, false);
                Panel(SizeMod({}, 24_dp, 54_dp), RoundedCorner(10_dp), Colors().secondaryContainer, [] {}, false);
                Panel(SizeMod({}, 12_dp, 54_dp), RoundedCorner(8_dp), Colors().tertiaryContainer, [] {}, false);
            });
        }

        void CheckboxesPreview() { m3::Checkbox(true, [](bool) {}); }

        void ChipsPreview() {
            m3::SelectableChipOptions o;
            o.leadingIcon = [] { m3::Icon(Icons::Filled::Check(), std::nullopt, Size({}, m3::FilterChipDefaults::IconSize)); };
            m3::FilterChip(true, [] {}, [] { m3::Text("Chip"); }, {}, o);
        }

        void DatePickersPreview() {
            m3::OutlinedCard(SizeMod({}, 76_dp, 60_dp), {}, [] {
                Column(FillMax({}, Axis::Both), Arrangement::Top(), AlignmentH::Start(), [] {
                    Box(Background(Height(FillMax({}, Axis::Horizontal), 16_dp), Colors().primaryContainer));
                    Column(Padding(FillMax({}, Axis::Both), 6_dp), Arrangement::SpaceEvenly(), AlignmentH::Start(), [] {
                        for (int row = 0; row < 2; ++row)
                            Row(FillMax({}, Axis::Horizontal), Arrangement::SpaceEvenly(), AlignmentV::Top(), [row] {
                                for (int col = 0; col < 4; ++col)
                                    Pill(Size({}, 8_dp), row == 0 && col == 2 ? Colors().primary : Colors().outlineVariant);
                            }, kiln::rt::Key(row));
                    });
                });
            });
        }

        void DialogsPreview() {
            Panel(SizeMod({}, 84_dp, 58_dp), RoundedCorner(14_dp), Colors().surfaceContainerHigh, [] {
                Column(Padding(FillMax({}, Axis::Both), 8_dp), Arrangement::SpaceBetween(), AlignmentH::Start(), [] {
                    Pill(SizeMod({}, 40_dp, 7_dp), Colors().onSurface);
                    Pill(Height(FillMax({}, Axis::Horizontal), 5_dp), Colors().outlineVariant);
                    Row(FillMax({}, Axis::Horizontal), Arrangement::End(), AlignmentV::Top(), [] { Pill(SizeMod({}, 20_dp, 7_dp), Colors().primary); });
                });
            });
        }

        void ExtendedFabPreview() {
            m3::ExtendedFloatingActionButton([] { m3::Text("FAB"); }, [] { m3::Icon(Icons::Filled::Add(), std::nullopt); }, [] {});
        }

        void FloatingActionButtonsPreview() {
            m3::FloatingActionButton([] {}, {}, {}, [] { m3::Icon(Icons::Filled::Add(), std::nullopt); });
        }

        void FabMenuPreview() {
            Column({}, Arrangement::SpacedBy(4_dp), AlignmentH::End(), [] {
                Panel({}, CircleShape(), Colors().secondaryContainer, [] {
                    Row(Padding({}, 8_dp, 4_dp), Arrangement::SpacedBy(4_dp), AlignmentV::CenterVertically(), [] { SmallIcon(Icons::Filled::Edit(), 12_dp); });
                }, false);
                m3::SmallFloatingActionButton([] {}, {}, {}, [] { SmallIcon(Icons::Filled::Close(), 18_dp); });
            });
        }

        void FloatingToolbarsPreview() {
            m3::HorizontalFloatingToolbar(true, {}, {}, [] {
                m3::IconButton([] {}, {}, {}, [] { m3::Icon(Icons::Filled::Edit(), std::nullopt); });
                m3::IconButton([] {}, {}, {}, [] { m3::Icon(Icons::Filled::Favorite(), std::nullopt); });
            });
        }

        void IconButtonsPreview() { m3::FilledIconButton([] {}, {}, {}, [] { m3::Icon(Icons::Filled::Lock(), std::nullopt); }); }

        void ListsPreview() {
            Panel(Width({}, 96_dp), RoundedCorner(10_dp), Colors().surfaceContainerLow, [] {
                Column(Padding({}, 8_dp), Arrangement::SpacedBy(6_dp), AlignmentH::Start(), [] {
                    for (int i = 0; i < 2; ++i)
                        Row({}, Arrangement::SpacedBy(6_dp), AlignmentV::CenterVertically(), [] {
                            Pill(Size({}, 14_dp), Colors().primaryContainer);
                            Column({}, Arrangement::SpacedBy(3_dp), AlignmentH::Start(), [] {
                                Pill(SizeMod({}, 48_dp, 5_dp), Colors().onSurface);
                                Pill(SizeMod({}, 32_dp, 4_dp), Colors().outlineVariant);
                            });
                        }, kiln::rt::Key(i));
                });
            });
        }

        void LoadingIndicatorsPreview() { m3::ContainedLoadingIndicator([] { return 0.75f; }); }

        void MenusPreview() {
            Panel(Width({}, 84_dp), RoundedCorner(10_dp), Colors().surfaceContainerHigh, [] {
                Column(Padding({}, 0_dp, 6_dp), Arrangement::Top(), AlignmentH::Start(), [] {
                    for (int index = 0; index < 2; ++index) {
                        Row(Padding(FillMax({}, Axis::Horizontal), 8_dp, 4_dp), Arrangement::SpacedBy(6_dp), AlignmentV::CenterVertically(), [index] {
                            SmallIcon(index == 0 ? Icons::Filled::Edit() : Icons::Filled::Settings(), 12_dp);
                            Pill(SizeMod({}, 36_dp, 5_dp), Colors().onSurface);
                        }, kiln::rt::Key(index));
                        if (index == 0) m3::HorizontalDivider(Padding({}, 0_dp, 2_dp));
                    }
                });
            });
        }

        void NavigationBarPreview() {
            Panel(SizeMod({}, 96_dp, 36_dp), RoundedCorner(10_dp), Colors().surfaceContainer, [] {
                Row(FillMax({}, Axis::Both), Arrangement::SpaceEvenly(), AlignmentV::CenterVertically(), [] {
                    Panel({}, CircleShape(), Colors().secondaryContainer, [] {
                        m3::Icon(Icons::Filled::Home(), std::nullopt, Size(Padding({}, 8_dp, 3_dp), 14_dp));
                    }, false);
                    SmallIcon(Icons::Filled::Favorite(), 14_dp);
                    SmallIcon(Icons::Filled::Person(), 14_dp);
                });
            });
        }

        void NavigationDrawerPreview() {
            Panel(SizeMod({}, 88_dp, 56_dp), RoundedCorner(10_dp), Colors().surfaceContainerLow, [] {
                Row(FillMax({}, Axis::Both), Arrangement::Start(), AlignmentV::Top(), [] {
                    Panel(FillMax(Width({}, 48_dp), Axis::Vertical), RoundedCorner(CornerSize::Of(0_dp), CornerSize::Of(10_dp), CornerSize::Of(10_dp), CornerSize::Of(0_dp)),
                          Colors().surfaceContainerHigh, [] {
                        Column(Padding({}, 6_dp), Arrangement::SpacedBy(5_dp), AlignmentH::Start(), [] {
                            Pill(Height(FillMax({}, Axis::Horizontal), 10_dp), Colors().secondaryContainer);
                            Pill(Height(FillMax({}, Axis::Horizontal, 0.8f), 6_dp), Colors().outlineVariant);
                        });
                    }, false);
                });
            });
        }

        void NavigationRailPreview() {
            Panel(SizeMod({}, 40_dp, 64_dp), RoundedCorner(10_dp), Colors().surfaceContainer, [] {
                Column(Padding(FillMax({}, Axis::Both), 0_dp, 6_dp), Arrangement::SpaceBetween(), AlignmentH::CenterHorizontally(), [] {
                    Panel({}, CircleShape(), Colors().secondaryContainer, [] {
                        m3::Icon(Icons::Filled::Home(), std::nullopt, Size(Padding({}, 6_dp, 2_dp), 14_dp));
                    }, false);
                    SmallIcon(Icons::Filled::Search(), 14_dp);
                    SmallIcon(Icons::Filled::Settings(), 14_dp);
                });
            });
        }

        void NavigationSuiteScaffoldPreview() {
            Panel(SizeMod({}, 88_dp, 56_dp), RoundedCorner(10_dp), Colors().surfaceContainerLow, [] {
                Row(FillMax({}, Axis::Both), Arrangement::Start(), AlignmentV::Top(), [] {
                    Column(Padding(Background(FillMax(Width({}, 24_dp), Axis::Vertical), Colors().surfaceContainer), 0_dp, 6_dp), Arrangement::SpacedBy(6_dp),
                           AlignmentH::CenterHorizontally(), [] {
                        Pill(Size({}, 10_dp), Colors().primary);
                        Pill(Size({}, 8_dp), Colors().outlineVariant);
                    });
                    Box(Background(Padding(FillMax({}, Axis::Both), 6_dp), Colors().secondaryContainer, RoundedCorner(6_dp)));
                });
            });
        }

        void ProgressIndicatorsPreview() { m3::CircularProgressIndicator([] { return 0.7f; }); }

        void PullToRefreshPreview() {
            Panel(Size({}, 44_dp), CircleShape(), Colors().surfaceContainerHigh, [] {
                Box({}, Alignment::Center(), false, [] {
                    m3::CircularProgressIndicatorOptions o;
                    o.strokeWidth = 3_dp;
                    m3::CircularProgressIndicator([] { return 0.75f; }, Size({}, 24_dp), o);
                });
            }, true, 2_dp);
        }

        void RadioButtonsPreview() {
            Row({}, Arrangement::SpacedBy(4_dp), AlignmentV::Top(), [] {
                m3::RadioButton(true, nullptr);
                m3::RadioButton(false, nullptr);
            });
        }

        void ScrollFieldPreview() {
            Panel(SizeMod({}, 56_dp, 60_dp), RoundedCorner(12_dp), Colors().surfaceContainerHigh, [] {
                Column(FillMax({}, Axis::Both), Arrangement::SpaceEvenly(), AlignmentH::CenterHorizontally(), [] {
                    const auto& t = m3::MaterialTheme::typography();
                    Label("11", t.labelSmall, Alpha({}, 0.4f));
                    Label("12", t.titleMedium, {}, kiln::text::FontWeight::Bold());
                    Label("13", t.labelSmall, Alpha({}, 0.4f));
                });
            });
        }

        void SearchBarsPreview() {
            Panel(SizeMod({}, 96_dp, 38_dp), CircleShape(), Colors().surfaceContainerHigh, [] {
                Row(Padding(FillMax({}, Axis::Both), 12_dp, 0_dp), Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [] {
                    SmallIcon(Icons::Filled::Search(), 18_dp);
                    Pill(SizeMod({}, 40_dp, 6_dp), Colors().outlineVariant);
                });
            });
        }

        void SegmentedButtonsPreview() {
            m3::SingleChoiceSegmentedButtonRow({}, [] {
                m3::SegmentedButtonOptions o;
                o.icon = std::function<void()>([] {});
                m3::SegmentedButton(true, [] {}, m3::SegmentedButtonDefaults::itemShape(0, 2), [] { SmallIcon(Icons::Filled::Check(), 16_dp); }, {}, o);
                m3::SegmentedButton(false, [] {}, m3::SegmentedButtonDefaults::itemShape(1, 2), [] { SmallIcon(Icons::Filled::Star(), 16_dp); }, {}, o);
            });
        }

        void SlidersPreview() {
            auto& state = m3::RememberSliderState(0.65f);
            m3::Slider(state, [](float) {}, Width({}, 96_dp));
        }

        void SnackbarsPreview() {
            m3::SurfaceOptions o;
            o.shape = RoundedCorner(8_dp);
            o.color = Colors().inverseSurface;
            o.contentColor = Colors().inverseOnSurface;
            m3::Surface(SizeMod({}, 96_dp, 36_dp), o, [] {
                Row(Padding(FillMax({}, Axis::Both), 10_dp, 0_dp), Arrangement::SpaceBetween(), AlignmentV::CenterVertically(), [] {
                    Pill(SizeMod({}, 44_dp, 6_dp), Colors().inverseOnSurface);
                    Pill(SizeMod({}, 18_dp, 6_dp), Colors().inversePrimary);
                });
            });
        }

        void SplitButtonsPreview() {
            m3::SplitButtonLayout(
                [] { m3::SplitButtonDefaults::LeadingButton([] {}, {}, {}, [] { SmallIcon(Icons::Filled::Edit(), 18_dp); }); },
                [] { m3::SplitButtonDefaults::TrailingButton(false, [](bool) {}, {}, {}, [] { SmallIcon(Icons::Filled::KeyboardArrowDown(), 18_dp); }); });
        }

        void SwitchesPreview() {
            m3::SwitchOptions o;
            o.thumbContent = [] { m3::Icon(Icons::Filled::Check(), std::nullopt, Size({}, m3::SwitchDefaults::IconSize)); };
            m3::Switch(true, [](bool) {}, {}, o);
        }

        void TabsPreview() {
            m3::SurfaceOptions o;
            o.shape = RoundedCorner(8_dp);
            o.border = BorderStroke(1_dp, Colors().outlineVariant);
            m3::Surface(Width({}, 96_dp), o, [] {
                m3::PrimaryTabRow(0, {}, {}, [] {
                    m3::Tab(true, [] {}, {}, nullptr, [] { SmallIcon(Icons::Filled::Home(), 16_dp); });
                    m3::Tab(false, [] {}, {}, nullptr, [] { SmallIcon(Icons::Filled::Favorite(), 16_dp); });
                });
            });
        }

        void TextFieldsPreview() {
            m3::SurfaceOptions o;
            o.shape = RoundedCorner(8_dp);
            o.color = Colors().surface;
            o.border = BorderStroke(1.5_dp, Colors().primary);
            m3::Surface(SizeMod({}, 96_dp, 40_dp), o, [] {
                Row(Padding(FillMax({}, Axis::Both), 10_dp, 0_dp), Arrangement::Start(), AlignmentV::CenterVertically(),
                    [] { Label("Abc|", m3::MaterialTheme::typography().bodyMedium); });
            });
        }

        void TimePickersPreview() {
            Panel(SizeMod({}, 88_dp, 44_dp), RoundedCorner(10_dp), Colors().surfaceContainerHigh, [] {
                Row(Padding(FillMax({}, Axis::Both), 8_dp, 0_dp), Arrangement::SpaceEvenly(), AlignmentV::CenterVertically(), [] {
                    const auto& t = m3::MaterialTheme::typography();
                    Panel({}, RoundedCorner(6_dp), Colors().primaryContainer, [&t] { Label("10", t.titleSmall, Padding({}, 6_dp, 4_dp)); }, false);
                    Label(":", t.titleSmall, {}, kiln::text::FontWeight::Bold());
                    Panel({}, RoundedCorner(6_dp), Colors().surfaceContainer, [&t] { Label("30", t.titleSmall, Padding({}, 6_dp, 4_dp)); }, false);
                });
            });
        }

        void ToggleButtonsPreview() {
            m3::ToggleButton(true, [](bool) {}, {}, {}, [] {
                m3::Icon(Icons::Filled::Favorite(), std::nullopt, Size({}, m3::ToggleButtonDefaults::IconSize));
                Spacer(Size({}, m3::ToggleButtonDefaults::IconSpacing));
                m3::Text("Like");
            });
        }

        void TooltipsPreview() {
            Column({}, Arrangement::SpacedBy(4_dp), AlignmentH::CenterHorizontally(), [] {
                m3::SurfaceOptions o;
                o.shape = RoundedCorner(6_dp);
                o.color = Colors().inverseSurface;
                o.contentColor = Colors().inverseOnSurface;
                m3::Surface({}, o, [] { Label("Tooltip", m3::MaterialTheme::typography().labelSmall, Padding({}, 8_dp, 4_dp)); });
                Pill(Size({}, 12_dp), Colors().primary);
            });
        }

        void TopAppBarPreview() {
            Panel(SizeMod({}, 96_dp, 38_dp), RoundedCorner(10_dp), Colors().surfaceContainer, [] {
                Row(Padding(FillMax({}, Axis::Both), 8_dp, 0_dp), Arrangement::SpaceBetween(), AlignmentV::CenterVertically(), [] {
                    Row({}, Arrangement::SpacedBy(6_dp), AlignmentV::CenterVertically(), [] {
                        SmallIcon(Icons::Filled::Menu(), 16_dp);
                        Pill(SizeMod({}, 32_dp, 6_dp), Colors().onSurface);
                    });
                    SmallIcon(Icons::Filled::Search(), 16_dp);
                });
            });
        }

        void TransitionsPreview() {
            Box(SizeMod({}, 76_dp, 54_dp), Alignment::TopStart(), false, [] {
                Panel(AlignInBox(SizeMod({}, 52_dp, 40_dp), Alignment::TopStart()), RoundedCorner(10_dp), Colors().secondaryContainer, [] {});
                Panel(OffsetMod(AlignInBox(SizeMod({}, 52_dp, 40_dp), Alignment::BottomEnd()), -2_dp, -2_dp), RoundedCorner(10_dp), Colors().primaryContainer,
                      [] {}, true, 2_dp, Colors().outline);
            });
        }

        void TypographyPreview() { Label("Aa", m3::MaterialTheme::typography().headlineLarge, {}, kiln::text::FontWeight::SemiBold()); }
    }

    std::function<void()> ComponentPreview(const std::string& componentName) {
        static const std::map<std::string, void (*)()> previews{
            { "Adaptive", AdaptivePreview },
            { "Badge", BadgePreview },
            { "Bottom App Bar", BottomAppBarPreview },
            { "Bottom Sheet", BottomSheetPreview },
            { "Buttons", ButtonsPreview },
            { "Button Groups", ButtonGroupsPreview },
            { "Card", CardPreview },
            { "Carousel", CarouselPreview },
            { "Checkboxes", CheckboxesPreview },
            { "Chips", ChipsPreview },
            { "Date pickers", DatePickersPreview },
            { "Dialogs", DialogsPreview },
            { "Extended FAB", ExtendedFabPreview },
            { "Floating action buttons", FloatingActionButtonsPreview },
            { "FAB Menu", FabMenuPreview },
            { "Floating Toolbar", FloatingToolbarsPreview },
            { "Icon buttons", IconButtonsPreview },
            { "Lists", ListsPreview },
            { "Loading indicators", LoadingIndicatorsPreview },
            { "Menus", MenusPreview },
            { "Navigation bar", NavigationBarPreview },
            { "Navigation drawer", NavigationDrawerPreview },
            { "Navigation rail", NavigationRailPreview },
            { "Navigation Suite Scaffold", NavigationSuiteScaffoldPreview },
            { "Progress indicators", ProgressIndicatorsPreview },
            { "Pull-to-Refresh Indicator", PullToRefreshPreview },
            { "Radio buttons", RadioButtonsPreview },
            { "Scroll field", ScrollFieldPreview },
            { "Search bars", SearchBarsPreview },
            { "Segmented Button", SegmentedButtonsPreview },
            { "ToggleButtons", ToggleButtonsPreview },
            { "Sliders", SlidersPreview },
            { "Snackbars", SnackbarsPreview },
            { "Split Button", SplitButtonsPreview },
            { "Switches", SwitchesPreview },
            { "Tabs", TabsPreview },
            { "Text fields", TextFieldsPreview },
            { "Tooltips", TooltipsPreview },
            { "Time Picker", TimePickersPreview },
            { "Top app bar", TopAppBarPreview },
            { "Transitions", TransitionsPreview },
            { "Typography", TypographyPreview },
        };
        auto it = previews.find(componentName);
        if (it == previews.end()) return nullptr;
        return it->second;
    }

}
