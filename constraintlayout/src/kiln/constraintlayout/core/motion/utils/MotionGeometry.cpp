#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"

#include "kiln/constraintlayout/core/LinearSystem.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"

namespace kiln::constraintlayout::core::motion {

    void ViewState::getState(MotionWidget& v) {
        left = v.getLeft();
        top = v.getTop();
        right = v.getRight();
        bottom = v.getBottom();
        rotation = float(JavaInt(v.getRotationZ()));
    }

}
