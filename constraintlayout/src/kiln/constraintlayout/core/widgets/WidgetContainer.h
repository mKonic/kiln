#pragma once

// WidgetContainer (constraintlayout-core): a widget with children. Children are not owned: whoever
// builds the layout keeps the widgets alive while they are in a container.

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <initializer_list>
#include <vector>

namespace kiln::constraintlayout::core {

    class WidgetContainer : public ConstraintWidget {
    public:
        WidgetContainer() { mKind |= KIND_WIDGET_CONTAINER; }
        WidgetContainer(int x, int y, int width, int height) : ConstraintWidget(x, y, width, height) { mKind |= KIND_WIDGET_CONTAINER; }
        WidgetContainer(int width, int height) : ConstraintWidget(width, height) { mKind |= KIND_WIDGET_CONTAINER; }

        std::vector<ConstraintWidget*> mChildren;

        void reset() override;
        void add(ConstraintWidget* widget);
        void add(std::initializer_list<ConstraintWidget*> widgets) { for (ConstraintWidget* w : widgets) add(w); }
        void remove(ConstraintWidget* widget);
        std::vector<ConstraintWidget*>& getChildren() { return mChildren; }
        ConstraintWidgetContainer* getRootConstraintContainer();
        void setOffset(int x, int y) override;
        virtual void layout();
        void resetSolverVariables(Cache* cache) override;
        void removeAllChildren() { mChildren.clear(); }
    };

}
