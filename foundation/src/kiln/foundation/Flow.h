#pragma once

// FlowRow / FlowColumn (FlowLayout.kt, Clip overflow): items fill a line along the main axis and
// wrap to the next; lines are laid out like a Row (Column) at least as long as the longest line,
// and arranged along the cross axis. Weighted items share a line's leftover space.

#include "kiln/foundation/Arrangement.h"
#include "kiln/ui/LayoutNode.h"

#include <climits>
#include <functional>

namespace kiln::foundation {

    using ui::Modifier;

    struct FlowOptions {
        int maxItemsInEachLine = INT_MAX;
        int maxLines = INT_MAX;
    };

    void FlowRow(const Modifier& modifier, Arrangement horizontal, Arrangement vertical, AlignmentV itemAlignment,
                 std::function<void()> content, FlowOptions options = {}, rt::Key key = {});
    void FlowColumn(const Modifier& modifier, Arrangement vertical, Arrangement horizontal, AlignmentH itemAlignment,
                    std::function<void()> content, FlowOptions options = {}, rt::Key key = {});

    class FlowPolicy final : public ui::MeasurePolicy {
    public:
        FlowPolicy(bool row, Arrangement main, Arrangement cross, float itemBias, FlowOptions o)
            : m_Row(row), m_Main(main), m_Cross(cross), m_ItemBias(itemBias), m_Options(o) {}
        ui::MeasureResult Measure(ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) override;
        bool operator==(const FlowPolicy& o) const {
            return m_Row == o.m_Row && m_Main == o.m_Main && m_Cross == o.m_Cross && m_ItemBias == o.m_ItemBias &&
                   m_Options.maxItemsInEachLine == o.m_Options.maxItemsInEachLine && m_Options.maxLines == o.m_Options.maxLines;
        }
    private:
        bool m_Row;
        Arrangement m_Main, m_Cross;
        float m_ItemBias;
        FlowOptions m_Options;
    };

}
