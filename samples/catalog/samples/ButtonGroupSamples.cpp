// ButtonGroupSamples.kt
#include "ButtonGroupSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/foundation/Flow.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/ButtonGroup.h>
#include <kiln/material3/Menu.h>
#include <kiln/material3/ToggleButton.h>

namespace catalog::samples {
    namespace GD = m3::ButtonGroupDefaults;

    namespace {
        // Leading, middle or trailing connected shapes by position.
        m3::ToggleButtonShapes ConnectedShapes(int index, int lastIndex) {
            if (index == 0) return GD::connectedLeadingButtonShapes();
            if (index == lastIndex) return GD::connectedTrailingButtonShapes();
            return GD::connectedMiddleButtonShapes();
        }

        const char* const options5[5] = { "Work", "Restaurant", "Coffee", "Search", "Home" };
        const ImageVectorPtr* UncheckedIcon(int i) {
            static const ImageVectorPtr* const icons[5] = { &Icons::Outlined::Work(), &Icons::Outlined::Restaurant(), &Icons::Outlined::Coffee(),
                                                            &Icons::Outlined::Search(), &Icons::Outlined::Home() };
            return icons[i];
        }
        const ImageVectorPtr* CheckedIcon(int i) {
            static const ImageVectorPtr* const icons[5] = { &Icons::Filled::Work(), &Icons::Filled::Restaurant(), &Icons::Filled::Coffee(),
                                                            &Icons::Filled::Search(), &Icons::Filled::Home() };
            return icons[i];
        }

        // The connected FlowRow samples: five toggle buttons with icons, wrapping as needed.
        void ConnectedRow(kiln::Function<bool(int)> isChecked, kiln::Function<void(int, bool)> onChange, bool radio) {
            FlowRow(FillMax(Padding({}, 8_dp, 0_dp), Axis::Horizontal), Arrangement::SpacedBy(GD::ConnectedSpaceBetween), Arrangement::SpacedBy(2_dp),
                    AlignmentV::Top(), [isChecked, onChange, radio] {
                        for (int index = 0; index < 5; ++index) {
                            const bool checked = isChecked(index);
                            m3::ToggleButtonOptions o;
                            o.shapes = ConnectedShapes(index, 4);
                            const Modifier m = radio ? Semantics({}, [](SemanticsConfiguration& c) { c.role = Role::RadioButton; }) : Modifier{};
                            m3::ToggleButton(checked, [onChange, index](bool v) { onChange(index, v); }, m, o, [checked, index] {
                                m3::Icon(*(checked ? CheckedIcon(index) : UncheckedIcon(index)), std::string("Localized description"));
                                Spacer(Size({}, m3::ToggleButtonDefaults::IconSpacing));
                                m3::Text(options5[index]);
                            });
                        }
                    });
        }
    }

    void ButtonGroupSample() {
        constexpr int numButtons = 10;
        m3::ButtonGroupOptions o;
        o.verticalAlignment = AlignmentV::Top();
        m3::ButtonGroup([](m3::ButtonGroupMenuState& menuState) { GD::OverflowIndicator(menuState); }, {}, [](m3::ButtonGroupScope& scope) {
            for (int i = 0; i < numButtons; ++i) scope.ClickableItem([] {}, std::to_string(i));
        }, o);
    }

    void ButtonGroupWithCustomItemSample() {
        static const char* const options[3] = { "Work", "Restaurant", "Home" };
        static const ImageVectorPtr* const unCheckedIcons[3] = { &Icons::Outlined::Work(), &Icons::Outlined::Restaurant(), &Icons::Outlined::Home() };
        static const ImageVectorPtr* const checkedIcons[3] = { &Icons::Filled::Work(), &Icons::Filled::Restaurant(), &Icons::Filled::Home() };
        auto& checked = RememberSaveableList(std::vector<bool>{ false, false, false });
        auto& interactionSources = Here().Remember<std::vector<std::shared_ptr<MutableInteractionSource>>>([] {
            return std::vector<std::shared_ptr<MutableInteractionSource>>{ std::make_shared<MutableInteractionSource>(),
                                                                           std::make_shared<MutableInteractionSource>(),
                                                                           std::make_shared<MutableInteractionSource>() };
        });
        m3::ButtonGroupOptions o;
        o.expandedRatio = 1;
        m3::ButtonGroup([](m3::ButtonGroupMenuState& menuState) { GD::OverflowIndicator(menuState); }, {}, [&checked, &interactionSources](m3::ButtonGroupScope& scope) {
            for (int index = 0; index < 3; ++index) {
                const auto source = interactionSources[std::size_t(index)];
                const std::string label = options[index];
                scope.CustomItem(
                    [&checked, &scope, source, label, index] {
                        const PaddingValues contentPadding = m3::ButtonDefaults::ButtonWithIconContentPadding;
                        const bool c = checked.Get()[std::size_t(index)];
                        m3::ToggleButtonOptions t;
                        t.shapes = ConnectedShapes(index, 2);
                        t.contentPadding = contentPadding;
                        t.interactionSource = source;
                        m3::ToggleButton(c, [&checked, index](bool v) {
                            auto list = checked.Peek();
                            list[std::size_t(index)] = v;
                            checked.Set(list);
                        }, scope.AnimateWidth({}, source, contentPadding.end), t, [c, label, index] {
                            m3::Icon(*(c ? checkedIcons[index] : unCheckedIcons[index]), std::string("Localized description"));
                            Spacer(Size({}, m3::ToggleButtonDefaults::IconSpacing));
                            m3::TextOptions text;
                            text.softWrap = false;
                            text.maxLines = 1;
                            text.overflow = kiln::text::TextOverflow::Visible;
                            m3::Text(label, {}, text);
                        });
                    },
                    [source, label](m3::ButtonGroupMenuState&) {
                        m3::DropdownMenuItemOptions d;
                        d.leadingIcon = [] {};   // as upstream: the lambda only names the icon, drawing nothing
                        d.interactionSource = source;
                        m3::DropdownMenuItem([label] { m3::Text(label); }, [] {}, {}, d);
                    });
            }
        }, o);
    }

    void SingleSelectConnectedButtonGroupSample() {
        auto& selectedIndex = RememberSaveableState(0);
        ConnectedRow([&selectedIndex](int i) { return selectedIndex.Get() == i; }, [&selectedIndex](int i, bool) { selectedIndex.Set(i); }, true);
    }

    void MultiSelectConnectedButtonGroupSample() {
        auto& checked = RememberSaveableList(std::vector<bool>(5, false));
        ConnectedRow([&checked](int i) { return bool(checked.Get()[std::size_t(i)]); }, [&checked](int i, bool v) {
            auto list = checked.Peek();
            list[std::size_t(i)] = v;
            checked.Set(list);
        }, false);
    }

    void VerticalButtonGroupSample() {
        static const char* const options[5] = { "Button 1", "Button 2", "Button 3", "Button 4", "Button 5" };
        auto& selectedIndex = RememberSaveableState(0);
        Column({}, Arrangement::SpacedBy(Dp(-6)), AlignmentH::Start(), [&selectedIndex] {
            for (int index = 0; index < 5; ++index) {
                ShapePtr shape = GD::connectedMiddleButtonShapes().shape;
                const auto& rounded = static_cast<const CornerBasedShape&>(*shape);
                if (index == 0) shape = rounded.Copy(CornerSize::Percent(100), CornerSize::Percent(100), rounded.bottomEnd, rounded.bottomStart);
                else if (index == 4) shape = rounded.Copy(rounded.topStart, rounded.topEnd, CornerSize::Percent(100), CornerSize::Percent(100));
                m3::ToggleButtonOptions o;
                o.shapes = m3::ToggleButtonShapes{ shape, m3::ToggleButtonDefaults::pressedShape(), GD::connectedButtonCheckedShape() };
                m3::ToggleButton(selectedIndex.Get() == index, [&selectedIndex, index](bool) { selectedIndex.Set(index); },
                                 Semantics({}, [](SemanticsConfiguration& c) { c.role = Role::RadioButton; }), o, [index] { m3::Text(options[index]); });
            }
        });
    }
}
