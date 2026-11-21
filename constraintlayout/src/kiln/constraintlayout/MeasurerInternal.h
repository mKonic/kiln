#pragma once

// constraintlayout-compose's Measurer2 (Measurer.kt), shared by ConstraintLayout and
// MotionLayout's MotionMeasurer; with what a child tells its layout through parent data.

#include "kiln/constraintlayout/ConstraintLayout.h"

#include "kiln/constraintlayout/core/state/State.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"
#include "kiln/constraintlayout/core/widgets/VirtualLayout.h"
#include "kiln/foundation/Layouts.h"

#include <any>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace kiln::constraintlayout::detail {

    using core::BasicMeasure;
    using core::ConstraintWidget;
    using core::ConstraintWidgetContainer;
    using core::WidgetFrame;
    using DB = ConstraintWidget::DimensionBehaviour;

    // What a child tells its ConstraintLayout (ConstraintLayoutParentData and
    // ConstraintLayoutTagParentData); kept in ParentData::extra.
    struct ConstrainAsData {
        ConstrainedLayoutReference ref;
        std::shared_ptr<const kiln::Function<void(ConstrainScope&)>> constrain;
    };
    struct ChildData {
        std::shared_ptr<const ConstrainAsData> constrainAs;
        std::optional<std::string> tag;
    };
    inline const ChildData* ChildDataOf(const ui::Measurable& m) { return std::any_cast<ChildData>(&m.GetParentData().extra); }
    inline ChildData& ChildDataIn(ui::ParentData& d) {
        if (ChildData* existing = std::any_cast<ChildData>(&d.extra)) return *existing;
        d.extra = ChildData{};
        return *std::any_cast<ChildData>(&d.extra);
    }

    // The id a child goes by (Measurable.anyOrNullId).
    inline std::string AnyOrNullId(const ui::Measurable& m) {
        if (const std::string* id = foundation::LayoutIdOf(m)) return *id;
        return "null";
    }

    // compose's State: the solver state with the incoming constraints.
    class ComposeState final : public core::state::State {
    public:
        ui::Constraints rootIncomingConstraints;
    };

    inline int CoerceIn(int v, int lo, int hi) {
        // Kotlin's coerceIn with null bounds (given as 0 or less here).
        if (lo > 0 && hi > 0 && lo > hi) return v;   // upstream would throw; keep the size
        if (lo > 0 && v < lo) v = lo;
        if (hi > 0 && v > hi) v = hi;
        return v;
    }

    // Measurer2: MotionMeasurer extends it.
    class Measurer : public BasicMeasure::Measurer {
    public:
        Measurer() { m_Root.setMeasurer(this); }

        void measure(ConstraintWidget* constraintWidget, BasicMeasure::Measure& measure) override {
            const std::string& widgetId = constraintWidget->stringId;
            const auto last = m_LastMeasures.find(widgetId);
            int widthConstraints[2], heightConstraints[2];
            ObtainConstraints(measure.horizontalBehavior, measure.horizontalDimension, constraintWidget->mMatchConstraintDefaultWidth,
                              measure.measureStrategy, (last != m_LastMeasures.end() ? last->second[1] : 0) == constraintWidget->getHeight(),
                              constraintWidget->isResolvedHorizontally(), m_State.rootIncomingConstraints.maxWidth, widthConstraints);
            ObtainConstraints(measure.verticalBehavior, measure.verticalDimension, constraintWidget->mMatchConstraintDefaultHeight,
                              measure.measureStrategy, (last != m_LastMeasures.end() ? last->second[0] : 0) == constraintWidget->getWidth(),
                              constraintWidget->isResolvedVertically(), m_State.rootIncomingConstraints.maxHeight, heightConstraints);
            ui::Constraints constraints{ widthConstraints[0], widthConstraints[1], heightConstraints[0], heightConstraints[1] };

            if ((measure.measureStrategy == BasicMeasure::Measure::TRY_GIVEN_DIMENSIONS ||
                 measure.measureStrategy == BasicMeasure::Measure::USE_GIVEN_DIMENSIONS) ||
                !(measure.horizontalBehavior == DB::MATCH_CONSTRAINT &&
                  constraintWidget->mMatchConstraintDefaultWidth == ConstraintWidget::MATCH_CONSTRAINT_SPREAD &&
                  measure.verticalBehavior == DB::MATCH_CONSTRAINT &&
                  constraintWidget->mMatchConstraintDefaultHeight == ConstraintWidget::MATCH_CONSTRAINT_SPREAD)) {
                const ui::IntSize result = MeasureWidget(constraintWidget, constraints);
                constraintWidget->setMeasureRequested(false);
                const int coercedWidth = CoerceIn(result.width, constraintWidget->mMatchConstraintMinWidth, constraintWidget->mMatchConstraintMaxWidth);
                const int coercedHeight =
                    CoerceIn(result.height, constraintWidget->mMatchConstraintMinHeight, constraintWidget->mMatchConstraintMaxHeight);
                bool remeasure = false;
                if (coercedWidth != result.width) {
                    constraints = { coercedWidth, coercedWidth, constraints.minHeight, constraints.maxHeight };
                    remeasure = true;
                }
                if (coercedHeight != result.height) {
                    constraints = { constraints.minWidth, constraints.maxWidth, coercedHeight, coercedHeight };
                    remeasure = true;
                }
                if (remeasure) {
                    MeasureWidget(constraintWidget, constraints);
                    constraintWidget->setMeasureRequested(false);
                }
            }

            auto* measurable = static_cast<ui::Measurable*>(constraintWidget->getCompanionWidget());
            const auto placeable = measurable ? m_Placeables.find(measurable) : m_Placeables.end();
            ui::Placeable* currentPlaceable = placeable != m_Placeables.end() ? placeable->second : nullptr;
            measure.measuredWidth = currentPlaceable ? currentPlaceable->Width() : constraintWidget->getWidth();
            measure.measuredHeight = currentPlaceable ? currentPlaceable->Height() : constraintWidget->getHeight();
            const int baseline = currentPlaceable != nullptr && m_State.isBaselineNeeded(constraintWidget) ? currentPlaceable->Get(ui::FirstBaseline)
                                                                                                          : ui::AlignmentLine::Unspecified;
            measure.measuredHasBaseline = baseline != ui::AlignmentLine::Unspecified;
            measure.measuredBaseline = baseline;
            auto& saved = m_LastMeasures.try_emplace(widgetId, std::array<int, 3>{ 0, 0, ui::AlignmentLine::Unspecified }).first->second;
            saved = { measure.measuredWidth, measure.measuredHeight, measure.measuredBaseline };
            measure.measuredNeedsSolverPass = measure.measuredWidth != measure.horizontalDimension || measure.measuredHeight != measure.verticalDimension;
        }

        void didMeasures() override {}

        ui::IntSize PerformMeasure(const ui::MeasureScope& scope, const ui::Constraints& constraints, ConstraintSet& constraintSet,
                                   std::span<ui::Measurable* const> measurables, int optimizationLevel) {
            if (measurables.empty()) return { constraints.minWidth, constraints.minHeight };
            const float density = scope.density;
            m_State.setDpToPixel([density](float dp) { return density * dp; });

            m_State.width(constraints.HasFixedWidth() ? core::state::Dimension::createFixed(constraints.maxWidth)
                                                      : core::state::Dimension::createWrap().min(constraints.minWidth));
            m_State.height(constraints.HasFixedHeight() ? core::state::Dimension::createFixed(constraints.maxHeight)
                                                        : core::state::Dimension::createWrap().min(constraints.minHeight));
            m_State.mParent->getWidth().apply(m_State, &m_Root, ConstraintWidget::HORIZONTAL);
            m_State.mParent->getHeight().apply(m_State, &m_Root, ConstraintWidget::VERTICAL);
            m_State.rootIncomingConstraints = constraints;
            m_State.setRtl(scope.Direction() == ui::LayoutDirection::Rtl);

            ResetMeasureState();
            if (constraintSet.IsDirty(measurables)) {
                m_State.reset();
                constraintSet.ApplyTo(m_State, measurables);
                BuildMapping(measurables);
                m_State.apply(m_Root);
            } else {
                BuildMapping(measurables);
            }

            m_Root.setWidth(constraints.maxWidth);
            m_Root.setHeight(constraints.maxHeight);
            m_Root.updateHierarchy();
            m_Root.setOptimizationLevel(optimizationLevel);
            m_Root.measure(m_Root.getOptimizationLevel(), 0, 0, 0, 0, 0, 0, 0, 0);
            return { m_Root.getWidth(), m_Root.getHeight() };
        }

        void PerformLayout(ui::PlacementScope& place, std::span<ui::Measurable* const> measurables) {
            if (m_FrameCache.empty()) {
                for (ConstraintWidget* child : m_Root.getChildren()) {
                    auto* measurable = static_cast<ui::Measurable*>(child->getCompanionWidget());
                    if (measurable == nullptr || !m_Known.count(measurable)) continue;
                    WidgetFrame frame(child->frame);
                    frame.update();
                    m_FrameCache[AnyOrNullId(*measurable)] = frame;
                }
            }
            for (ui::Measurable* measurable : measurables) {
                const auto frame = m_FrameCache.find(AnyOrNullId(*measurable));
                if (frame == m_FrameCache.end()) continue;
                const auto placeable = m_Placeables.find(measurable);
                if (placeable == m_Placeables.end()) continue;
                PlaceWithFrameTransform(place, *placeable->second, frame->second);
            }
        }

    protected:
        static bool ObtainConstraints(DB dimensionBehaviour, int dimension, int matchConstraintDefaultDimension, int measureStrategy,
                                      bool otherDimensionResolved, bool currentDimensionResolved, int rootMaxConstraint, int* outConstraints) {
            switch (dimensionBehaviour) {
                case DB::FIXED:
                    outConstraints[0] = dimension;
                    outConstraints[1] = dimension;
                    return false;
                case DB::WRAP_CONTENT:
                    outConstraints[0] = 0;
                    outConstraints[1] = rootMaxConstraint;
                    return true;
                case DB::MATCH_CONSTRAINT: {
                    const bool useDimension =
                        currentDimensionResolved ||
                        ((measureStrategy == BasicMeasure::Measure::TRY_GIVEN_DIMENSIONS || measureStrategy == BasicMeasure::Measure::USE_GIVEN_DIMENSIONS) &&
                         (measureStrategy == BasicMeasure::Measure::USE_GIVEN_DIMENSIONS ||
                          matchConstraintDefaultDimension != ConstraintWidget::MATCH_CONSTRAINT_WRAP || otherDimensionResolved));
                    outConstraints[0] = useDimension ? dimension : 0;
                    outConstraints[1] = useDimension ? dimension : rootMaxConstraint;
                    return !useDimension;
                }
                case DB::MATCH_PARENT:
                    outConstraints[0] = rootMaxConstraint;
                    outConstraints[1] = rootMaxConstraint;
                    return false;
            }
            return false;
        }

        ui::IntSize MeasureWidget(ConstraintWidget* constraintWidget, const ui::Constraints& constraints) {
            if (constraintWidget->is(ConstraintWidget::KIND_VIRTUAL_LAYOUT)) {
                const int widthMode = constraints.HasFixedWidth() ? BasicMeasure::EXACTLY
                                      : constraints.HasBoundedWidth() ? BasicMeasure::AT_MOST
                                                                       : BasicMeasure::UNSPECIFIED;
                const int heightMode = constraints.HasFixedHeight() ? BasicMeasure::EXACTLY
                                       : constraints.HasBoundedHeight() ? BasicMeasure::AT_MOST
                                                                         : BasicMeasure::UNSPECIFIED;
                auto* layout = static_cast<core::VirtualLayout*>(constraintWidget);
                layout->measure(widthMode, constraints.maxWidth, heightMode, constraints.maxHeight);
                return { layout->getMeasuredWidth(), layout->getMeasuredHeight() };
            }
            auto* measurable = static_cast<ui::Measurable*>(constraintWidget->getCompanionWidget());
            if (measurable != nullptr && m_Known.count(measurable)) {
                ui::Placeable& result = measurable->Measure(constraints);
                m_Placeables[measurable] = &result;
                return { result.Width(), result.Height() };
            }
            return { 0, 0 };   // upstream logs "Nothing to measure"
        }

        // buildMapping: each child's id and the child itself, and its tag.
        void BuildMapping(std::span<ui::Measurable* const> measurables) {
            m_Known.clear();
            for (ui::Measurable* measurable : measurables) {
                m_Known.insert(measurable);
                const std::string* layoutId = foundation::LayoutIdOf(*measurable);
                // createId(): a fresh object, so a fresh id, for a child without one.
                const std::string id = layoutId ? *layoutId : "\x02" + std::to_string(++m_AnonymousIds);
                m_State.map(id, measurable);
                const ChildData* data = ChildDataOf(*measurable);
                if (data && data->tag && layoutId) m_State.setTag(id, *data->tag);
            }
        }

        void ResetMeasureState() {
            m_Placeables.clear();
            m_LastMeasures.clear();
            m_FrameCache.clear();
        }

        // placeWithFrameTransform: gone children are not placed; a transform makes a layer.
        static void PlaceWithFrameTransform(ui::PlacementScope& place, ui::Placeable& placeable, const WidgetFrame& frame) {
            if (frame.visibility == ConstraintWidget::GONE) return;
            if (frame.isDefaultTransform()) {
                place.Place(placeable, frame.left, frame.top);
                return;
            }
            foundation::GraphicsLayerParams layer;
            if (!std::isnan(frame.pivotX) || !std::isnan(frame.pivotY)) {
                layer.originX = std::isnan(frame.pivotX) ? 0.5f : frame.pivotX;
                layer.originY = std::isnan(frame.pivotY) ? 0.5f : frame.pivotY;
            }
            if (!std::isnan(frame.rotationX)) layer.rotationX = frame.rotationX;
            if (!std::isnan(frame.rotationY)) layer.rotationY = frame.rotationY;
            if (!std::isnan(frame.rotationZ)) layer.rotationZ = frame.rotationZ;
            if (!std::isnan(frame.translationX)) layer.translationX = frame.translationX;
            if (!std::isnan(frame.translationY)) layer.translationY = frame.translationY;
            if (!std::isnan(frame.translationZ)) layer.shadowElevation = frame.translationZ;
            if (!std::isnan(frame.scaleX) || !std::isnan(frame.scaleY)) {
                layer.scaleX = std::isnan(frame.scaleX) ? 1.0f : frame.scaleX;
                layer.scaleY = std::isnan(frame.scaleY) ? 1.0f : frame.scaleY;
            }
            if (!std::isnan(frame.alpha)) layer.alpha = frame.alpha;
            const float zIndex = std::isnan(frame.translationZ) ? 0.0f : frame.translationZ;
            place.PlaceWithLayer(placeable, frame.left, frame.top, foundation::PlacementLayer(layer), zIndex);
        }

        ConstraintWidgetContainer m_Root{ 0, 0 };
        ComposeState m_State;
        std::unordered_map<ui::Measurable*, ui::Placeable*> m_Placeables;
        std::unordered_set<ui::Measurable*> m_Known;   // this pass's children (companions may be stale)
        std::unordered_map<std::string, std::array<int, 3>> m_LastMeasures;
        std::unordered_map<std::string, WidgetFrame> m_FrameCache;
        int m_AnonymousIds = 0;
    };

    // RawConstraintSet over a copy of an inline DSL's constraints.
    std::shared_ptr<ConstraintSet> MakeRawConstraintSet(std::shared_ptr<core::CLObject> clObject);

}
