#pragma once

// Grid (foundation-layout Grid.kt): CSS grid. The config lists the column and row tracks (fixed,
// a fraction of the space, a flexible share, content-sized, or a minimum with a share); items
// go to explicit cells, named areas or the next free cell along the flow, spanning several
// tracks if asked. Rows and columns past the explicit ones are added as Auto tracks.
//
//   Grid([](GridConfigurationScope& g) {
//       g.Column(Dp(80));
//       g.Column(Fr(1));
//       g.Gap(Dp(8));
//   }, Modifier{}, [] {
//       Text(...);                                     // auto-placed: row 1, column 1
//       Box(GridItem(Modifier{}, 2, 1, 1, 2), ...);    // row 2, both columns
//   });
//
// Indices are 1-based; negative ones count from the end of the explicit tracks (-1 = the last).

#include "kiln/foundation/LayoutConfig.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Layout.h"
#include "kiln/ui/Modifier.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace kiln::foundation {

    using ui::Alignment;
    using ui::Constraints;
    using ui::Dp;
    using ui::Modifier;

    enum class GridFlow : std::uint8_t { Row, Column };

    // Fr: a share of the space left after the other tracks (1fr, 2fr, ...).
    struct Fr {
        float value = 0;
        explicit constexpr Fr(float v) : value(v) {}
        bool operator==(const Fr&) const = default;
    };

    // GridTrackSize: how wide a column (or tall a row) is.
    struct GridTrackSize {
        enum class Type : std::uint8_t { Fixed = 1, Percentage, Flex, MinContent, MaxContent, Auto, MinMax };
        Type type = Type::Auto;
        float value = 0;      // Fixed: dp; Percentage: fraction; Flex: fr; MinMax: the minimum (dp)
        float maxValue = 0;   // MinMax: fr

        static GridTrackSize Fixed(Dp size);
        static GridTrackSize Percentage(float fraction);   // of the space without gaps
        static GridTrackSize Flex(Fr weight);
        // minmax(min, max fr): at least `min`, then a flexible share.
        static GridTrackSize MinMax(Dp min, Fr max);
        static GridTrackSize MinContent() { return { Type::MinContent }; }
        static GridTrackSize MaxContent() { return { Type::MaxContent }; }
        // Auto: between the content's min and max intrinsic size, growing into free space.
        static GridTrackSize Auto() { return { Type::Auto }; }
        bool operator==(const GridTrackSize&) const = default;
    };

    class GridConfigurationScope : public ui::Density {
    public:
        static constexpr int MaxGridIndex = 1000;
        static constexpr int GridIndexUnspecified = 0;

        Constraints constraints;
        GridFlow flow = GridFlow::Row;   // where auto-placed items go next

        void Column(Dp size) { Column(GridTrackSize::Fixed(size)); }
        void Column(Fr weight) { Column(GridTrackSize::Flex(weight)); }
        void Column(float percentage) { Column(GridTrackSize::Percentage(percentage)); }
        void Column(GridTrackSize size) { columnSpecs.push_back(size); }
        void Row(Dp size) { Row(GridTrackSize::Fixed(size)); }
        void Row(Fr weight) { Row(GridTrackSize::Flex(weight)); }
        void Row(float percentage) { Row(GridTrackSize::Percentage(percentage)); }
        void Row(GridTrackSize size) { rowSpecs.push_back(size); }
        // columns(...) / rows(...)
        void Columns(std::initializer_list<GridTrackSize> specs) { for (auto s : specs) Column(s); }
        void Rows(std::initializer_list<GridTrackSize> specs) { for (auto s : specs) Row(s); }
        // A named area for GridItemArea (at least a row or a column).
        void Area(std::string areaId, int row = GridIndexUnspecified, int column = GridIndexUnspecified, int rowSpan = 1, int columnSpan = 1);
        void Gap(Dp all);
        void Gap(Dp row, Dp column);
        void ColumnGap(Dp gap);
        void RowGap(Dp gap);
        static GridTrackSize MinMax(Dp min, Fr max) { return GridTrackSize::MinMax(min, max); }

        struct AreaBounds { int row, column, rowSpan, columnSpan; };
        std::vector<GridTrackSize> columnSpecs, rowSpecs;
        std::map<std::string, AreaBounds> namedAreas;
        Dp columnGap{ 0 }, rowGap{ 0 };
    };
    using GridConfig = LayoutConfig<GridConfigurationScope>;

    void Grid(GridConfig config, const Modifier& modifier, std::function<void()> content);

    // GridScope.gridItem: a cell (1-based; 0 lets that axis auto-place), spans, and the
    // alignment inside the spanned area.
    Modifier GridItem(Modifier m, int row = GridConfigurationScope::GridIndexUnspecified, int column = GridConfigurationScope::GridIndexUnspecified,
                      int rowSpan = 1, int columnSpan = 1, Alignment alignment = Alignment::TopStart());
    // gridItem(rows, columns): inclusive ranges [first, last].
    Modifier GridItem(Modifier m, std::pair<int, int> rows, std::pair<int, int> columns, Alignment alignment = Alignment::TopStart());
    // gridItem(areaId): an area the config named (an unknown one auto-places).
    Modifier GridItemArea(Modifier m, std::string areaId, Alignment alignment = Alignment::TopStart());

}
