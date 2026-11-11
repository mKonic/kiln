#pragma once

// ScrollField.kt: a field whose value is picked by scrolling an endless wheel of numbers
// 0..itemCount-1 (the current one centred, larger and emphasised).

#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/Pager.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Modifier.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace kiln::material3 {

    class ScrollFieldState {
    public:
        ScrollFieldState(foundation::PagerState& pagerState, int itemCount);
        int ItemCount() const { return m_ItemCount; }
        // The centred option; setting it jumps there (without stopping a scroll).
        int SelectedOption() const;
        void SetSelectedOption(int option);
        rt::Task<> ScrollToOption(int option);
        rt::Task<> AnimateScrollToOption(int option);
        // Where a scroll in progress is heading.
        int TargetOption() const;
        bool IsScrollInProgress() const { return m_Pager->IsScrollInProgress(); }
        foundation::PagerState& Pager() const { return *m_Pager; }

    private:
        int TargetPage(int option) const;   // the page of `option` nearest the current one
        foundation::PagerState* m_Pager;
        int m_ItemCount;
    };

    // rememberScrollFieldState: starts on `index` (clamped to the items).
    std::shared_ptr<ScrollFieldState> RememberScrollFieldState(int itemCount, int index = 0, rt::Key key = {});

    struct ScrollFieldColors {
        ui::Color containerColor, contentColor, selectedContentColor, disabledContainerColor, disabledContentColor,
            disabledSelectedContentColor;
        bool operator==(const ScrollFieldColors&) const = default;
    };

    namespace ScrollFieldDefaults {
        inline constexpr ui::Dp ScrollFieldHeight{ 120 };
        inline constexpr ui::Dp ItemHeight{ 52 };
        foundation::ShapePtr shape();
        ScrollFieldColors colors();
        // The number, in two digits, growing to displayLargeEmphasized as it is selected.
        void Item(int index, bool selected, bool enabled = true, std::optional<ScrollFieldColors> colors = std::nullopt);
    }

    struct ScrollFieldOptions {
        bool enabled = true;
        std::optional<ScrollFieldColors> colors;
        // The state description of an option (default: its number in the locale's digits).
        std::function<std::string(int index)> fieldAccessibilityDescription;
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        // An item's content (default: ScrollFieldDefaults::Item).
        std::function<void(int index, bool selected, bool enabled)> field;
        ui::Dp itemHeight = ScrollFieldDefaults::ItemHeight;   // internal (the time picker's)
    };
    void ScrollField(ScrollFieldState& state, std::optional<std::string> contentDescription, const ui::Modifier& modifier = {},
                     const ScrollFieldOptions& options = {});

}
