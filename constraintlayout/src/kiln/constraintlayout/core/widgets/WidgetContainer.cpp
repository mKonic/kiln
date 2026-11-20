#include "kiln/constraintlayout/core/widgets/WidgetContainer.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidgetContainer.h"

#include <algorithm>

namespace kiln::constraintlayout::core {

    void WidgetContainer::reset() {
        mChildren.clear();
        ConstraintWidget::reset();
    }

    void WidgetContainer::add(ConstraintWidget* widget) {
        mChildren.push_back(widget);
        if (widget->getParent() != nullptr) {
            auto* container = static_cast<WidgetContainer*>(widget->getParent());
            container->remove(widget);
        }
        widget->setParent(this);
    }

    void WidgetContainer::remove(ConstraintWidget* widget) {
        // ArrayList.remove(Object): the first occurrence.
        if (auto it = std::find(mChildren.begin(), mChildren.end(), widget); it != mChildren.end()) mChildren.erase(it);
        widget->reset();
    }

    ConstraintWidgetContainer* WidgetContainer::getRootConstraintContainer() {
        ConstraintWidget* item = this;
        ConstraintWidget* parent = item->getParent();
        ConstraintWidgetContainer* container = nullptr;
        if (item->is(KIND_CONSTRAINT_WIDGET_CONTAINER)) container = static_cast<ConstraintWidgetContainer*>(this);
        while (parent != nullptr) {
            item = parent;
            parent = item->getParent();
            if (item->is(KIND_CONSTRAINT_WIDGET_CONTAINER)) container = static_cast<ConstraintWidgetContainer*>(item);
        }
        return container;
    }

    void WidgetContainer::setOffset(int x, int y) {
        ConstraintWidget::setOffset(x, y);
        for (ConstraintWidget* widget : mChildren) widget->setOffset(getRootX(), getRootY());
    }

    void WidgetContainer::layout() {
        for (ConstraintWidget* widget : mChildren)
            if (widget->is(KIND_WIDGET_CONTAINER)) static_cast<WidgetContainer*>(widget)->layout();
    }

    void WidgetContainer::resetSolverVariables(Cache* cache) {
        ConstraintWidget::resetSolverVariables(cache);
        for (ConstraintWidget* widget : mChildren) widget->resetSolverVariables(cache);
    }

}
