#pragma once

// FlexBox (foundation-layout FlexBox.kt): CSS flexbox. Items line up along a main axis (the
// direction), optionally wrap into several lines, grow into free space and shrink when there is
// too little, by their flex factors. The container's config and each item's flex config are
// functions run at measure time, so they can read the constraints (responsive layouts):
//
//   FlexBox(Modifier{}, [](FlexBoxConfigScope& s) {
//       s.Wrap(FlexWrap::Wrap);
//       s.Gap(8_dp);
//       if (s.constraints.maxWidth < s.RoundToPx(600_dp)) s.Direction(FlexDirection::Column);
//   }, [] {
//       Box(Flex(Modifier{}, [](FlexConfigScope& f) { f.Grow(1); f.Basis(120_dp); }), ...);
//   });
//
// An item without a grow factor grows by its FillMax fraction along the main axis.

#include "kiln/foundation/LayoutConfig.h"
#include <kiln/Function.h>
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Layout.h"
#include "kiln/ui/Modifier.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace kiln::foundation {

    using ui::Constraints;
    using ui::Dp;
    using ui::Modifier;

    enum class FlexDirection : std::uint8_t { Row, Column, RowReverse, ColumnReverse };
    enum class FlexWrap : std::uint8_t { NoWrap, Wrap, WrapReverse };
    enum class FlexJustifyContent : std::uint8_t { Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly };
    enum class FlexAlignItems : std::uint8_t { Start, End, Center, Stretch, Baseline };
    enum class FlexAlignSelf : std::uint8_t { Auto, Start, End, Center, Stretch, Baseline };
    enum class FlexAlignContent : std::uint8_t { Start, End, Center, Stretch, SpaceBetween, SpaceAround };

    // FlexBasis: an item's size before growing or shrinking: its max-content size (Auto), a
    // size, or a fraction of the container's main axis.
    struct FlexBasis {
        enum class Kind : std::uint8_t { Auto, Dp, Percent };
        Kind kind = Kind::Auto;
        float value = 0;
        static FlexBasis Auto() { return {}; }
        static FlexBasis Of(ui::Dp d) { return { Kind::Dp, d.value }; }
        static FlexBasis Percent(float fraction) { return { Kind::Percent, fraction }; }
        bool operator==(const FlexBasis&) const = default;
    };

    // FlexBoxConfigScope: the container's settings, with the incoming constraints to read.
    class FlexBoxConfigScope : public ui::Density {
    public:
        Constraints constraints;
        void Direction(FlexDirection v) { direction = v; }
        void Wrap(FlexWrap v) { wrap = v; }
        void JustifyContent(FlexJustifyContent v) { justifyContent = v; }
        void AlignItems(FlexAlignItems v) { alignItems = v; }
        // Baseline alignment on `line`, or on a position each item gives.
        void AlignItems(const ui::AlignmentLine& line) { alignItems = FlexAlignItems::Baseline; baselineLine = &line; baselineBlock = nullptr; }
        void AlignItems(kiln::Function<int(ui::Placeable&)> block) { alignItems = FlexAlignItems::Baseline; baselineLine = nullptr; baselineBlock = std::move(block); }
        void AlignContent(FlexAlignContent v) { alignContent = v; }
        void MaxItemsInEachLine(int v);   // > 0
        void RowGap(Dp v) { rowGap = v; }
        void ColumnGap(Dp v) { columnGap = v; }
        void Gap(Dp all) { rowGap = columnGap = all; }
        void Gap(Dp row, Dp column) { rowGap = row; columnGap = column; }

        FlexDirection direction = FlexDirection::Row;
        FlexWrap wrap = FlexWrap::NoWrap;
        FlexJustifyContent justifyContent = FlexJustifyContent::Start;
        FlexAlignItems alignItems = FlexAlignItems::Start;
        FlexAlignContent alignContent = FlexAlignContent::Start;
        int maxItemsInEachLine = INT32_MAX;
        Dp rowGap{ 0 }, columnGap{ 0 };
        const ui::AlignmentLine* baselineLine = nullptr;
        kiln::Function<int(ui::Placeable&)> baselineBlock;
    };
    using FlexBoxConfig = LayoutConfig<FlexBoxConfigScope>;

    // FlexConfigScope: an item's settings, with the container's constraints along its axes.
    class FlexConfigScope : public ui::Density {
    public:
        int flexBoxMainAxisMaxPx = 0, flexBoxMainAxisMinPx = 0, flexBoxCrossAxisMaxPx = 0, flexBoxCrossAxisMinPx = 0;
        void AlignSelf(FlexAlignSelf v) { alignSelf = v; }
        void AlignSelf(const ui::AlignmentLine& line) { alignSelf = FlexAlignSelf::Baseline; baselineLine = &line; baselineBlock = nullptr; }
        void AlignSelf(kiln::Function<int(ui::Placeable&)> block) { alignSelf = FlexAlignSelf::Baseline; baselineLine = nullptr; baselineBlock = std::move(block); }
        // Placement order among the items (stable; default 0).
        void Order(int v) { order = v; }
        void Grow(float v);     // >= 0
        void Shrink(float v);   // >= 0 (default 1)
        void Basis(FlexBasis v) { basis = v; }
        void Basis(Dp v) { basis = FlexBasis::Of(v); }
        void Basis(float fraction) { basis = FlexBasis::Percent(fraction); }

        FlexAlignSelf alignSelf = FlexAlignSelf::Auto;
        int order = 0;
        float grow = NAN;   // unset: the FillMax fraction along the main axis
        float shrink = 1;
        FlexBasis basis;
        const ui::AlignmentLine* baselineLine = nullptr;
        kiln::Function<int(ui::Placeable&)> baselineBlock;
    };
    using FlexConfig = LayoutConfig<FlexConfigScope>;

    // FlexBox: lays its children out as a flex container.
    void FlexBox(const Modifier& modifier, FlexBoxConfig config, kiln::Function<void()> content);
    inline void FlexBox(const Modifier& modifier, kiln::Function<void()> content) { FlexBox(modifier, FlexBoxConfig{}, std::move(content)); }

    // FlexBoxScope.flex: this child's flex settings.
    Modifier Flex(Modifier m, FlexConfig config);

}
