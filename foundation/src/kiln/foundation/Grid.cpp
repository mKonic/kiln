#include "kiln/foundation/Grid.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/Composition.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace kiln::foundation {

    using namespace ui;

    namespace {
        void Require(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }
        constexpr int MaxGridIndex = GridConfigurationScope::MaxGridIndex;
        constexpr int Unspecified = GridConfigurationScope::GridIndexUnspecified;
    }

    GridTrackSize GridTrackSize::Fixed(Dp size) {
        Require(size.Specified() && size.value >= 0, "Fixed size must be non-negative and specified");
        return { Type::Fixed, size.value };
    }
    GridTrackSize GridTrackSize::Percentage(float fraction) {
        Require(fraction >= 0, "Percentage cannot be negative");
        return { Type::Percentage, fraction };
    }
    GridTrackSize GridTrackSize::Flex(Fr weight) {
        Require(weight.value >= 0 && std::isfinite(weight.value), "Flex weight must be non-negative and finite");
        return { Type::Flex, weight.value };
    }
    GridTrackSize GridTrackSize::MinMax(Dp min, Fr max) {
        Require(min.value >= 0, "MinMax minimum size cannot be negative");
        Require(max.value >= 0 && std::isfinite(max.value), "MinMax max weight must be non-negative and finite");
        return { Type::MinMax, min.value, max.value };
    }

    void GridConfigurationScope::Area(std::string areaId, int row, int column, int rowSpan, int columnSpan) {
        Require(row != Unspecified || column != Unspecified, "An area must specify at least a row or a column.");
        Require(row >= -MaxGridIndex && row <= MaxGridIndex, "row must be between -1000 and 1000");
        Require(column >= -MaxGridIndex && column <= MaxGridIndex, "column must be between -1000 and 1000");
        Require(rowSpan >= 1 && rowSpan <= MaxGridIndex, "rowSpan must be between 1 and 1000");
        Require(columnSpan >= 1 && columnSpan <= MaxGridIndex, "columnSpan must be between 1 and 1000");
        namedAreas[std::move(areaId)] = { row, column, rowSpan, columnSpan };
    }
    void GridConfigurationScope::Gap(Dp all) {
        Require(all.value >= 0, "Gap must be non-negative");
        columnGap = rowGap = all;
    }
    void GridConfigurationScope::Gap(Dp row, Dp column) {
        Require(row.value >= 0, "Row gap must be non-negative");
        Require(column.value >= 0, "Column gap must be non-negative");
        rowGap = row;
        columnGap = column;
    }
    void GridConfigurationScope::ColumnGap(Dp gap) {
        Require(gap.value >= 0, "Column gap must be non-negative");
        columnGap = gap;
    }
    void GridConfigurationScope::RowGap(Dp gap) {
        Require(gap.value >= 0, "Row gap must be non-negative");
        rowGap = gap;
    }

    namespace {
        // GridItemNode's data, in ParentData::extra.
        struct GridItemData {
            std::optional<std::string> areaId;
            int row = Unspecified, column = Unspecified, rowSpan = 1, columnSpan = 1;
            Alignment alignment = Alignment::TopStart();
            bool operator==(const GridItemData&) const = default;
        };
        struct GridItemNode final : ModifierNode, ParentDataModifier {
            GridItemData params;
            explicit GridItemNode(GridItemData p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ParentData& d) const override { d.extra = params; }
        };

        struct Item {
            Measurable* measurable;
            int row, column, rowSpan, columnSpan;
            Alignment alignment;
            Placeable* placeable = nullptr;
            int offsetX = 0, offsetY = 0, spannedWidth = 0;
        };

        const GridTrackSize kAuto = GridTrackSize::Auto();
        const GridTrackSize& SpecAt(const std::vector<GridTrackSize>& specs, int i) { return i < int(specs.size()) ? specs[std::size_t(i)] : kAuto; }

        int ResolveToZeroBasedIndex(int index, int maxCount) {
            if (index == Unspecified) return -1;
            if (index > 0) return index - 1;
            const int resolved = maxCount + index;
            return resolved >= 0 ? resolved : -1;
        }

        // resolveGridItemIndices: each item's cell; the grid's size in columns and rows.
        std::vector<Item> ResolveGridItemIndices(std::span<Measurable* const> measurables, const GridConfigurationScope& config, IntSize& gridSize) {
            std::vector<Item> items;
            items.reserve(measurables.size());
            std::unordered_set<int> occupied;
            const int explicitColCount = int(config.columnSpecs.size()), explicitRowCount = int(config.rowSpecs.size());
            int maxRow = explicitRowCount, maxCol = explicitColCount;
            auto pack = [](int r, int c) { return (r << 16) | (c & 0xFFFF); };
            auto isOccupied = [&](int r0, int c0, int rs, int cs) {
                if (r0 + rs > MaxGridIndex || c0 + cs > MaxGridIndex) return true;
                for (int r = r0; r < r0 + rs; ++r)
                    for (int c = c0; c < c0 + cs; ++c)
                        if (occupied.contains(pack(r, c))) return true;
                return false;
            };
            auto markOccupied = [&](int r0, int c0, int rs, int cs) {
                for (int r = r0; r < r0 + rs; ++r)
                    for (int c = c0; c < c0 + cs; ++c) occupied.insert(pack(r, c));
            };
            int cursorRow = 0, cursorCol = 0;
            const GridFlow flow = config.flow;
            for (Measurable* measurable : measurables) {
                int rowSpan = 1, colSpan = 1, requestedRow = -1, requestedCol = -1;
                Alignment alignment = Alignment::TopStart();
                if (const auto* data = std::any_cast<GridItemData>(&measurable->GetParentData().extra)) {
                    alignment = data->alignment;
                    int specifiedRow, specifiedCol;
                    if (data->areaId) {
                        if (auto it = config.namedAreas.find(*data->areaId); it != config.namedAreas.end()) {
                            specifiedRow = it->second.row;
                            specifiedCol = it->second.column;
                            rowSpan = it->second.rowSpan;
                            colSpan = it->second.columnSpan;
                        } else {
                            specifiedRow = specifiedCol = Unspecified;
                        }
                    } else {
                        specifiedRow = data->row;
                        specifiedCol = data->column;
                        rowSpan = data->rowSpan;
                        colSpan = data->columnSpan;
                    }
                    if (specifiedRow == Unspecified && explicitRowCount > 0 && flow == GridFlow::Column) rowSpan = std::min(rowSpan, explicitRowCount);
                    if (specifiedCol == Unspecified && explicitColCount > 0 && flow == GridFlow::Row) colSpan = std::min(colSpan, explicitColCount);
                    requestedRow = ResolveToZeroBasedIndex(specifiedRow, explicitRowCount);
                    requestedCol = ResolveToZeroBasedIndex(specifiedCol, explicitColCount);
                }
                int finalRow = -1, finalCol = -1;
                if (requestedRow != -1 && requestedCol != -1) {
                    finalRow = requestedRow;
                    finalCol = requestedCol;
                } else if (requestedRow != -1) {
                    finalRow = requestedRow;
                    int candidate = flow == GridFlow::Row && requestedRow == cursorRow ? cursorCol : 0;
                    for (; candidate < MaxGridIndex; ++candidate)
                        if (!isOccupied(requestedRow, candidate, rowSpan, colSpan)) { finalCol = candidate; break; }
                } else if (requestedCol != -1) {
                    finalCol = requestedCol;
                    int candidate = flow == GridFlow::Column && requestedCol == cursorCol ? cursorRow : 0;
                    for (; candidate < MaxGridIndex; ++candidate)
                        if (!isOccupied(candidate, requestedCol, rowSpan, colSpan)) { finalRow = candidate; break; }
                } else {
                    int r = cursorRow, c = cursorCol;
                    while (r < MaxGridIndex && c < MaxGridIndex) {
                        if (flow == GridFlow::Row) {
                            if (explicitColCount > 0 && c + colSpan > explicitColCount && c > 0) { c = 0; ++r; continue; }
                        } else if (explicitRowCount > 0 && r + rowSpan > explicitRowCount && r > 0) {
                            r = 0;
                            ++c;
                            continue;
                        }
                        if (!isOccupied(r, c, rowSpan, colSpan)) { finalRow = r; finalCol = c; break; }
                        if (flow == GridFlow::Row) {
                            if (++c > MaxGridIndex) { c = 0; ++r; }
                        } else if (++r > MaxGridIndex) {
                            r = 0;
                            ++c;
                        }
                    }
                }
                const int placementRow = std::max(0, finalRow), placementCol = std::max(0, finalCol);
                markOccupied(placementRow, placementCol, rowSpan, colSpan);
                items.push_back({ measurable, placementRow, placementCol, rowSpan, colSpan, alignment });
                maxRow = std::max(maxRow, placementRow + rowSpan);
                maxCol = std::max(maxCol, placementCol + colSpan);
                if (requestedRow == -1 && requestedCol == -1) {
                    if (flow == GridFlow::Row) { cursorRow = placementRow; cursorCol = placementCol + colSpan; }
                    else { cursorRow = placementRow + rowSpan; cursorCol = placementCol; }
                }
            }
            std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.row != b.row ? a.row < b.row : a.column < b.column; });
            gridSize = { maxCol, maxRow };
            return items;
        }

        int SpannedWidth(const Item& item, const std::vector<int>& columnWidths, int fallbackWidth, int columnGap) {
            const int colStart = item.column;
            if (colStart >= int(columnWidths.size())) return fallbackWidth;
            const int colEnd = std::min(colStart + item.columnSpan, int(columnWidths.size()));
            int width = 0;
            for (int i = colStart; i < colEnd; ++i) width += columnWidths[std::size_t(i)];
            return width + std::max(0, colEnd - colStart - 1) * columnGap;
        }

        // The largest min/max intrinsic main size of the single-span items of a track.
        struct MinMaxSize { int min = 0, max = 0; };
        MinMaxSize ColumnIntrinsics(const std::vector<const Item*>& items) {
            MinMaxSize r;
            for (const Item* it : items) {
                if (it->columnSpan != 1) continue;
                r.min = std::max(r.min, it->measurable->MinIntrinsicWidth(Constraints::Infinity));
                r.max = std::max(r.max, it->measurable->MaxIntrinsicWidth(Constraints::Infinity));
            }
            return r;
        }
        int MaxIntrinsicWidthOf(const std::vector<const Item*>& items) {
            int m = 0;
            for (const Item* it : items) if (it->columnSpan == 1) m = std::max(m, it->measurable->MaxIntrinsicWidth(Constraints::Infinity));
            return m;
        }
        int MinIntrinsicWidthOf(const std::vector<const Item*>& items) {
            int m = 0;
            for (const Item* it : items) if (it->columnSpan == 1) m = std::max(m, it->measurable->MinIntrinsicWidth(Constraints::Infinity));
            return m;
        }
        int MaxIntrinsicHeightOf(const std::vector<const Item*>& items, const std::vector<int>& widths, int fallback, int gap) {
            int m = 0;
            for (const Item* it : items) if (it->rowSpan == 1) m = std::max(m, it->measurable->MaxIntrinsicHeight(SpannedWidth(*it, widths, fallback, gap)));
            return m;
        }
        int MinIntrinsicHeightOf(const std::vector<const Item*>& items, const std::vector<int>& widths, int fallback, int gap) {
            int m = 0;
            for (const Item* it : items) if (it->rowSpan == 1) m = std::max(m, it->measurable->MinIntrinsicHeight(SpannedWidth(*it, widths, fallback, gap)));
            return m;
        }
        MinMaxSize RowIntrinsics(const std::vector<const Item*>& items, const std::vector<int>& widths, int fallback, int gap) {
            MinMaxSize r;
            for (const Item* it : items) {
                if (it->rowSpan != 1) continue;
                const int w = SpannedWidth(*it, widths, fallback, gap);
                r.min = std::max(r.min, it->measurable->MinIntrinsicHeight(w));
                r.max = std::max(r.max, it->measurable->MaxIntrinsicHeight(w));
            }
            return r;
        }

        bool Grows(const GridTrackSize& s) { return s.type != GridTrackSize::Type::Fixed && s.type != GridTrackSize::Type::Percentage; }

        // distributeSpanningSpace: spanning items the tracks are too small for widen the
        // tracks they span that can grow, evenly.
        void DistributeSpanningSpace(const std::vector<GridTrackSize>& specs, std::vector<int>& sizes, const std::vector<Item>& items, bool isRowAxis,
                                     const Constraints& constraints, const std::vector<int>* crossAxisSizes, int mainAxisGap, int crossAxisGap) {
            for (const Item& item : items) {
                const int trackIndex = isRowAxis ? item.row : item.column;
                const int span = isRowAxis ? item.rowSpan : item.columnSpan;
                if (span <= 1) continue;
                const int endIndex = std::min(trackIndex + span, int(sizes.size()));
                int current = 0, toGrow = 0;
                for (int i = trackIndex; i < endIndex; ++i) {
                    current += sizes[std::size_t(i)];
                    if (Grows(SpecAt(specs, i))) ++toGrow;
                }
                current += std::max(0, span - 1) * mainAxisGap;
                int required;
                if (isRowAxis) {
                    int itemWidth = 0;
                    if (crossAxisSizes) {
                        const int colEnd = std::min(item.column + item.columnSpan, int(crossAxisSizes->size()));
                        for (int i = item.column; i < colEnd; ++i) itemWidth += (*crossAxisSizes)[std::size_t(i)];
                        itemWidth += std::max(0, item.columnSpan - 1) * crossAxisGap;
                    } else {
                        itemWidth = constraints.maxWidth;
                    }
                    required = item.measurable->MaxIntrinsicHeight(itemWidth);
                } else {
                    required = item.measurable->MaxIntrinsicWidth(Constraints::Infinity);
                }
                const int deficit = required - current;
                if (deficit <= 0 || toGrow <= 0) continue;
                const int share = deficit / toGrow;
                int remainder = deficit % toGrow;
                for (int i = trackIndex; i < endIndex; ++i) {
                    if (!Grows(SpecAt(specs, i))) continue;
                    sizes[std::size_t(i)] += share + (remainder > 0 ? 1 : 0);
                    if (remainder > 0) --remainder;
                }
            }
        }

        // expandAutoTracks: Auto tracks grow towards their max-content size into free space.
        void ExpandAutoTracks(const std::vector<int>& autoIndices, std::vector<int>& sizes, const std::vector<int>& maxSizes, int availableSpace) {
            if (autoIndices.empty()) return;
            int used = 0;
            for (int s : sizes) used += s;
            const int remaining = availableSpace - used;
            if (remaining <= 0) return;
            std::vector<int> potential(autoIndices.size());
            int total = 0;
            for (std::size_t i = 0; i < autoIndices.size(); ++i) {
                const std::size_t t = std::size_t(autoIndices[i]);
                potential[i] = std::max(0, maxSizes[t] - sizes[t]);
                total += potential[i];
            }
            if (total == 0) return;
            for (std::size_t i = 0; i < autoIndices.size(); ++i)
                sizes[std::size_t(autoIndices[i])] += remaining >= total ? potential[i] : RoundJava(float(potential[i]) / float(total) * float(remaining));
        }

        int DistributeFlexSpaceAndGetTotal(std::vector<int>& sizes, int availableTrackSpace, float totalFlex, const std::vector<GridTrackSize>& specs) {
            int used = 0;
            for (int s : sizes) used += s;
            const int remaining = availableTrackSpace == Constraints::Infinity ? 0 : std::max(0, availableTrackSpace - used);
            int added = 0;
            if (totalFlex > 0 && remaining > 0) {
                int distributed = 0;
                float accumulated = 0;
                for (int i = 0; i < int(sizes.size()); ++i) {
                    const GridTrackSize& spec = SpecAt(specs, i);
                    const float weight = spec.type == GridTrackSize::Type::Flex ? spec.value : spec.type == GridTrackSize::Type::MinMax ? spec.maxValue : 0;
                    if (weight <= 0) continue;
                    accumulated += weight;
                    const int target = RoundJava(accumulated / totalFlex * float(remaining));
                    const int share = std::max(0, target - distributed);
                    sizes[std::size_t(i)] += share;
                    distributed += share;
                    added = distributed;
                }
            }
            return used + added;
        }

        // calculateColumnWidths / calculateRowHeights: one function, the axis chosen by isRow.
        int CalculateTrackSizes(const Density& density, bool isRow, const std::vector<GridTrackSize>& specs, int totalCount, int availableSpace,
                                std::vector<int>& out, const std::vector<std::vector<const Item*>>& itemsByTrack, const Constraints& constraints,
                                const std::vector<int>& columnWidths, const std::vector<Item>& items, int mainGap, int columnGap) {
            if (totalCount == 0) return 0;
            float totalFlex = 0;
            const int totalGapSpace = std::max(mainGap * (totalCount - 1), 0);
            const int available = availableSpace == Constraints::Infinity ? Constraints::Infinity : std::max(availableSpace - totalGapSpace, 0);
            std::vector<int> autoIndices, autoMax(std::size_t(totalCount), 0);
            const int fallback = constraints.maxWidth;
            for (int index = 0; index < totalCount; ++index) {
                const GridTrackSize& spec = SpecAt(specs, index);
                const auto& track = itemsByTrack[std::size_t(index)];
                auto maxContent = [&] { return isRow ? MaxIntrinsicHeightOf(track, columnWidths, fallback, columnGap) : MaxIntrinsicWidthOf(track); };
                auto minContent = [&] { return isRow ? MinIntrinsicHeightOf(track, columnWidths, fallback, columnGap) : MinIntrinsicWidthOf(track); };
                int size;
                switch (spec.type) {
                    case GridTrackSize::Type::Fixed: size = density.RoundToPx(Dp(spec.value)); break;
                    case GridTrackSize::Type::Percentage: size = available != Constraints::Infinity ? RoundJava(spec.value * float(available)) : maxContent(); break;
                    case GridTrackSize::Type::Flex: totalFlex += spec.value; size = minContent(); break;
                    case GridTrackSize::Type::MinContent: size = minContent(); break;
                    case GridTrackSize::Type::MaxContent: size = maxContent(); break;
                    case GridTrackSize::Type::Auto:
                        if (available == Constraints::Infinity) {
                            size = maxContent();
                        } else {
                            const MinMaxSize mm = isRow ? RowIntrinsics(track, columnWidths, fallback, columnGap) : ColumnIntrinsics(track);
                            autoIndices.push_back(index);
                            autoMax[std::size_t(index)] = mm.max;
                            size = mm.min;
                        }
                        break;
                    case GridTrackSize::Type::MinMax: totalFlex += spec.maxValue; size = density.RoundToPx(Dp(spec.value)); break;
                    default: size = maxContent(); break;
                }
                out[std::size_t(index)] = size;
            }
            DistributeSpanningSpace(specs, out, items, isRow, constraints, isRow ? &columnWidths : nullptr, mainGap, isRow ? columnGap : 0);
            if (available != Constraints::Infinity && !autoIndices.empty()) ExpandAutoTracks(autoIndices, out, autoMax, available);
            return DistributeFlexSpaceAndGetTotal(out, available, totalFlex, specs);
        }

        std::vector<int> TrackOffsets(const std::vector<int>& sizes, int gap) {
            std::vector<int> offsets(sizes.size());
            int current = 0;
            for (std::size_t i = 0; i < sizes.size(); ++i) { offsets[i] = current; current += sizes[i] + gap; }
            return offsets;
        }

        class GridPolicy final : public MeasurePolicy {
        public:
            explicit GridPolicy(GridConfig config) : m_Config(std::move(config)) {}
            const GridConfig& Config() const { return m_Config; }

            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> measurables, const Constraints& constraints) override {
                GridConfigurationScope config;
                static_cast<Density&>(config) = s;
                config.constraints = constraints;
                m_Config.Configure(config);
                IntSize gridSize;
                auto items = std::make_shared<std::vector<Item>>(ResolveGridItemIndices(measurables, config, gridSize));
                const int colCount = gridSize.width, rowCount = gridSize.height;
                const int colGap = s.RoundToPx(config.columnGap), rowGap = s.RoundToPx(config.rowGap);
                std::vector<std::vector<const Item*>> byColumn(static_cast<std::size_t>(colCount)), byRow(static_cast<std::size_t>(rowCount));
                for (const Item& it : *items) {
                    if (it.column < colCount) byColumn[std::size_t(it.column)].push_back(&it);
                    if (it.row < rowCount) byRow[std::size_t(it.row)].push_back(&it);
                }
                auto columnWidths = std::make_shared<std::vector<int>>(std::size_t(colCount), 0);
                const int trackWidth = CalculateTrackSizes(s, false, config.columnSpecs, colCount, constraints.maxWidth, *columnWidths, byColumn, constraints, {},
                                                           *items, colGap, colGap);
                auto rowHeights = std::make_shared<std::vector<int>>(std::size_t(rowCount), 0);
                const int trackHeight = CalculateTrackSizes(s, true, config.rowSpecs, rowCount, constraints.maxHeight, *rowHeights, byRow, constraints, *columnWidths,
                                                            *items, rowGap, colGap);
                const int totalWidth = trackWidth + std::max(0, colCount - 1) * colGap;
                const int totalHeight = trackHeight + std::max(0, rowCount - 1) * rowGap;
                // measureItems: each in its spanned area, aligned there.
                const LayoutDirection dir = s.Direction();
                for (Item& item : *items) {
                    if (item.row >= rowCount || item.column >= colCount) continue;
                    const int colLimit = std::min(item.column + item.columnSpan, colCount);
                    int width = 0;
                    for (int i = item.column; i < colLimit; ++i) width += (*columnWidths)[std::size_t(i)];
                    if (colLimit - item.column > 1) width += (colLimit - item.column - 1) * colGap;
                    const int rowLimit = std::min(item.row + item.rowSpan, rowCount);
                    int height = 0;
                    for (int i = item.row; i < rowLimit; ++i) height += (*rowHeights)[std::size_t(i)];
                    if (rowLimit - item.row > 1) height += (rowLimit - item.row - 1) * rowGap;
                    Placeable& p = item.measurable->Measure(Constraints{ 0, width, 0, height });
                    const IntOffset o = item.alignment.Align({ p.Width(), p.Height() }, { width, height }, dir);
                    item.placeable = &p;
                    item.spannedWidth = width;
                    item.offsetX = o.x;
                    item.offsetY = o.y;
                }
                const int layoutWidth = constraints.ConstrainWidth(totalWidth), layoutHeight = constraints.ConstrainHeight(totalHeight);
                return s.Layout(layoutWidth, layoutHeight, [items, columnWidths, rowHeights, colGap, rowGap, layoutWidth, dir](PlacementScope& p) {
                    const std::vector<int> columnOffsets = TrackOffsets(*columnWidths, colGap), rowOffsets = TrackOffsets(*rowHeights, rowGap);
                    const bool rtl = dir == LayoutDirection::Rtl;
                    for (const Item& item : *items) {
                        if (!item.placeable) continue;
                        const int columnOffset = rtl ? layoutWidth - columnOffsets[std::size_t(item.column)] - item.spannedWidth : columnOffsets[std::size_t(item.column)];
                        p.Place(*item.placeable, columnOffset + item.offsetX, rowOffsets[std::size_t(item.row)] + item.offsetY);
                    }
                });
            }

        private:
            GridConfig m_Config;
        };
    }

    void Grid(GridConfig config, const Modifier& modifier, std::function<void()> content) {
        auto* c = rt::Composition::Current();
        c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                            [&](LayoutNode& n) {
                                n.SetModifier(modifier);
                                TakeLocalLayoutDirection(n);
                                if (auto* cur = dynamic_cast<GridPolicy*>(n.Policy().get()); !cur || !(cur->Config() == config))
                                    n.SetMeasurePolicy(std::make_shared<GridPolicy>(config));
                            }, std::move(content));
    }

    Modifier GridItem(Modifier m, int row, int column, int rowSpan, int columnSpan, Alignment alignment) {
        if (row != Unspecified) Require(row >= -MaxGridIndex && row <= MaxGridIndex, "row must be between -1000 and 1000");
        if (column != Unspecified) Require(column >= -MaxGridIndex && column <= MaxGridIndex, "column must be between -1000 and 1000");
        Require(rowSpan > 0, "rowSpan must be > 0");
        Require(columnSpan > 0, "columnSpan must be > 0");
        return m.With<GridItemNode>(GridItemData{ std::nullopt, row, column, rowSpan, columnSpan, alignment });
    }

    Modifier GridItem(Modifier m, std::pair<int, int> rows, std::pair<int, int> columns, Alignment alignment) {
        Require(rows.second >= rows.first, "Row range cannot be empty");
        Require(columns.second >= columns.first, "Column range cannot be empty");
        return GridItem(std::move(m), rows.first, columns.first, rows.second - rows.first + 1, columns.second - columns.first + 1, alignment);
    }

    Modifier GridItemArea(Modifier m, std::string areaId, Alignment alignment) {
        return m.With<GridItemNode>(GridItemData{ std::move(areaId), Unspecified, Unspecified, 1, 1, alignment });
    }

}
