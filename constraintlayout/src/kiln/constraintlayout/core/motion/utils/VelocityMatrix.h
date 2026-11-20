#pragma once

// VelocityMatrix (constraintlayout-core motion utils): how scale, translation and rotation
// splines and cycles move a point of a widget, added to its path velocity.

#include "kiln/constraintlayout/core/motion/utils/KeyCycleOscillator.h"
#include "kiln/constraintlayout/core/motion/utils/SplineSet.h"

#include <cmath>

namespace kiln::constraintlayout::core::motion {

    class VelocityMatrix {
    public:
        void clear() { mDScaleX = mDScaleY = mDTranslateX = mDTranslateY = mDRotate = 0; }
        void setRotationVelocity(SplineSet* rot, float position) {
            if (rot != nullptr) {
                mDRotate = rot->getSlope(position);
                mRotate = rot->get(position);
            }
        }
        void setTranslationVelocity(SplineSet* transX, SplineSet* transY, float position) {
            if (transX != nullptr) mDTranslateX = transX->getSlope(position);
            if (transY != nullptr) mDTranslateY = transY->getSlope(position);
        }
        void setScaleVelocity(SplineSet* scaleX, SplineSet* scaleY, float position) {
            if (scaleX != nullptr) mDScaleX = scaleX->getSlope(position);
            if (scaleY != nullptr) mDScaleY = scaleY->getSlope(position);
        }
        void setRotationVelocity(KeyCycleOscillator* oscR, float position) {
            if (oscR != nullptr) mDRotate = oscR->getSlope(position);
        }
        void setTranslationVelocity(KeyCycleOscillator* oscX, KeyCycleOscillator* oscY, float position) {
            if (oscX != nullptr) mDTranslateX = oscX->getSlope(position);
            if (oscY != nullptr) mDTranslateY = oscY->getSlope(position);
        }
        void setScaleVelocity(KeyCycleOscillator* oscSx, KeyCycleOscillator* oscSy, float position) {
            if (oscSx != nullptr) mDScaleX = oscSx->getSlope(position);
            if (oscSy != nullptr) mDScaleY = oscSy->getSlope(position);
        }
        void applyTransform(float locationX, float locationY, int width, int height, float* anchorDpDt) const {
            constexpr double kDegreesToRadians = 0.017453292519943295;
            float dx = anchorDpDt[0];
            float dy = anchorDpDt[1];
            const float offx = 2 * (locationX - 0.5f);
            const float offy = 2 * (locationY - 0.5f);
            dx += mDTranslateX;
            dy += mDTranslateY;
            dx += offx * mDScaleX;
            dy += offy * mDScaleY;
            const float r = float(mRotate * kDegreesToRadians);
            const float dr = float(mDRotate * kDegreesToRadians);
            dx += dr * float(-width * offx * std::sin(r) - height * offy * std::cos(r));
            dy += dr * float(width * offx * std::cos(r) - height * offy * std::sin(r));
            anchorDpDt[0] = dx;
            anchorDpDt[1] = dy;
        }

    private:
        float mDScaleX = 0, mDScaleY = 0, mDTranslateX = 0, mDTranslateY = 0, mDRotate = 0;
        float mRotate = 0;
    };

}
