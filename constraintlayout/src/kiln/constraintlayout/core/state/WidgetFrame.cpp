#include "kiln/constraintlayout/core/state/WidgetFrame.h"

#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

namespace kiln::constraintlayout::core {

    WidgetFrame& WidgetFrame::update() {
        if (widget != nullptr) {
            left = widget->getLeft();
            top = widget->getTop();
            right = widget->getRight();
            bottom = widget->getBottom();
            updateAttributes(widget->frame);
        }
        return *this;
    }

}
