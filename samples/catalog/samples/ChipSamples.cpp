// ChipSamples.kt
#include "ChipSamples.h"
#include "Common.h"

#include <kiln/foundation/Flow.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/material3/Chip.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/Menu.h>

namespace catalog::samples {
    namespace FCD = m3::FilterChipDefaults;

    namespace {
        std::function<void()> ChipIcon(const ImageVectorPtr& icon, const char* description, Dp size) {
            return [&icon, description, size] { m3::Icon(icon, std::string(description), Size({}, size)); };
        }
        std::function<void()> Label(const char* text) {
            return [text] { m3::Text(text); };
        }

        using SelectableFn = void (*)(bool, std::function<void()>, std::function<void()>, const Modifier&, const m3::SelectableChipOptions&);

        // The filter chip samples: Done while selected; `unselectedHome` puts Home there otherwise.
        void Filter(SelectableFn chip, bool morphing, bool unselectedHome, bool trailing, std::optional<Dp> spacing = std::nullopt) {
            auto& selected = RememberSaveableState(false);
            m3::SelectableChipOptions o;
            if (morphing) o.shapes = FCD::shapes();
            if (selected.Get()) o.leadingIcon = ChipIcon(Icons::Filled::Done(), "Localized Description", FCD::IconSize);
            else if (unselectedHome) o.leadingIcon = ChipIcon(Icons::Filled::Home(), "Localized description", FCD::IconSize);
            if (trailing) o.trailingIcon = ChipIcon(Icons::Filled::ArrowDropDown(), "Localized Description", FCD::IconSize);
            if (spacing) o.horizontalArrangement = FCD::horizontalArrangement(*spacing);
            chip(selected.Get(), [&selected] { selected.Set(!selected.Peek()); }, Label("Filter chip"), {}, o);
        }

        void Input(bool avatar, bool morphing) {
            auto& selected = RememberSaveableState(false);
            m3::SelectableChipOptions o;
            if (morphing) o.shapes = m3::InputChipDefaults::shapes();
            if (avatar) o.avatar = ChipIcon(Icons::Filled::Person(), "Localized description", m3::InputChipDefaults::AvatarSize);
            m3::InputChip(selected.Get(), [&selected] { selected.Set(!selected.Peek()); }, Label("Input Chip"), {}, o);
        }
    }

    void AssistChipSample() {
        m3::ChipOptions o;
        o.leadingIcon = ChipIcon(Icons::Filled::Settings(), "Localized description", m3::AssistChipDefaults::IconSize);
        m3::AssistChip([] {}, Label("Assist Chip"), {}, o);
    }
    void ElevatedAssistChipSample() {
        m3::ChipOptions o;
        o.leadingIcon = ChipIcon(Icons::Filled::Settings(), "Localized description", m3::AssistChipDefaults::IconSize);
        m3::ElevatedAssistChip([] {}, Label("Assist Chip"), {}, o);
    }

    void FilterChipSample() { Filter(m3::FilterChip, false, false, false); }
    void FilterChipWithCornerMorphingSample() { Filter(m3::FilterChip, true, false, false); }
    void ElevatedFilterChipSample() { Filter(m3::ElevatedFilterChip, false, false, false); }
    void ElevatedFilterChipWithCornerMorphingSample() { Filter(m3::ElevatedFilterChip, true, true, true); }
    void FilterChipWithLeadingIconSample() { Filter(m3::FilterChip, false, true, false); }
    void FilterChipWithTrailingIconSample() { Filter(m3::FilterChip, false, false, true); }
    void FilterChipWithCustomSpacingSample() { Filter(m3::FilterChip, false, false, false, 4_dp); }

    void InputChipSample() { Input(false, false); }
    void InputChipWithAvatarSample() { Input(true, false); }
    void InputChipWithCornerMorphingSample() { Input(true, true); }

    void SuggestionChipSample() { m3::SuggestionChip([] {}, Label("Suggestion Chip")); }
    void ElevatedSuggestionChipSample() { m3::ElevatedSuggestionChip([] {}, Label("Suggestion Chip")); }

    void ChipGroupSingleLineSample() {
        auto& expanded = RememberSaveableState(false);
        constexpr int listSize = 9;
        Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), [&expanded] {
            Row(HorizontalScroll({}, RememberScrollState()), Arrangement::Start(), AlignmentV::Top(), [&expanded] {
                m3::ChipOptions show;
                show.leadingIcon = ChipIcon(Icons::Filled::Tune(), "Localized Description", FCD::IconSize);
                m3::AssistChip([&expanded] { expanded.Set(!expanded.Peek()); }, Label("Show All"), {}, show);
                // When chip lists exceed the available horizontal screen space, one option is to
                // provide a chip button that opens a menu displaying all chip options. This ensures
                // all options are accessible while maintaining the position of the content below the
                // chip list.
                m3::DropdownMenu(expanded.Get(), [&expanded] { expanded.Set(false); }, {}, {}, [] {
                    for (int index = 0; index < listSize; ++index) {
                        m3::DropdownMenuItemOptions o;
                        o.trailingIcon = [] { m3::Icon(Icons::AutoMirrored::Filled::ArrowRight(), std::nullopt); };
                        m3::DropdownMenuItem([index] { m3::Text("Chip " + std::to_string(index)); }, [] {}, {}, o);
                    }
                });
                for (int index = 0; index < listSize; ++index) {
                    m3::ChipOptions o;
                    o.trailingIcon = [] { m3::Icon(Icons::Filled::ArrowDropDown(), std::nullopt); };
                    m3::AssistChip([] {}, [index] { m3::Text("Chip " + std::to_string(index)); }, Padding({}, 4_dp, 0_dp), o);
                }
            });
        });
    }

    void ChipGroupReflowSample() {
        auto& selected = RememberSaveableState(false);
        static const char* const colorNames[] = { "Blue", "Yellow", "Red", "Orange", "Black", "Green", "White", "Magenta", "Gray", "Transparent" };
        Column({}, Arrangement::Top(), AlignmentH::Start(), [&selected] {
            // One remembered state per branch, as each branch's rememberScrollState().
            auto& vertical = RememberScrollState();
            auto& horizontal = RememberScrollState();
            Modifier m = WrapContent(FillMax({}, Axis::Horizontal, 1), Axis::Vertical, Alignment::TopCenter());
            m = selected.Get() ? VerticalScroll(m, vertical) : HorizontalScroll(m, horizontal);
            FlowOptions flow;
            flow.maxLines = selected.Get() ? INT_MAX : 1;
            FlowRow(m, Arrangement::Start(), Arrangement::Top(), AlignmentV::Top(), [&selected] {
                // When chip lists exceed the available horizontal screen space, one option is to
                // provide a leading chip that expands the list into a vertical scrolling list. This
                // ensures all options are accessible while maintaining the position of the content
                // below the chip list.
                m3::SelectableChipOptions show;
                show.leadingIcon = ChipIcon(Icons::Filled::Tune(), "Localized Description", FCD::IconSize);
                m3::FilterChip(selected.Get(), [&selected] { selected.Set(!selected.Peek()); }, Label("Show All"),
                               AlignInRow(Padding({}, 4_dp, 0_dp), AlignmentV::CenterVertically()), show);
                Box(AlignInRow(Height({}, FCD::Height), AlignmentV::CenterVertically()), Alignment::TopStart(), false, [] { m3::VerticalDivider(); });
                int index = 0;
                for (const char* element : colorNames) {
                    m3::AssistChip([] {}, [element, index] { m3::Text(std::string(element) + " " + std::to_string(index)); },
                                   AlignInRow(Padding({}, 4_dp, 0_dp), AlignmentV::CenterVertically()), {});
                    ++index;
                }
            }, flow);
        });
    }
}
