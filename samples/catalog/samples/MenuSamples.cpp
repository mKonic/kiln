// MenuSamples.kt
#include "MenuSamples.h"
#include <kiln/Function.h>
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/Interaction.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/material3/ButtonGroup.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/Menu.h>

namespace catalog::samples {
    namespace MD = m3::MenuDefaults;

    namespace {
        // The samples' anchor: a More icon button (with its tooltip) opening the menu.
        void MoreButton(kiln::rt::State<bool>& expanded) {
            PlainTooltipAbove("Localized description", [&expanded] {
                m3::IconButton([&expanded] { expanded.Set(true); }, {}, {}, [] { m3::Icon(Icons::Filled::MoreVert(), std::string("Localized description")); });
            });
        }
        void InTopStart(kiln::Function<void()> content) {
            Box(WrapContent(FillMax({}, Axis::Both), Axis::Both, Alignment::TopStart()), Alignment::TopStart(), false, std::move(content));
        }
        kiln::Function<void()> MenuIcon(const ImageVectorPtr* icon, Dp size) {
            if (!icon) return {};
            return [icon, size] { m3::Icon(*icon, std::nullopt, Size({}, size)); };
        }

        void TextSubmenu(std::shared_ptr<MutableInteractionSource> source) {
            auto& boldChecked = RememberState(false);
            auto& italicChecked = RememberState(false);
            auto& underlineChecked = RememberState(false);
            m3::DropdownMenuGroupOptions g;
            g.interactionSource = source;
            m3::DropdownMenuGroup(MD::groupShape(0, 1), {}, g, [&] {
                const auto item = [](kiln::rt::State<bool>& checked, const char* text, int index, const ImageVectorPtr& on, const ImageVectorPtr& off) {
                    m3::MenuItemOptions o;
                    o.trailingContent = [&checked, &on, &off] { m3::Icon(checked.Get() ? on : off, std::nullopt, Size({}, MD::TrailingIconSize())); };
                    m3::CheckableDropdownMenuItem(checked.Get(), [&checked](bool v) { checked.Set(v); }, [text] { m3::Text(text); },
                                                  MD::itemShape(index, 3), {}, o);
                };
                item(boldChecked, "Bold", 0, Icons::Filled::FormatBold(), Icons::Outlined::FormatBold());
                item(italicChecked, "Italic", 1, Icons::Filled::FormatItalic(), Icons::Outlined::FormatItalic());
                item(underlineChecked, "Underline", 2, Icons::Filled::FormatUnderlined(), Icons::Outlined::FormatUnderlined());
            });
        }

        // Align and line spacing: one of four, checked with a leading check mark.
        void SelectSubmenu(std::shared_ptr<MutableInteractionSource> source, const char* const (&labels)[4],
                           const ImageVectorPtr* const (&trailing)[4]) {
            auto& selected = RememberState(0);
            m3::DropdownMenuGroupOptions g;
            g.interactionSource = source;
            m3::DropdownMenuGroup(MD::groupShape(0, 1), {}, g, [&selected, &labels, &trailing] {
                for (int i = 0; i < 4; ++i) {
                    m3::MenuItemOptions o;
                    o.selectedLeadingIcon = MenuIcon(&Icons::Filled::Check(), MD::LeadingIconSize);
                    o.trailingContent = MenuIcon(trailing[i], MD::TrailingIconSize());
                    m3::SelectableDropdownMenuItem(selected.Get() == i, [&selected, i] { selected.Set(i); }, [&labels, i] { m3::Text(labels[i]); },
                                                   MD::itemShape(i, 4), {}, o);
                }
            });
        }
        void AlignSubmenu(std::shared_ptr<MutableInteractionSource> source) {
            static const char* const labels[4] = { "Left", "Center", "Right", "Justify" };
            static const ImageVectorPtr* const trailing[4] = { &Icons::AutoMirrored::Filled::FormatAlignLeft(), &Icons::Filled::FormatAlignCenter(),
                                                               &Icons::AutoMirrored::Filled::FormatAlignRight(), &Icons::Filled::FormatAlignJustify() };
            SelectSubmenu(std::move(source), labels, trailing);
        }
        void LineSpacingSubmenu(std::shared_ptr<MutableInteractionSource> source) {
            static const char* const labels[4] = { "Single", "1.15", "1.5", "Double" };
            static const ImageVectorPtr* const trailing[4] = { nullptr, nullptr, nullptr, nullptr };
            SelectSubmenu(std::move(source), labels, trailing);
        }

        void DropdownMenuButtonGroup() {
            m3::ButtonGroupOptions o;
            o.horizontalArrangement = Arrangement::SpacedBy(2_dp, AlignmentH::CenterHorizontally());
            m3::ButtonGroup([](m3::ButtonGroupMenuState& menuState) { m3::ButtonGroupDefaults::OverflowIndicator(menuState); },
                            FillMax({}, Axis::Horizontal), [](m3::ButtonGroupScope& s) {
                const auto item = [&s](const ImageVectorPtr& icon, const char* label) {
                    s.CustomItem([&icon] {
                        m3::IconButtonOptions b;
                        b.colors = m3::IconButtonDefaults::filledIconButtonColors(MD::groupStandardContainerColor());
                        m3::FilledIconButton([] {}, {}, b, [&icon] { m3::Icon(icon, std::string("Localized description")); });
                    }, [&icon, label](m3::ButtonGroupMenuState&) {
                        m3::DropdownMenuItemOptions d;
                        d.leadingIcon = [&icon] { m3::Icon(icon, std::string("Localized description")); };
                        m3::DropdownMenuItem([label] { m3::Text(label); }, [] {}, {}, d);
                    });
                };
                item(Icons::Filled::ThumbUp(), "Thumbs up");
                item(Icons::Filled::ThumbDown(), "Thumbs down");
                item(Icons::Filled::TagFaces(), "Emotes");
            }, o);
        }
    }

    void MenuSample() {
        auto& expanded = RememberState(false);
        InTopStart([&expanded] {
            MoreButton(expanded);
            m3::DropdownMenu(expanded.Get(), [&expanded] { expanded.Set(false); }, {}, {}, [] {
                const auto leading = [](const ImageVectorPtr& icon) { return [&icon] { m3::Icon(icon, std::nullopt); }; };
                m3::DropdownMenuItemOptions edit, settings, feedback;
                edit.leadingIcon = leading(Icons::Outlined::Edit());
                settings.leadingIcon = leading(Icons::Outlined::Settings());
                feedback.leadingIcon = leading(Icons::Outlined::Email());
                feedback.trailingIcon = [] {
                    m3::TextOptions t;
                    t.textAlign = kiln::text::TextAlign::Center;
                    m3::Text("F11", {}, t);
                };
                m3::DropdownMenuItem([] { m3::Text("Edit"); }, [] {}, {}, edit);
                m3::DropdownMenuItem([] { m3::Text("Settings"); }, [] {}, {}, settings);
                m3::HorizontalDivider();
                m3::DropdownMenuItem([] { m3::Text("Send Feedback"); }, [] {}, {}, feedback);
            });
        });
    }

    void GroupedMenuSample() {
        auto groupInteractionSource = RememberInteractionSource();
        auto& expanded = RememberSaveableState(false);
        static const char* const groupLabels[2] = { "Modification", "Navigation" };
        static const char* const groupItemLabels[2][2] = { { "Edit", "Settings" }, { "Home", "More Options" } };
        static const ImageVectorPtr* const groupItemLeadingIcons[2][2] = { { &Icons::Outlined::Edit(), &Icons::Outlined::Settings() },
                                                                           { nullptr, &Icons::Outlined::Info() } };
        static const ImageVectorPtr* const groupItemCheckedLeadingIcons[2][2] = { { &Icons::Filled::Edit(), &Icons::Filled::Settings() },
                                                                                  { &Icons::Filled::Check(), &Icons::Filled::Info() } };
        static const ImageVectorPtr* const groupItemTrailingIcons[2][2] = { { nullptr, nullptr }, { &Icons::Outlined::Home(), &Icons::Outlined::MoreVert() } };
        static const ImageVectorPtr* const groupItemCheckedTrailingIcons[2][2] = { { nullptr, nullptr },
                                                                                   { &Icons::Filled::Home(), &Icons::Filled::MoreVert() } };
        static const char* const groupItemSupportingText[2][2] = { { "Edit mode", nullptr }, { nullptr, "Opens menu" } };
        // rememberSaveable(listSaver) { listOf(mutableStateListOf(false, false), ...) }
        auto& checked = RememberSaveableState(std::vector<bool>(4, false), kiln::rt::ListSaver<std::vector<bool>>(
            [](const std::vector<bool>& v) {
                kiln::rt::SavedValue::List l;
                for (bool b : v) l.push_back(kiln::rt::SavedValue(b));
                return l;
            },
            [](const kiln::rt::SavedValue::List& l) -> std::optional<std::vector<bool>> {
                std::vector<bool> v;
                for (const auto& s : l)
                    if (auto* b = std::get_if<bool>(&s.v)) v.push_back(*b);
                return v.size() == 4 ? std::optional(v) : std::nullopt;
            }));
        InTopStart([&expanded, &checked, groupInteractionSource] {
            MoreButton(expanded);
            m3::DropdownMenuPopup(expanded.Get(), [&expanded] { expanded.Set(false); }, {}, nullptr, [&checked, groupInteractionSource] {
                constexpr int groupCount = 2;
                for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
                    m3::DropdownMenuGroupOptions g;
                    g.interactionSource = groupInteractionSource;
                    Here().Child([&checked, groupIndex, g] { m3::DropdownMenuGroup(MD::groupShape(groupIndex, groupCount), {}, g, [&checked, groupIndex] {
                        MD::DropdownMenuGroupLabel({}, [groupIndex] { m3::Text(groupLabels[groupIndex]); });
                        m3::HorizontalDivider(Padding({}, MD::HorizontalDividerPadding));
                        for (int itemIndex = 0; itemIndex < 2; ++itemIndex) {
                            const bool isChecked = checked.Get()[std::size_t(groupIndex * 2 + itemIndex)];
                            m3::MenuItemOptions o;
                            if (const char* s = groupItemSupportingText[groupIndex][itemIndex]) o.supportingText = [s] { m3::Text(s); };
                            o.leadingIcon = MenuIcon(groupItemLeadingIcons[groupIndex][itemIndex], MD::LeadingIconSize);
                            o.selectedLeadingIcon = MenuIcon(groupItemCheckedLeadingIcons[groupIndex][itemIndex], MD::LeadingIconSize);
                            o.trailingContent = MenuIcon(isChecked ? groupItemCheckedTrailingIcons[groupIndex][itemIndex]
                                                                   : groupItemTrailingIcons[groupIndex][itemIndex],
                                                         MD::TrailingIconSize());
                            m3::CheckableDropdownMenuItem(isChecked, [&checked, groupIndex, itemIndex](bool v) {
                                auto next = checked.Peek();
                                next[std::size_t(groupIndex * 2 + itemIndex)] = v;
                                checked.Set(next);
                            }, [groupIndex, itemIndex] { m3::Text(groupItemLabels[groupIndex][itemIndex]); }, MD::itemShape(itemIndex, 2), {}, o);
                        }
                    }); }, kiln::rt::Key(groupIndex));
                    if (groupIndex != groupCount - 1) Spacer(Height({}, MD::GroupSpacing));
                }
                if (checked.Get().back()) DropdownMenuButtonGroup();
            });
        });
    }

    void MenuWithCascadingMenusSample() {
        auto groupInteractionSource = RememberInteractionSource();
        auto& expanded = RememberState(false);
        static const char* const groupItemLabels[3] = { "Text", "Align", "Line spacing" };
        static const ImageVectorPtr* const mainGroupItemLeadingIcons[3] = { &Icons::Filled::FormatBold(), &Icons::AutoMirrored::Filled::FormatAlignLeft(),
                                                                           &Icons::Filled::FormatLineSpacing() };
        static void (*const submenus[3])(std::shared_ptr<MutableInteractionSource>) = { TextSubmenu, AlignSubmenu, LineSpacingSubmenu };
        InTopStart([&expanded, groupInteractionSource] {
            MoreButton(expanded);
            m3::DropdownMenuPopup(expanded.Get(), [&expanded] { expanded.Set(false); }, {}, nullptr, [groupInteractionSource] {
                m3::DropdownMenuGroupOptions g;
                g.interactionSource = groupInteractionSource;
                m3::DropdownMenuGroup(MD::groupShape(0, 1), {}, g, [] {
                    constexpr int groupItemCount = 3;
                    for (int itemIndex = 0; itemIndex < groupItemCount; ++itemIndex) {
                        Box({}, Alignment::TopStart(), false, [itemIndex] {
                            auto itemInteractionSource = RememberInteractionSource();
                            const bool itemHovered = itemInteractionSource->IsHovered();
                            auto& itemChecked = RememberState(false);
                            m3::MenuItemOptions o;
                            o.interactionSource = itemInteractionSource;
                            o.leadingIcon = MenuIcon(mainGroupItemLeadingIcons[itemIndex], MD::LeadingIconSize);
                            o.trailingContent = MenuIcon(&Icons::AutoMirrored::Filled::ArrowRight(), MD::TrailingIconSize());
                            m3::DropdownMenuItem([&itemChecked] { itemChecked.Set(!itemChecked.Peek()); }, [itemIndex] { m3::Text(groupItemLabels[itemIndex]); },
                                                 itemIndex == 0 ? MD::leadingItemShape() : itemIndex == groupItemCount - 1 ? MD::trailingItemShape() : MD::middleItemShape(),
                                                 {}, o);
                            kiln::ui::PopupProperties properties;
                            properties.focusable = false;
                            m3::DropdownMenuPopup(itemChecked.Get() || itemHovered, [&itemChecked] { itemChecked.Set(false); }, {},
                                                  MD::rememberDropdownMenuPopupPositionProvider(m3::MenuAnchorPosition::End()),
                                                  [itemIndex, itemInteractionSource] { submenus[itemIndex](itemInteractionSource); }, properties);
                        }, kiln::rt::Key(itemIndex));
                    }
                });
            });
        });
    }

    void MenuWithScrollStateSample() {
        auto& expanded = RememberState(false);
        auto& scrollState = RememberScrollState();
        InTopStart([&expanded, &scrollState] {
            MoreButton(expanded);
            m3::DropdownMenuOptions o;
            o.scrollState = &scrollState;
            m3::DropdownMenu(expanded.Get(), [&expanded] { expanded.Set(false); }, {}, o, [] {
                for (int it = 0; it < 30; ++it) {
                    m3::DropdownMenuItemOptions d;
                    d.leadingIcon = [] { m3::Icon(Icons::Outlined::Edit(), std::nullopt); };
                    m3::DropdownMenuItem([it] { m3::Text("Item " + std::to_string(it + 1)); }, [] {}, {}, d);
                }
            });
            Here().LaunchedEffect(kiln::rt::Keys(expanded.Get()), [&scrollState, open = expanded.Get()]() -> kiln::rt::Task<> {
                // Scroll to show the bottom menu items.
                if (open) scrollState.ScrollTo(scrollState.MaxValue());
                co_return;
            });
        });
    }
}
