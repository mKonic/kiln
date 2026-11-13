#include "kiln/foundation/FlexBox.h"
#include <kiln/Function.h>

#include "kiln/foundation/Compose.h"
#include "kiln/runtime/Composition.h"

#include <algorithm>
#include <any>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace kiln::foundation {

    using namespace ui;

    void FlexBoxConfigScope::MaxItemsInEachLine(int v) {
        if (v <= 0) throw std::invalid_argument("maxItemsInEachLine must be positive: " + std::to_string(v));
        maxItemsInEachLine = v;
    }
    void FlexConfigScope::Grow(float v) {
        if (v < 0) throw std::invalid_argument("Flex grow cannot be negative: " + std::to_string(v));
        grow = v;
    }
    void FlexConfigScope::Shrink(float v) {
        if (v < 0) throw std::invalid_argument("Flex shrink cannot be negative: " + std::to_string(v));
        shrink = v;
    }

    namespace {
        // The child's flex config, in ParentData::extra.
        struct FlexChildData { FlexConfig config; };

        struct FlexChildParams {
            FlexConfig config;
            bool operator==(const FlexChildParams&) const = default;
        };
        struct FlexChildNode final : ModifierNode, ParentDataModifier {
            FlexChildParams params;
            explicit FlexChildNode(FlexChildParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ParentData& d) const override { d.extra = FlexChildData{ params.config }; }
        };

        // OrientationIndependentConstraints.
        struct AxisConstraints {
            int mainAxisMin, mainAxisMax, crossAxisMin, crossAxisMax;
            AxisConstraints(const Constraints& c, bool horizontal)
                : mainAxisMin(horizontal ? c.minWidth : c.minHeight), mainAxisMax(horizontal ? c.maxWidth : c.maxHeight),
                  crossAxisMin(horizontal ? c.minHeight : c.minWidth), crossAxisMax(horizontal ? c.maxHeight : c.maxWidth) {}
        };

        // ResolvedFlexBoxConfig.
        struct BoxConfig : FlexBoxConfigScope {
            void Prepare(const Density& d, const Constraints& c) {
                static_cast<FlexBoxConfigScope&>(*this) = FlexBoxConfigScope{};
                static_cast<Density&>(*this) = d;
                constraints = c;
            }
            bool IsHorizontal() const { return direction == FlexDirection::Row || direction == FlexDirection::RowReverse; }
            bool IsWrapEnabled() const { return wrap == FlexWrap::Wrap || wrap == FlexWrap::WrapReverse; }
            bool IsCrossAxisReverse() const { return wrap == FlexWrap::WrapReverse; }
            bool HasBaseline() const { return alignItems == FlexAlignItems::Baseline; }
            int MainAxisGap() const { return RoundToPx(IsHorizontal() ? columnGap : rowGap); }
            int CrossAxisGap() const { return RoundToPx(IsHorizontal() ? rowGap : columnGap); }
            int GetBaseline(Placeable& p) const {
                if (baselineBlock) return baselineBlock(p);
                if (baselineLine) {
                    const int v = p.Get(*baselineLine);
                    return v != AlignmentLine::Unspecified ? v : baselineLine->horizontal ? p.Height() : p.Width();
                }
                const int v = p.Get(FirstBaseline);
                return v != AlignmentLine::Unspecified ? v : p.Height();
            }
        };

        // ResolvedFlexItemInfo.
        struct Item : FlexConfigScope {
            Measurable* measurable = nullptr;
            int minMainAxisSize = -1, maxContentSize = -1;
            int flexBaseSize = 0, hypotheticalMainSize = 0, targetMainSize = 0;
            bool isFrozen = false;
            int mainPosition = 0, crossPosition = 0;
            Placeable* placeable = nullptr;
            int baseline = AlignmentLine::Unspecified;
            int mainAxisSize = 0, crossAxisSize = 0;

            void Prepare(const Density& d, const AxisConstraints& c) {
                static_cast<Density&>(*this) = d;
                flexBoxMainAxisMaxPx = c.mainAxisMax;
                flexBoxMainAxisMinPx = c.mainAxisMin;
                flexBoxCrossAxisMaxPx = c.crossAxisMax;
                flexBoxCrossAxisMinPx = c.crossAxisMin;
            }
            bool IsGrowUnset() const { return std::isnan(grow); }
            bool HasBaseline() const { return alignSelf == FlexAlignSelf::Baseline; }
            int GetMinMainAxisSize(bool horizontal) {
                if (minMainAxisSize == -1)
                    minMainAxisSize = !measurable ? 0 : horizontal ? measurable->MinIntrinsicWidth(flexBoxCrossAxisMaxPx) : measurable->MinIntrinsicHeight(flexBoxCrossAxisMaxPx);
                return minMainAxisSize;
            }
            int GetMaxContentSize(bool horizontal) {
                if (maxContentSize == -1)
                    maxContentSize = !measurable ? 0 : horizontal ? measurable->MaxIntrinsicWidth(flexBoxCrossAxisMaxPx) : measurable->MaxIntrinsicHeight(flexBoxCrossAxisMaxPx);
                return maxContentSize;
            }
            int GetBaseline(Placeable& p, const BoxConfig& fallback) const {
                if (baselineBlock) return baselineBlock(p);
                if (baselineLine) {
                    const int v = p.Get(*baselineLine);
                    return v != AlignmentLine::Unspecified ? v : baselineLine->horizontal ? p.Height() : p.Width();
                }
                if (alignSelf == FlexAlignSelf::Baseline) return fallback.GetBaseline(p);
                if (fallback.HasBaseline() && alignSelf == FlexAlignSelf::Auto) return fallback.GetBaseline(p);
                return AlignmentLine::Unspecified;
            }
        };

        struct Line {
            std::size_t startIndex = 0, endIndex = 0;
            int mainAxisSize = 0, crossAxisSize = 0, crossStart = 0, maxAboveBaseline = 0;
        };

        template<typename V, typename F> void ForEach(V& v, bool reversed, std::size_t start, std::size_t end, F&& f) {
            if (reversed) { for (std::size_t i = end; i-- > start;) f(v[i]); }
            else { for (std::size_t i = start; i < end; ++i) f(v[i]); }
        }

        class FlexBoxPolicy final : public MeasurePolicy {
        public:
            explicit FlexBoxPolicy(FlexBoxConfig config) : m_Config(std::move(config)) {}
            const FlexBoxConfig& Config() const { return m_Config; }

            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> measurables, const Constraints& constraints) override {
                if (measurables.empty()) return s.Layout(constraints.minWidth, constraints.minHeight);
                BoxConfig& config = Resolve(s, constraints);
                return MeasureFlexBox(s, config, measurables, AxisConstraints(constraints, config.IsHorizontal()), config.IsHorizontal());
            }

            int MinIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ms, int height) override {
                if (ms.empty()) return 0;
                BoxConfig& config = Resolve(s, Constraints{ 0, Constraints::Infinity, 0, height });
                if (config.IsHorizontal()) return MinMainAxis(config, ms, [height](Measurable* m) { return m->MinIntrinsicWidth(height); });
                return IntrinsicCrossAxisSize(config, ms, height, [](Measurable* m) { return m->MinIntrinsicHeight(Constraints::Infinity); },
                                              [](Measurable* m, int main) { return m->MinIntrinsicWidth(main); });
            }
            int MinIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ms, int width) override {
                if (ms.empty()) return 0;
                BoxConfig& config = Resolve(s, Constraints{ 0, width, 0, Constraints::Infinity });
                if (config.IsHorizontal())
                    return IntrinsicCrossAxisSize(config, ms, width, [](Measurable* m) { return m->MinIntrinsicWidth(Constraints::Infinity); },
                                                  [](Measurable* m, int main) { return m->MinIntrinsicHeight(main); });
                return MinMainAxis(config, ms, [width](Measurable* m) { return m->MinIntrinsicHeight(width); });
            }
            int MaxIntrinsicWidth(MeasureScope& s, std::span<Measurable* const> ms, int height) override {
                if (ms.empty()) return 0;
                BoxConfig& config = Resolve(s, Constraints{ 0, Constraints::Infinity, 0, height });
                if (config.IsHorizontal()) return MaxIntrinsicMainAxisSize(config, ms, [height](Measurable* m) { return m->MaxIntrinsicWidth(height); });
                return IntrinsicCrossAxisSize(config, ms, height, [](Measurable* m) { return m->MaxIntrinsicHeight(Constraints::Infinity); },
                                              [](Measurable* m, int main) { return m->MaxIntrinsicWidth(main); });
            }
            int MaxIntrinsicHeight(MeasureScope& s, std::span<Measurable* const> ms, int width) override {
                if (ms.empty()) return 0;
                BoxConfig& config = Resolve(s, Constraints{ 0, width, 0, Constraints::Infinity });
                if (config.IsHorizontal())
                    return IntrinsicCrossAxisSize(config, ms, width, [](Measurable* m) { return m->MaxIntrinsicWidth(Constraints::Infinity); },
                                                  [](Measurable* m, int main) { return m->MaxIntrinsicHeight(main); });
                return MaxIntrinsicMainAxisSize(config, ms, [width](Measurable* m) { return m->MaxIntrinsicHeight(width); });
            }

        private:
            BoxConfig& Resolve(const Density& d, const Constraints& c) {
                m_Resolved.Prepare(d, c);
                m_Config.Configure(m_Resolved);
                return m_Resolved;
            }

            // minIntrinsic along the main axis: the widest item when wrapping, else the sum plus gaps.
            template<typename F> static int MinMainAxis(const BoxConfig& config, std::span<Measurable* const> ms, F size) {
                if (config.IsWrapEnabled()) {
                    int maxSize = 0;
                    for (auto* m : ms) maxSize = std::max(maxSize, size(m));
                    return maxSize;
                }
                int sum = 0;
                for (auto* m : ms) sum += size(m);
                return sum + std::max<int>(int(ms.size()) - 1, 0) * config.MainAxisGap();
            }

            template<typename F> static int MaxIntrinsicMainAxisSize(const BoxConfig& config, std::span<Measurable* const> ms, F mainAxisSize) {
                const int gap = config.MainAxisGap();
                const int maxItems = config.IsWrapEnabled() ? config.maxItemsInEachLine : INT32_MAX;
                int maxLineSize = 0, currentLineSize = 0, itemsInCurrentLine = 0;
                for (auto* m : ms) {
                    currentLineSize += itemsInCurrentLine == 0 ? mainAxisSize(m) : gap + mainAxisSize(m);
                    if (++itemsInCurrentLine == maxItems) {
                        maxLineSize = std::max(maxLineSize, currentLineSize);
                        currentLineSize = 0;
                        itemsInCurrentLine = 0;
                    }
                }
                return std::max(maxLineSize, currentLineSize);
            }

            template<typename M, typename C>
            static int IntrinsicCrossAxisSize(const BoxConfig& config, std::span<Measurable* const> ms, int mainAxisAvailable, M mainAxisSize, C crossAxisSize) {
                const int mainAxisGap = config.MainAxisGap(), crossAxisGap = config.CrossAxisGap();
                int currentLineMain = 0, currentLineCross = 0, total = 0, itemsInCurrentLine = 0;
                for (auto* m : ms) {
                    const int itemMain = mainAxisSize(m);
                    const int itemCross = crossAxisSize(m, itemMain);
                    const int projected = itemsInCurrentLine == 0 ? itemMain : currentLineMain + mainAxisGap + itemMain;
                    if (config.IsWrapEnabled() && itemsInCurrentLine > 0 && (itemsInCurrentLine >= config.maxItemsInEachLine || projected > mainAxisAvailable)) {
                        total += currentLineCross + crossAxisGap;
                        currentLineMain = itemMain;
                        currentLineCross = itemCross;
                        itemsInCurrentLine = 1;
                    } else {
                        currentLineMain = projected;
                        currentLineCross = std::max(currentLineCross, itemCross);
                        ++itemsInCurrentLine;
                    }
                }
                return total + currentLineCross;
            }

            MeasureResult MeasureFlexBox(MeasureScope& s, BoxConfig& config, std::span<Measurable* const> measurables, const AxisConstraints& constraints, bool isHorizontal) {
                auto items = std::make_shared<std::vector<Item>>();
                items->reserve(measurables.size());
                bool needsUpfront = NeedUpfrontCrossAxisCalculation(config, constraints);
                bool needsSorting = false;
                for (auto* m : measurables) {
                    items->push_back(CreateFlexItem(s, m, isHorizontal, constraints));
                    const Item& it = items->back();
                    if (it.order != 0) needsSorting = true;
                    if (it.alignSelf == FlexAlignSelf::Baseline || it.alignSelf == FlexAlignSelf::Stretch) needsUpfront = true;
                }
                if (needsSorting) std::stable_sort(items->begin(), items->end(), [](const Item& a, const Item& b) { return a.order < b.order; });
                const int mainAxisGap = config.MainAxisGap(), crossAxisGap = config.CrossAxisGap();
                int totalLinesCrossSize = 0;
                auto lines = std::make_shared<std::vector<Line>>(BuildFlexLines(config, *items, constraints, mainAxisGap, crossAxisGap, needsUpfront, totalLinesCrossSize));
                if (!config.IsWrapEnabled() && !lines->empty()) (*lines)[0].crossAxisSize = std::max((*lines)[0].crossAxisSize, constraints.crossAxisMin);
                totalLinesCrossSize = ApplyAlignContentStretch(config, *lines, constraints, totalLinesCrossSize, crossAxisGap);
                totalLinesCrossSize = MeasureFlexItems(*lines, *items, config, totalLinesCrossSize, needsUpfront, constraints);
                CalculateLineCrossPositions(config, std::max(totalLinesCrossSize, constraints.crossAxisMin), *lines, totalLinesCrossSize, crossAxisGap);
                totalLinesCrossSize = TotalCrossAxisSize(*lines, config.IsCrossAxisReverse());
                totalLinesCrossSize = std::max(totalLinesCrossSize, constraints.crossAxisMin);
                int maxLineMainSize = constraints.mainAxisMin;
                if (!lines->empty()) {
                    maxLineMainSize = (*lines)[0].mainAxisSize;
                    for (const auto& l : *lines) maxLineMainSize = std::max(maxLineMainSize, l.mainAxisSize);
                }
                const int mainAxisSize = std::clamp(std::max(maxLineMainSize, constraints.mainAxisMin), constraints.mainAxisMin, constraints.mainAxisMax);
                const int crossSize = std::clamp(totalLinesCrossSize, constraints.crossAxisMin, constraints.crossAxisMax);
                const int layoutWidth = isHorizontal ? mainAxisSize : crossSize;
                const int layoutHeight = isHorizontal ? crossSize : mainAxisSize;
                const bool isMainAxisReverse = config.direction == FlexDirection::RowReverse || config.direction == FlexDirection::ColumnReverse;
                const FlexJustifyContent justify = config.justifyContent;
                const FlexAlignItems alignItems = config.alignItems;
                const bool crossReverse = config.IsCrossAxisReverse();
                return s.Layout(layoutWidth, layoutHeight, [items, lines, isMainAxisReverse, justify, alignItems, crossReverse, isHorizontal, layoutWidth, layoutHeight,
                                                            mainAxisGap](PlacementScope& p) {
                    ForEach(*lines, crossReverse, 0, lines->size(), [&](Line& line) {
                        PositionItemsOnMainAxis(*items, justify, alignItems, isHorizontal ? layoutWidth : layoutHeight, line, mainAxisGap, isMainAxisReverse);
                        ForEach(*items, isMainAxisReverse, line.startIndex, line.endIndex, [&](Item& item) {
                            const int x = isHorizontal ? item.mainPosition : item.crossPosition;
                            const int y = isHorizontal ? item.crossPosition : item.mainPosition;
                            if (item.placeable) p.PlaceRelative(*item.placeable, x, y);
                        });
                    });
                });
            }

            static Item CreateFlexItem(MeasureScope& s, Measurable* measurable, bool isHorizontal, const AxisConstraints& constraints) {
                Item item;
                item.Prepare(s, constraints);
                const ParentData& data = measurable->GetParentData();
                if (const auto* flex = std::any_cast<FlexChildData>(&data.extra)) flex->config.Configure(item);
                if (item.IsGrowUnset()) {
                    const float fill = isHorizontal ? data.fillHorizontalFraction : data.fillVerticalFraction;
                    item.grow = fill > 0 ? fill : 0;
                }
                item.measurable = measurable;
                const int minMainAxisSize = item.GetMinMainAxisSize(isHorizontal);
                int flexBaseSize;
                switch (item.basis.kind) {
                    case FlexBasis::Kind::Dp: flexBaseSize = s.RoundToPx(Dp(item.basis.value)); break;
                    case FlexBasis::Kind::Percent:
                        flexBaseSize = constraints.mainAxisMax == Constraints::Infinity || std::isnan(item.basis.value)
                                           ? item.GetMaxContentSize(isHorizontal)
                                           : int(float(constraints.mainAxisMax) * item.basis.value);
                        break;
                    default: flexBaseSize = item.GetMaxContentSize(isHorizontal); break;
                }
                item.flexBaseSize = flexBaseSize;
                item.hypotheticalMainSize = std::max(flexBaseSize, minMainAxisSize);
                item.targetMainSize = item.hypotheticalMainSize;
                return item;
            }

            static bool NeedUpfrontCrossAxisCalculation(const BoxConfig& c, const AxisConstraints& constraints) {
                return c.alignItems == FlexAlignItems::Stretch || c.alignItems == FlexAlignItems::Baseline ||
                       (c.IsWrapEnabled() && c.alignContent == FlexAlignContent::Stretch && constraints.crossAxisMax != Constraints::Infinity);
            }

            static std::vector<Line> BuildFlexLines(const BoxConfig& config, std::vector<Item>& items, const AxisConstraints& constraints, int mainAxisGap,
                                                    int crossAxisGap, bool needsUpfront, int& totalLinesCrossSizeOut) {
                std::vector<Line> lines;
                Line current;
                int currentLineHypotheticalMainAxisSize = 0, currentCrossPosition = 0, totalLinesCrossSize = 0;
                std::size_t lineStartIndex = 0;
                int remainingCrossAxisSize = constraints.crossAxisMax;
                for (std::size_t index = 0; index < items.size(); ++index) {
                    const Item& item = items[index];
                    // Compared in 64 bits: Kotlin's Int sum wraps the same way only for huge sizes.
                    if (config.IsWrapEnabled() && index > lineStartIndex &&
                        (int(index - lineStartIndex) >= config.maxItemsInEachLine ||
                         std::int64_t(currentLineHypotheticalMainAxisSize) + item.hypotheticalMainSize > constraints.mainAxisMax)) {
                        current.startIndex = lineStartIndex;
                        current.endIndex = index;
                        ProcessFlexLine(current, items, config, currentLineHypotheticalMainAxisSize - mainAxisGap, needsUpfront, constraints, remainingCrossAxisSize);
                        totalLinesCrossSize += current.crossAxisSize;
                        current.crossStart = currentCrossPosition;
                        currentCrossPosition += current.crossAxisSize + crossAxisGap;
                        remainingCrossAxisSize = std::max(remainingCrossAxisSize - (current.crossAxisSize + crossAxisGap), 0);
                        lines.push_back(current);
                        current = Line{};
                        lineStartIndex = index;
                        currentLineHypotheticalMainAxisSize = item.hypotheticalMainSize + mainAxisGap;
                    } else {
                        currentLineHypotheticalMainAxisSize += item.hypotheticalMainSize + mainAxisGap;
                    }
                }
                if (lineStartIndex < items.size()) {
                    current.startIndex = lineStartIndex;
                    current.endIndex = items.size();
                    ProcessFlexLine(current, items, config, currentLineHypotheticalMainAxisSize - mainAxisGap, needsUpfront, constraints, remainingCrossAxisSize);
                    totalLinesCrossSize += current.crossAxisSize;
                    current.crossStart = currentCrossPosition;
                    lines.push_back(current);
                }
                totalLinesCrossSizeOut = totalLinesCrossSize;
                return lines;
            }

            static void ProcessFlexLine(Line& line, std::vector<Item>& items, const BoxConfig& config, int hypotheticalLineSize, bool needsUpfront,
                                        const AxisConstraints& constraints, int remainingCrossAxisSize) {
                line.mainAxisSize = ResolveFlexibleLengths(config.IsHorizontal(), items, config, line.startIndex, line.endIndex, hypotheticalLineSize, constraints.mainAxisMax);
                if (needsUpfront) CalculateLineCrossAxisSize(items, config, line, remainingCrossAxisSize);
            }

            static int ResolveFlexibleLengths(bool isHorizontal, std::vector<Item>& items, const BoxConfig& config, std::size_t startIndex, std::size_t endIndex,
                                              int hypotheticalLineSize, int containerMainAxisSize) {
                const int itemCount = int(endIndex - startIndex);
                const int totalGap = itemCount > 0 ? (itemCount - 1) * config.MainAxisGap() : 0;
                if (containerMainAxisSize == Constraints::Infinity) {
                    int lineMainAxisSize = totalGap;
                    for (std::size_t i = startIndex; i < endIndex; ++i) {
                        items[i].targetMainSize = items[i].hypotheticalMainSize;
                        lineMainAxisSize += items[i].targetMainSize;
                    }
                    return lineMainAxisSize;
                }
                const bool isGrowing = hypotheticalLineSize < containerMainAxisSize;
                int unfrozenCount = 0, sumFlexBaseSize = 0, sumFrozenTargetSize = 0;
                float sumGrow = 0, sumScaledShrink = 0, sumFactors = 0;
                for (std::size_t i = startIndex; i < endIndex; ++i) {
                    Item& item = items[i];
                    const float flexFactor = isGrowing ? item.grow : item.shrink;
                    if (flexFactor == 0 || (!isGrowing && item.flexBaseSize < item.hypotheticalMainSize)) {
                        item.targetMainSize = item.hypotheticalMainSize;
                        item.isFrozen = true;
                        sumFrozenTargetSize += item.targetMainSize;
                    } else {
                        item.isFrozen = false;
                        ++unfrozenCount;
                        sumFlexBaseSize += item.flexBaseSize;
                        sumFactors += flexFactor;
                        if (isGrowing) sumGrow += item.grow;
                        else sumScaledShrink += item.shrink * float(item.flexBaseSize);
                    }
                }
                const float initialFreeSpace = float(containerMainAxisSize - totalGap - sumFrozenTargetSize - sumFlexBaseSize);
                int allocatedUnfrozenSize = 0;
                if (isGrowing) {
                    float freeSpace = float(containerMainAxisSize - (totalGap + sumFrozenTargetSize + sumFlexBaseSize));
                    if (sumFactors < 1) {
                        const float fractional = initialFreeSpace * sumFactors;
                        if (std::abs(fractional) < std::abs(freeSpace)) freeSpace = fractional;
                    }
                    float currentFreeSpace = freeSpace, currentSumGrow = sumGrow;
                    for (std::size_t i = startIndex; i < endIndex; ++i) {
                        Item& item = items[i];
                        if (item.isFrozen) continue;
                        const float share = currentSumGrow > 0 ? item.grow / currentSumGrow : 0;
                        const int space = RoundJava(currentFreeSpace * share);
                        currentFreeSpace -= float(space);
                        currentSumGrow -= item.grow;
                        item.targetMainSize = item.flexBaseSize + space;
                        allocatedUnfrozenSize += item.targetMainSize;
                    }
                } else {
                    bool needsRedistribution = true;
                    int loopCount = 0;
                    while (needsRedistribution && loopCount < itemCount) {
                        needsRedistribution = false;
                        ++loopCount;
                        allocatedUnfrozenSize = 0;
                        if (unfrozenCount == 0) break;
                        float freeSpace = float(containerMainAxisSize - (totalGap + sumFrozenTargetSize + sumFlexBaseSize));
                        if (sumFactors < 1) {
                            const float fractional = initialFreeSpace * sumFactors;
                            if (std::abs(fractional) < std::abs(freeSpace)) freeSpace = fractional;
                        }
                        float currentFreeSpace = std::abs(freeSpace), currentSumScaledShrink = sumScaledShrink;
                        for (std::size_t i = startIndex; i < endIndex; ++i) {
                            Item& item = items[i];
                            if (item.isFrozen) continue;
                            const float scaledShrink = item.shrink * float(item.flexBaseSize);
                            const float share = currentSumScaledShrink > 0 ? scaledShrink / currentSumScaledShrink : 0;
                            const int spaceToTake = RoundJava(currentFreeSpace * share);
                            const int targetSize = item.flexBaseSize - spaceToTake;
                            const int minSize = item.GetMinMainAxisSize(isHorizontal);
                            if (targetSize < minSize) {
                                item.isFrozen = true;
                                needsRedistribution = true;
                                currentFreeSpace -= float(item.flexBaseSize - minSize);
                                currentSumScaledShrink -= scaledShrink;
                                item.targetMainSize = minSize;
                                --unfrozenCount;
                                sumFrozenTargetSize += minSize;
                                sumFlexBaseSize -= item.flexBaseSize;
                                sumScaledShrink -= scaledShrink;
                                sumFactors -= item.shrink;
                            } else {
                                currentFreeSpace -= float(spaceToTake);
                                currentSumScaledShrink -= scaledShrink;
                                item.targetMainSize = targetSize;
                                allocatedUnfrozenSize += item.targetMainSize;
                            }
                        }
                    }
                }
                return totalGap + sumFrozenTargetSize + allocatedUnfrozenSize;
            }

            static int ApplyAlignContentStretch(const BoxConfig& config, std::vector<Line>& lines, const AxisConstraints& constraints, int totalLinesCrossSize, int crossAxisGap) {
                if (!config.IsWrapEnabled() || config.alignContent != FlexAlignContent::Stretch || constraints.crossAxisMin == Constraints::Infinity || lines.empty())
                    return totalLinesCrossSize;
                const int totalSpacing = int(lines.size() - 1) * crossAxisGap;
                const int container = constraints.crossAxisMin;
                if (totalLinesCrossSize + totalSpacing >= container) return totalLinesCrossSize;
                int updated = totalLinesCrossSize;
                const int extra = std::max(container - totalLinesCrossSize - totalSpacing, 0);
                const int perLine = extra / int(lines.size());
                int y = 0;
                for (auto& line : lines) {
                    line.crossStart = y;
                    line.crossAxisSize += perLine;
                    y += line.crossAxisSize + crossAxisGap;
                    updated += perLine;
                }
                return updated;
            }

            static int MeasureFlexItems(std::vector<Line>& lines, std::vector<Item>& items, const BoxConfig& config, int totalLinesCrossSize, bool needsUpfront,
                                        const AxisConstraints& constraints) {
                int updatedTotal = totalLinesCrossSize;
                int remainingCrossAxisSize = constraints.crossAxisMax;
                for (auto& line : lines) {
                    int lineCross = needsUpfront ? line.crossAxisSize : 0;
                    for (std::size_t i = line.startIndex; i < line.endIndex; ++i) {
                        Item& item = items[i];
                        if (item.placeable) {
                            if (!needsUpfront) lineCross = std::max(lineCross, item.crossAxisSize);
                            continue;
                        }
                        const bool shouldStretch = item.alignSelf == FlexAlignSelf::Stretch || (item.alignSelf == FlexAlignSelf::Auto && config.alignItems == FlexAlignItems::Stretch);
                        const int cross = MeasureItem(item, config, shouldStretch ? line.crossAxisSize : 0, shouldStretch, remainingCrossAxisSize);
                        if (!needsUpfront) lineCross = std::max(lineCross, cross);
                        if (!config.IsWrapEnabled()) lineCross = std::max(lineCross, constraints.crossAxisMin);
                    }
                    if (!needsUpfront) {
                        line.crossAxisSize = lineCross;
                        updatedTotal += lineCross;
                    }
                    remainingCrossAxisSize = std::max(remainingCrossAxisSize - line.crossAxisSize - config.CrossAxisGap(), 0);
                }
                return needsUpfront ? totalLinesCrossSize : updatedTotal;
            }

            static void CalculateLineCrossPositions(const BoxConfig& config, int totalCrossAxisSpace, std::vector<Line>& lines, int totalLinesCrossSize, int crossAxisGap) {
                if (!config.IsWrapEnabled() || lines.empty()) return;
                const int n = int(lines.size());
                const int freeSpace = totalCrossAxisSpace - totalLinesCrossSize - (n - 1) * crossAxisGap;
                int between = 0;
                if (config.alignContent == FlexAlignContent::SpaceAround) between = freeSpace / n;
                else if (config.alignContent == FlexAlignContent::SpaceBetween) between = n > 1 ? freeSpace / (n - 1) : 0;
                int crossPosition;
                switch (config.alignContent) {
                    case FlexAlignContent::End: crossPosition = config.IsCrossAxisReverse() ? 0 : freeSpace; break;
                    case FlexAlignContent::Center: crossPosition = freeSpace / 2; break;
                    case FlexAlignContent::SpaceAround: crossPosition = between / 2; break;
                    default: crossPosition = config.IsCrossAxisReverse() ? freeSpace : 0; break;
                }
                ForEach(lines, config.IsCrossAxisReverse(), 0, lines.size(), [&](Line& line) {
                    line.crossStart = crossPosition;
                    crossPosition += line.crossAxisSize + between + crossAxisGap;
                });
            }

            static int TotalCrossAxisSize(const std::vector<Line>& lines, bool reverse) {
                if (lines.empty()) return 0;
                const Line& l = reverse ? lines.front() : lines.back();
                return l.crossStart + l.crossAxisSize;
            }

            static void PositionItemsOnMainAxis(std::vector<Item>& items, FlexJustifyContent justify, FlexAlignItems alignItems, int containerMainAxisSize, const Line& line,
                                                int mainAxisGap, bool isMainAxisReverse) {
                const int itemCount = int(line.endIndex - line.startIndex);
                if (itemCount == 0) return;
                const int remaining = containerMainAxisSize - line.mainAxisSize;
                int between = 0;
                switch (justify) {
                    case FlexJustifyContent::SpaceAround: between = remaining / itemCount; break;
                    case FlexJustifyContent::SpaceBetween: between = itemCount > 1 ? remaining / (itemCount - 1) : 0; break;
                    case FlexJustifyContent::SpaceEvenly: between = remaining / (itemCount + 1); break;
                    default: break;
                }
                int mainPosition;
                switch (justify) {
                    case FlexJustifyContent::End: mainPosition = isMainAxisReverse ? 0 : remaining; break;
                    case FlexJustifyContent::Center: mainPosition = remaining / 2; break;
                    case FlexJustifyContent::SpaceAround: mainPosition = between / 2; break;
                    case FlexJustifyContent::SpaceEvenly: mainPosition = between; break;
                    case FlexJustifyContent::SpaceBetween: mainPosition = itemCount == 1 && isMainAxisReverse ? remaining : 0; break;
                    default: mainPosition = isMainAxisReverse ? remaining : 0; break;
                }
                ForEach(items, isMainAxisReverse, line.startIndex, line.endIndex, [&](Item& item) {
                    item.mainPosition = mainPosition;
                    mainPosition += item.targetMainSize + between + mainAxisGap;
                    item.crossPosition = line.crossStart + CalculateItemCrossPosition(item, line.maxAboveBaseline, line.crossAxisSize, alignItems);
                });
            }

            static int CalculateItemCrossPosition(const Item& item, int lineMaxAboveBaseline, int lineCrossAxisSize, FlexAlignItems containerAlignItems) {
                FlexAlignSelf a = item.alignSelf;
                if (a == FlexAlignSelf::Auto) {
                    switch (containerAlignItems) {
                        case FlexAlignItems::End: a = FlexAlignSelf::End; break;
                        case FlexAlignItems::Center: a = FlexAlignSelf::Center; break;
                        case FlexAlignItems::Stretch: a = FlexAlignSelf::Stretch; break;
                        case FlexAlignItems::Baseline: a = FlexAlignSelf::Baseline; break;
                        default: a = FlexAlignSelf::Start; break;
                    }
                }
                switch (a) {
                    case FlexAlignSelf::End: return lineCrossAxisSize - item.crossAxisSize;
                    case FlexAlignSelf::Center: return (lineCrossAxisSize - item.crossAxisSize) / 2;
                    case FlexAlignSelf::Baseline: return item.baseline != AlignmentLine::Unspecified ? lineMaxAboveBaseline - item.baseline : 0;
                    default: return 0;
                }
            }

            static void CalculateLineCrossAxisSize(std::vector<Item>& items, const BoxConfig& config, Line& line, int remainingCrossAxisSize) {
                int lineCross = 0, maxAbove = 0, maxBelow = 0;
                const bool isHorizontal = config.IsHorizontal();
                for (std::size_t i = line.startIndex; i < line.endIndex; ++i) {
                    Item& item = items[i];
                    int cross;
                    if (item.HasBaseline() || (config.HasBaseline() && item.alignSelf == FlexAlignSelf::Auto)) {
                        MeasureItem(item, config, 0, false, remainingCrossAxisSize);
                        const int baseline = item.GetBaseline(*item.placeable, config);
                        item.baseline = baseline;
                        maxAbove = std::max(maxAbove, baseline);
                        maxBelow = std::max(maxBelow, item.crossAxisSize - baseline);
                        cross = maxAbove + maxBelow;
                    } else {
                        item.crossAxisSize = !item.measurable ? 0
                                             : isHorizontal ? item.measurable->MaxIntrinsicHeight(item.targetMainSize)
                                                            : item.measurable->MaxIntrinsicWidth(item.targetMainSize);
                        cross = item.crossAxisSize;
                    }
                    lineCross = std::max(lineCross, cross);
                }
                line.maxAboveBaseline = maxAbove;
                line.crossAxisSize = lineCross;
            }

            static int MeasureItem(Item& item, const BoxConfig& config, int lineCrossAxisSize, bool shouldStretch, int remainingCrossAxisSize) {
                const bool isHorizontal = config.IsHorizontal();
                Constraints c;
                if (shouldStretch && lineCrossAxisSize > 0) {
                    const int cross = std::min(lineCrossAxisSize, remainingCrossAxisSize);
                    c = isHorizontal ? Constraints::Fixed(item.targetMainSize, cross) : Constraints::Fixed(cross, item.targetMainSize);
                } else if (isHorizontal) {
                    c = Constraints{ item.targetMainSize, item.targetMainSize, 0, remainingCrossAxisSize };   // fitPrioritizingWidth
                } else {
                    c = Constraints{ 0, remainingCrossAxisSize, item.targetMainSize, item.targetMainSize };   // fitPrioritizingHeight
                }
                item.placeable = item.measurable ? &item.measurable->Measure(c) : nullptr;
                item.crossAxisSize = !item.placeable ? 0 : isHorizontal ? item.placeable->Height() : item.placeable->Width();
                item.mainAxisSize = !item.placeable ? 0 : isHorizontal ? item.placeable->Width() : item.placeable->Height();
                return item.crossAxisSize;
            }

            FlexBoxConfig m_Config;
            BoxConfig m_Resolved;
        };
    }

    void FlexBox(const Modifier& modifier, FlexBoxConfig config, kiln::Function<void()> content) {
        auto* c = rt::Composition::Current();
        c->Node<LayoutNode>([] { return std::make_unique<LayoutNode>(); },
                            [&](LayoutNode& n) {
                                n.SetModifier(modifier);
                                TakeLocalLayoutDirection(n);
                                if (auto* cur = dynamic_cast<FlexBoxPolicy*>(n.Policy().get()); !cur || !(cur->Config() == config))
                                    n.SetMeasurePolicy(std::make_shared<FlexBoxPolicy>(config));
                            }, std::move(content));
    }

    Modifier Flex(Modifier m, FlexConfig config) { return m.With<FlexChildNode>(FlexChildParams{ std::move(config) }); }

}
