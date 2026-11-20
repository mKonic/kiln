#pragma once

// HelperWidget (and Helper, which only it implements): a widget that refers to others, such as a
// barrier or a flow.

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <memory>
#include <vector>

namespace kiln::constraintlayout::core {

    class HelperWidget : public ConstraintWidget {
    public:
        HelperWidget() { mKind |= KIND_HELPER_WIDGET; }

        std::vector<ConstraintWidget*> mWidgets = std::vector<ConstraintWidget*>(4);
        int mWidgetsCount = 0;

        virtual void updateConstraints(ConstraintWidgetContainer* /*container*/) {}
        virtual void add(ConstraintWidget* widget);
        void copy(const ConstraintWidget& src, const WidgetMap& map) override;
        virtual void removeAllIds();
        void addDependents(std::vector<std::shared_ptr<WidgetGroup>>& dependencyLists, int orientation, WidgetGroup* group);
        int findGroupInDependents(int orientation) const;
    };

}
