// SegmentedButtonSamples.kt
#include "SegmentedButtonSamples.h"
#include "Common.h"

#include <kiln/material3/SegmentedButton.h>

#include <algorithm>

namespace catalog::samples {
    namespace SBD = m3::SegmentedButtonDefaults;

    void SegmentedButtonSingleSelectSample() {
        auto& selectedIndex = RememberSaveableState(0);
        static const char* const options[] = { "Day", "Month", "Week" };
        m3::SingleChoiceSegmentedButtonRow({}, [&selectedIndex] {
            for (int index = 0; index < 3; ++index) {
                m3::SegmentedButton(index == selectedIndex.Get(), [&selectedIndex, index] { selectedIndex.Set(index); }, SBD::itemShape(index, 3),
                                    [index] { m3::Text(options[index]); });
            }
        });
    }

    void SegmentedButtonMultiSelectSample() {
        // rememberSaveable { mutableStateListOf<Int>() }
        auto& checkedList = RememberSaveableState(std::vector<int>{}, kiln::rt::ListSaver<std::vector<int>>(
            [](const std::vector<int>& v) {
                kiln::rt::SavedValue::List l;
                for (int i : v) l.push_back(kiln::rt::SavedValue(std::int64_t(i)));
                return l;
            },
            [](const kiln::rt::SavedValue::List& l) -> std::optional<std::vector<int>> {
                std::vector<int> v;
                for (const auto& s : l)
                    if (auto* i = std::get_if<std::int64_t>(&s.v)) v.push_back(int(*i));
                return v;
            }));
        static const char* const options[] = { "Favorites", "Trending", "Saved" };
        static const ImageVectorPtr* const icons[] = { &Icons::Filled::StarBorder(), &Icons::AutoMirrored::Filled::TrendingUp(),
                                                       &Icons::Filled::BookmarkBorder() };
        m3::MultiChoiceSegmentedButtonRow({}, [&checkedList] {
            for (int index = 0; index < 3; ++index) {
                const auto& list = checkedList.Get();
                const bool checked = std::find(list.begin(), list.end(), index) != list.end();
                m3::SegmentedButtonOptions o;
                o.icon = [checked, index] {
                    // The trailing lambda is inactiveContent: the checkmark stays the active icon.
                    SBD::Icon(checked, SBD::ActiveIcon, [index] { m3::Icon(*icons[index], std::nullopt, Size({}, SBD::IconSize)); });
                };
                m3::SegmentedButton(checked, [&checkedList, index](bool) {
                    auto v = checkedList.Peek();
                    if (auto it = std::find(v.begin(), v.end(), index); it != v.end()) v.erase(it);
                    else v.push_back(index);
                    checkedList.Set(v);
                }, SBD::itemShape(index, 3), [index] { m3::Text(options[index]); }, {}, o);
            }
        });
    }
}
