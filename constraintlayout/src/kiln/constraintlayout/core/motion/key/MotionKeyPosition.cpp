#include "kiln/constraintlayout/core/motion/key/MotionKeyPosition.h"

#include "kiln/constraintlayout/core/LinearSystem.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"

#include <cstdio>

namespace kiln::constraintlayout::core::motion {

    using PT = TypedValues::PositionType;

    void MotionKeyPosition::calcScreenPosition(int layoutWidth, int layoutHeight) {
        const int viewWidth = 0;
        const int viewHeight = 0;
        mCalculatedPositionX = (layoutWidth - viewWidth) * mPercentX + float(viewWidth / 2);
        mCalculatedPositionY = (layoutHeight - viewHeight) * mPercentX + float(viewHeight / 2);   // percentX, as upstream
    }

    void MotionKeyPosition::calcPathPosition(float startX, float startY, float endX, float endY) {
        const float pathVectorX = endX - startX;
        const float pathVectorY = endY - startY;
        const float perpendicularX = -pathVectorY;
        const float perpendicularY = pathVectorX;
        mCalculatedPositionX = startX + pathVectorX * mPercentX + perpendicularX * mPercentY;
        mCalculatedPositionY = startY + pathVectorY * mPercentX + perpendicularY * mPercentY;
    }

    void MotionKeyPosition::calcCartesianPosition(float startX, float startY, float endX, float endY) {
        const float pathVectorX = endX - startX;
        const float pathVectorY = endY - startY;
        const float dxdx = std::isnan(mPercentX) ? 0 : mPercentX;
        const float dydx = std::isnan(mAltPercentY) ? 0 : mAltPercentY;
        const float dydy = std::isnan(mPercentY) ? 0 : mPercentY;
        const float dxdy = std::isnan(mAltPercentX) ? 0 : mAltPercentX;
        mCalculatedPositionX = float(JavaInt(startX + pathVectorX * dxdx + pathVectorY * dxdy));
        mCalculatedPositionY = float(JavaInt(startY + pathVectorX * dydx + pathVectorY * dydy));
    }

    void MotionKeyPosition::positionAttributes(MotionWidget& view, const FloatRect& start, const FloatRect& end, float x, float y, std::string* attribute,
                                               float* value) {
        switch (mPositionType) {
            case TYPE_PATH: positionPathAttributes(start, end, x, y, attribute, value); return;
            case TYPE_SCREEN: positionScreenAttributes(view, start, end, x, y, attribute, value); return;
            case TYPE_CARTESIAN:
            default: positionCartAttributes(start, end, x, y, attribute, value); return;
        }
    }

    void MotionKeyPosition::positionPathAttributes(const FloatRect& start, const FloatRect& end, float x, float y, std::string* attribute, float* value) {
        const float startCenterX = start.centerX();
        const float startCenterY = start.centerY();
        const float pathVectorX = end.centerX() - startCenterX;
        const float pathVectorY = end.centerY() - startCenterY;
        const float distance = float(std::hypot(double(pathVectorX), double(pathVectorY)));
        if (distance < 0.0001) {
            std::printf("distance ~ 0\n");
            value[0] = 0;
            value[1] = 0;
            return;
        }
        const float dx = pathVectorX / distance;
        const float dy = pathVectorY / distance;
        const float perpendicular = (dx * (y - startCenterY) - (x - startCenterX) * dy) / distance;
        const float dist = (dx * (x - startCenterX) + dy * (y - startCenterY)) / distance;
        if (!attribute[0].empty()) {
            if (attribute[0] == PT::S_PERCENT_X) {
                value[0] = dist;
                value[1] = perpendicular;
            }
        } else {
            attribute[0] = PT::S_PERCENT_X;
            attribute[1] = PT::S_PERCENT_Y;
            value[0] = dist;
            value[1] = perpendicular;
        }
    }

    void MotionKeyPosition::positionScreenAttributes(MotionWidget& view, const FloatRect&, const FloatRect&, float x, float y, std::string* attribute,
                                                     float* value) {
        MotionWidget* viewGroup = view.getParent();
        const int width = viewGroup->getWidth();
        const int height = viewGroup->getHeight();
        if (!attribute[0].empty()) {
            if (attribute[0] == PT::S_PERCENT_X) {
                value[0] = x / width;
                value[1] = y / height;
            } else {
                value[1] = x / width;
                value[0] = y / height;
            }
        } else {
            attribute[0] = PT::S_PERCENT_X;
            value[0] = x / width;
            attribute[1] = PT::S_PERCENT_Y;
            value[1] = y / height;
        }
    }

    void MotionKeyPosition::positionCartAttributes(const FloatRect& start, const FloatRect& end, float x, float y, std::string* attribute, float* value) {
        const float startCenterX = start.centerX();
        const float startCenterY = start.centerY();
        const float pathVectorX = end.centerX() - startCenterX;
        const float pathVectorY = end.centerY() - startCenterY;
        if (!attribute[0].empty()) {
            if (attribute[0] == PT::S_PERCENT_X) {
                value[0] = (x - startCenterX) / pathVectorX;
                value[1] = (y - startCenterY) / pathVectorY;
            } else {
                value[1] = (x - startCenterX) / pathVectorX;
                value[0] = (y - startCenterY) / pathVectorY;
            }
        } else {
            attribute[0] = PT::S_PERCENT_X;
            value[0] = (x - startCenterX) / pathVectorX;
            attribute[1] = PT::S_PERCENT_Y;
            value[1] = (y - startCenterY) / pathVectorY;
        }
    }

    bool MotionKeyPosition::intersects(int layoutWidth, int layoutHeight, const FloatRect& start, const FloatRect& end, float x, float y) {
        calcPosition(layoutWidth, layoutHeight, start.centerX(), start.centerY(), end.centerX(), end.centerY());
        return std::abs(x - mCalculatedPositionX) < SELECTION_SLOPE && std::abs(y - mCalculatedPositionY) < SELECTION_SLOPE;
    }

    MotionKey& MotionKeyPosition::copy(const MotionKey& src) {
        MotionKey::copy(src);
        const auto& k = static_cast<const MotionKeyPosition&>(src);
        mTransitionEasing = k.mTransitionEasing;
        mPathMotionArc = k.mPathMotionArc;
        mDrawPath = k.mDrawPath;
        mPercentWidth = k.mPercentWidth;
        mPercentHeight = NAN;   // as upstream
        mPercentX = k.mPercentX;
        mPercentY = k.mPercentY;
        mAltPercentX = k.mAltPercentX;
        mAltPercentY = k.mAltPercentY;
        mCalculatedPositionX = k.mCalculatedPositionX;
        mCalculatedPositionY = k.mCalculatedPositionY;
        return *this;
    }

    void MotionKeyPosition::calcPosition(int layoutWidth, int layoutHeight, float startX, float startY, float endX, float endY) {
        switch (mPositionType) {
            case TYPE_SCREEN: calcScreenPosition(layoutWidth, layoutHeight); return;
            case TYPE_PATH: calcPathPosition(startX, startY, endX, endY); return;
            case TYPE_CARTESIAN:
            default: calcCartesianPosition(startX, startY, endX, endY); return;
        }
    }

    bool MotionKeyPosition::setValue(int type, int value) {
        switch (type) {
            case PT::TYPE_POSITION_TYPE: mPositionType = value; break;
            case TYPE_FRAME_POSITION: mFramePosition = value; break;
            case PT::TYPE_CURVE_FIT: mCurveFit = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    bool MotionKeyPosition::setValue(int type, float value) {
        switch (type) {
            case PT::TYPE_PERCENT_WIDTH: mPercentWidth = value; break;
            case PT::TYPE_PERCENT_HEIGHT: mPercentHeight = value; break;
            case PT::TYPE_SIZE_PERCENT: mPercentHeight = mPercentWidth = value; break;
            case PT::TYPE_PERCENT_X: mPercentX = value; break;
            case PT::TYPE_PERCENT_Y: mPercentY = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    bool MotionKeyPosition::setValue(int type, const std::string& value) {
        if (type == PT::TYPE_TRANSITION_EASING) {
            mTransitionEasing = value;
            return true;
        }
        return MotionKey::setValue(type, value);
    }

}
