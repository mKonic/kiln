#pragma once

// Grouping and WidgetGroup (constraintlayout-core): independent groups of widgets, measured on
// their own to size a wrap-content container (Optimizer::OPTIMIZATION_GROUPING).

#include "kiln/constraintlayout/core/widgets/analyzer/BasicMeasure.h"

#include <memory>
#include <vector>

namespace kiln::constraintlayout::core {

    class WidgetGroup {
    public:
        explicit WidgetGroup(int orientation) : mId(sCount++), mOrientation(orientation) {}

        int getOrientation() const { return mOrientation; }
        int getId() const { return mId; }
        bool add(ConstraintWidget* widget);
        void setAuthoritative(bool v) { mAuthoritative = v; }
        bool isAuthoritative() const { return mAuthoritative; }
        void moveTo(int orientation, WidgetGroup* widgetGroup);
        void clear() { mWidgets.clear(); }
        int measureWrap(LinearSystem& system, int orientation);
        void setOrientation(int orientation) { mOrientation = orientation; }
        void apply();
        bool intersectWith(const WidgetGroup& group) const;
        int size() const { return int(mWidgets.size()); }
        void cleanup(std::vector<std::shared_ptr<WidgetGroup>>& dependencyLists);
        bool contains(const ConstraintWidget* widget) const;

        std::vector<ConstraintWidget*> mWidgets;
        static inline int sCount = 0;

    private:
        struct MeasureResult {
            ConstraintWidget* mWidget;
            int mLeft, mTop, mRight, mBottom, mBaseline, mOrientation;
            MeasureResult(ConstraintWidget* widget, LinearSystem& system, int orientation);
            void apply() const;
        };
        int solverMeasure(LinearSystem& system, std::vector<ConstraintWidget*>& widgets, int orientation);

        int mId = -1;
        bool mAuthoritative = false;
        int mOrientation = ConstraintWidget::HORIZONTAL;
        std::vector<MeasureResult> mResults;
        bool mHasResults = false;
        int mMoveTo = -1;
    };

    struct Grouping {
        static bool validInGroup(ConstraintWidget::DimensionBehaviour layoutHorizontal, ConstraintWidget::DimensionBehaviour layoutVertical,
                                 ConstraintWidget::DimensionBehaviour widgetHorizontal, ConstraintWidget::DimensionBehaviour widgetVertical);
        static bool simpleSolvingPass(ConstraintWidgetContainer* layout, BasicMeasure::Measurer* measurer);
        static WidgetGroup* findDependents(ConstraintWidget* constraintWidget, int orientation, std::vector<std::shared_ptr<WidgetGroup>>& list,
                                           WidgetGroup* group);
        // A group taken out of a list stays alive until the pass ends: callers may still hold it.
        static void remove(std::vector<std::shared_ptr<WidgetGroup>>& list, WidgetGroup* group);

    private:
        static WidgetGroup* findGroup(std::vector<std::shared_ptr<WidgetGroup>>& list, int groupId);
    };

}
