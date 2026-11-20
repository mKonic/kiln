#include "kiln/constraintlayout/core/motion/MotionPaths.h"

#include "kiln/constraintlayout/core/LinearSystem.h"
#include "kiln/constraintlayout/core/motion/Motion.h"
#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <algorithm>

namespace kiln::constraintlayout::core::motion {

    namespace {
        constexpr double kPI = 3.141592653589793;
        constexpr double kRadiansToDegrees = 180.0 / kPI;
        constexpr double kDegreesToRadians = kPI / 180.0;
        float I(float v) { return float(JavaInt(v)); }   // (int) cast kept as float

        float xRotate(float sin, float cos, float cx, float cy, float x, float y) {
            x = x - cx;
            y = y - cy;
            return x * cos - y * sin + cx;
        }
        float yRotate(float sin, float cos, float cx, float cy, float x, float y) {
            x = x - cx;
            y = y - cy;
            return x * sin + y * cos + cy;
        }
        bool diff(float a, float b) {
            if (std::isnan(a) || std::isnan(b)) return std::isnan(a) != std::isnan(b);
            return std::abs(a - b) > 0.000001f;
        }
    }

    void MotionPaths::initCartesian(const MotionKeyPosition& c, const MotionPaths& startTimePoint, const MotionPaths& endTimePoint) {
        const float position = c.mFramePosition / 100.0f;
        MotionPaths& point = *this;
        point.mTime = position;
        mDrawPath = c.mDrawPath;
        const float scaleWidth = std::isnan(c.mPercentWidth) ? position : c.mPercentWidth;
        const float scaleHeight = std::isnan(c.mPercentHeight) ? position : c.mPercentHeight;
        const float scaleX = endTimePoint.mWidth - startTimePoint.mWidth;
        const float scaleY = endTimePoint.mHeight - startTimePoint.mHeight;
        point.mPosition = point.mTime;
        const float path = position;
        const float startCenterX = startTimePoint.mX + startTimePoint.mWidth / 2;
        const float startCenterY = startTimePoint.mY + startTimePoint.mHeight / 2;
        const float endCenterX = endTimePoint.mX + endTimePoint.mWidth / 2;
        const float endCenterY = endTimePoint.mY + endTimePoint.mHeight / 2;
        const float pathVectorX = endCenterX - startCenterX;
        const float pathVectorY = endCenterY - startCenterY;
        point.mX = I(startTimePoint.mX + pathVectorX * path - scaleX * scaleWidth / 2);
        point.mY = I(startTimePoint.mY + pathVectorY * path - scaleY * scaleHeight / 2);
        point.mWidth = I(startTimePoint.mWidth + scaleX * scaleWidth);
        point.mHeight = I(startTimePoint.mHeight + scaleY * scaleHeight);
        const float dxdx = std::isnan(c.mPercentX) ? position : c.mPercentX;
        const float dydx = std::isnan(c.mAltPercentY) ? 0 : c.mAltPercentY;
        const float dydy = std::isnan(c.mPercentY) ? position : c.mPercentY;
        const float dxdy = std::isnan(c.mAltPercentX) ? 0 : c.mAltPercentX;
        point.mMode = CARTESIAN;
        point.mX = I(startTimePoint.mX + pathVectorX * dxdx + pathVectorY * dxdy - scaleX * scaleWidth / 2);
        point.mY = I(startTimePoint.mY + pathVectorX * dydx + pathVectorY * dydy - scaleY * scaleHeight / 2);
        point.mKeyFrameEasing = Easing::getInterpolator(c.mTransitionEasing);
        point.mPathMotionArc = c.mPathMotionArc;
    }

    MotionPaths::MotionPaths(int parentWidth, int parentHeight, const MotionKeyPosition& c, const MotionPaths& startTimePoint,
                             const MotionPaths& endTimePoint) {
        if (!startTimePoint.mAnimateRelativeTo.empty()) {
            initPolar(parentWidth, parentHeight, c, startTimePoint, endTimePoint);
            return;
        }
        switch (c.mPositionType) {
            case MotionKeyPosition::TYPE_SCREEN: initScreen(parentWidth, parentHeight, c, startTimePoint, endTimePoint); return;
            case MotionKeyPosition::TYPE_PATH: initPath(c, startTimePoint, endTimePoint); return;
            default: initCartesian(c, startTimePoint, endTimePoint); return;
        }
    }

    void MotionPaths::initPolar(int, int, const MotionKeyPosition& c, const MotionPaths& s, const MotionPaths& e) {
        const float position = c.mFramePosition / 100.0f;
        mTime = position;
        mDrawPath = c.mDrawPath;
        mMode = c.mPositionType;
        const float scaleWidth = std::isnan(c.mPercentWidth) ? position : c.mPercentWidth;
        const float scaleHeight = std::isnan(c.mPercentHeight) ? position : c.mPercentHeight;
        const float scaleX = e.mWidth - s.mWidth;
        const float scaleY = e.mHeight - s.mHeight;
        mPosition = mTime;
        mWidth = I(s.mWidth + scaleX * scaleWidth);
        mHeight = I(s.mHeight + scaleY * scaleHeight);
        switch (c.mPositionType) {
            case MotionKeyPosition::TYPE_SCREEN:
                mX = std::isnan(c.mPercentX) ? (position * (e.mX - s.mX) + s.mX) : c.mPercentX * std::min(scaleHeight, scaleWidth);
                mY = std::isnan(c.mPercentY) ? (position * (e.mY - s.mY) + s.mY) : c.mPercentY;
                break;
            case MotionKeyPosition::TYPE_PATH:
            default:
                mX = (std::isnan(c.mPercentX) ? position : c.mPercentX) * (e.mX - s.mX) + s.mX;
                mY = (std::isnan(c.mPercentY) ? position : c.mPercentY) * (e.mY - s.mY) + s.mY;
                break;
        }
        mAnimateRelativeTo = s.mAnimateRelativeTo;
        mKeyFrameEasing = Easing::getInterpolator(c.mTransitionEasing);
        mPathMotionArc = c.mPathMotionArc;
    }

    void MotionPaths::setupRelative(Motion* mc, const MotionPaths& relative) {
        const double dx = mX + mWidth / 2 - relative.mX - relative.mWidth / 2;
        const double dy = mY + mHeight / 2 - relative.mY - relative.mHeight / 2;
        mRelativeToController = mc;
        mX = float(std::hypot(dy, dx));
        if (std::isnan(mRelativeAngle)) mY = float(std::atan2(dy, dx) + kPI / 2);
        else mY = float(mRelativeAngle * kDegreesToRadians);
    }

    void MotionPaths::initScreen(int parentWidth, int parentHeight, const MotionKeyPosition& c, const MotionPaths& startTimePoint,
                                 const MotionPaths& endTimePoint) {
        const float position = c.mFramePosition / 100.0f;
        MotionPaths& point = *this;
        point.mTime = position;
        mDrawPath = c.mDrawPath;
        const float scaleWidth = std::isnan(c.mPercentWidth) ? position : c.mPercentWidth;
        const float scaleHeight = std::isnan(c.mPercentHeight) ? position : c.mPercentHeight;
        const float scaleX = endTimePoint.mWidth - startTimePoint.mWidth;
        const float scaleY = endTimePoint.mHeight - startTimePoint.mHeight;
        point.mPosition = point.mTime;
        const float path = position;
        const float startCenterX = startTimePoint.mX + startTimePoint.mWidth / 2;
        const float startCenterY = startTimePoint.mY + startTimePoint.mHeight / 2;
        const float endCenterX = endTimePoint.mX + endTimePoint.mWidth / 2;
        const float endCenterY = endTimePoint.mY + endTimePoint.mHeight / 2;
        const float pathVectorX = endCenterX - startCenterX;
        const float pathVectorY = endCenterY - startCenterY;
        point.mX = I(startTimePoint.mX + pathVectorX * path - scaleX * scaleWidth / 2);
        point.mY = I(startTimePoint.mY + pathVectorY * path - scaleY * scaleHeight / 2);
        point.mWidth = I(startTimePoint.mWidth + scaleX * scaleWidth);
        point.mHeight = I(startTimePoint.mHeight + scaleY * scaleHeight);
        point.mMode = SCREEN;
        if (!std::isnan(c.mPercentX)) {
            parentWidth -= JavaInt(point.mWidth);
            point.mX = I(c.mPercentX * parentWidth);
        }
        if (!std::isnan(c.mPercentY)) {
            parentHeight -= JavaInt(point.mHeight);
            point.mY = I(c.mPercentY * parentHeight);
        }
        point.mAnimateRelativeTo = mAnimateRelativeTo;
        point.mKeyFrameEasing = Easing::getInterpolator(c.mTransitionEasing);
        point.mPathMotionArc = c.mPathMotionArc;
    }

    void MotionPaths::initPath(const MotionKeyPosition& c, const MotionPaths& startTimePoint, const MotionPaths& endTimePoint) {
        const float position = c.mFramePosition / 100.0f;
        MotionPaths& point = *this;
        point.mTime = position;
        mDrawPath = c.mDrawPath;
        const float scaleWidth = std::isnan(c.mPercentWidth) ? position : c.mPercentWidth;
        const float scaleHeight = std::isnan(c.mPercentHeight) ? position : c.mPercentHeight;
        const float scaleX = endTimePoint.mWidth - startTimePoint.mWidth;
        const float scaleY = endTimePoint.mHeight - startTimePoint.mHeight;
        point.mPosition = point.mTime;
        const float path = std::isnan(c.mPercentX) ? position : c.mPercentX;
        const float startCenterX = startTimePoint.mX + startTimePoint.mWidth / 2;
        const float startCenterY = startTimePoint.mY + startTimePoint.mHeight / 2;
        const float endCenterX = endTimePoint.mX + endTimePoint.mWidth / 2;
        const float endCenterY = endTimePoint.mY + endTimePoint.mHeight / 2;
        const float pathVectorX = endCenterX - startCenterX;
        const float pathVectorY = endCenterY - startCenterY;
        point.mX = I(startTimePoint.mX + pathVectorX * path - scaleX * scaleWidth / 2);
        point.mY = I(startTimePoint.mY + pathVectorY * path - scaleY * scaleHeight / 2);
        point.mWidth = I(startTimePoint.mWidth + scaleX * scaleWidth);
        point.mHeight = I(startTimePoint.mHeight + scaleY * scaleHeight);
        const float perpendicular = std::isnan(c.mPercentY) ? 0 : c.mPercentY;
        const float perpendicularX = -pathVectorY;
        const float perpendicularY = pathVectorX;
        const float normalX = perpendicularX * perpendicular;
        const float normalY = perpendicularY * perpendicular;
        point.mMode = PERPENDICULAR;
        point.mX = I(startTimePoint.mX + pathVectorX * path - scaleX * scaleWidth / 2);
        point.mY = I(startTimePoint.mY + pathVectorY * path - scaleY * scaleHeight / 2);
        point.mX += normalX;
        point.mY += normalY;
        point.mAnimateRelativeTo = mAnimateRelativeTo;
        point.mKeyFrameEasing = Easing::getInterpolator(c.mTransitionEasing);
        point.mPathMotionArc = c.mPathMotionArc;
    }

    void MotionPaths::different(const MotionPaths& points, bool* mask, const std::vector<std::string>&, bool arcMode) const {
        int c = 0;
        const bool diffx = diff(mX, points.mX);
        const bool diffy = diff(mY, points.mY);
        mask[c++] |= diff(mPosition, points.mPosition);
        mask[c++] |= diffx || diffy || arcMode;
        mask[c++] |= diffx || diffy || arcMode;
        mask[c++] |= diff(mWidth, points.mWidth);
        mask[c++] |= diff(mHeight, points.mHeight);
    }

    void MotionPaths::getCenter(double p, const std::vector<int>& toUse, const double* data, float* point, int offset) const {
        float v_x = mX, v_y = mY, v_width = mWidth, v_height = mHeight;
        for (std::size_t i = 0; i < toUse.size(); i++) {
            const float value = float(data[i]);
            switch (toUse[i]) {
                case OFF_X: v_x = value; break;
                case OFF_Y: v_y = value; break;
                case OFF_WIDTH: v_width = value; break;
                case OFF_HEIGHT: v_height = value; break;
            }
        }
        if (mRelativeToController != nullptr) {
            float pos[2], vel[2];
            mRelativeToController->getCenter(p, pos, vel);
            const float radius = v_x;
            const float angle = v_y;
            v_x = float(pos[0] + radius * std::sin(angle) - v_width / 2);
            v_y = float(pos[1] - radius * std::cos(angle) - v_height / 2);
        }
        point[offset] = v_x + v_width / 2;
        point[offset + 1] = v_y + v_height / 2;
    }

    void MotionPaths::getCenter(double p, const std::vector<int>& toUse, const double* data, float* point, const double* vdata, float* velocity) const {
        float v_x = mX, v_y = mY, v_width = mWidth, v_height = mHeight;
        float dv_x = 0, dv_y = 0, dv_width = 0, dv_height = 0;
        for (std::size_t i = 0; i < toUse.size(); i++) {
            const float value = float(data[i]);
            const float dvalue = float(vdata[i]);
            switch (toUse[i]) {
                case OFF_X: v_x = value; dv_x = dvalue; break;
                case OFF_Y: v_y = value; dv_y = dvalue; break;
                case OFF_WIDTH: v_width = value; dv_width = dvalue; break;
                case OFF_HEIGHT: v_height = value; dv_height = dvalue; break;
            }
        }
        float dpos_x = dv_x + dv_width / 2;
        float dpos_y = dv_y + dv_height / 2;
        if (mRelativeToController != nullptr) {
            float pos[2], vel[2];
            mRelativeToController->getCenter(p, pos, vel);
            const float radius = v_x, angle = v_y, dradius = dv_x, dangle = dv_y;
            v_x = float(pos[0] + radius * std::sin(angle) - v_width / 2);
            v_y = float(pos[1] - radius * std::cos(angle) - v_height / 2);
            dpos_x = float(vel[0] + dradius * std::sin(angle) + std::cos(angle) * dangle);
            dpos_y = float(vel[1] - dradius * std::cos(angle) + std::sin(angle) * dangle);
        }
        point[0] = v_x + v_width / 2;
        point[1] = v_y + v_height / 2;
        velocity[0] = dpos_x;
        velocity[1] = dpos_y;
    }

    void MotionPaths::getCenterVelocity(double p, const std::vector<int>& toUse, const double* data, float* point, int offset) const {
        getCenter(p, toUse, data, point, offset);
    }

    void MotionPaths::getBounds(const std::vector<int>& toUse, const double* data, float* point, int offset) const {
        float v_width = mWidth, v_height = mHeight;
        for (std::size_t i = 0; i < toUse.size(); i++) {
            const float value = float(data[i]);
            switch (toUse[i]) {
                case OFF_WIDTH: v_width = value; break;
                case OFF_HEIGHT: v_height = value; break;
            }
        }
        point[offset] = v_width;
        point[offset + 1] = v_height;
    }

    void MotionPaths::setView(float position, MotionWidget& view, const std::vector<int>& toUse, const double* data, std::vector<double>& slope,
                              const double* cycle) {
        float v_x = mX, v_y = mY, v_width = mWidth, v_height = mHeight;
        float dv_x = 0, dv_y = 0, dv_width = 0, dv_height = 0;
        float path_rotate = NAN;
        if (!toUse.empty() && int(mTempValue.size()) <= toUse.back()) {
            const std::size_t scratch_data_length = std::size_t(toUse.back()) + 1;
            mTempValue.assign(scratch_data_length, 0);
            mTempDelta.assign(scratch_data_length, 0);
        }
        std::fill(mTempValue.begin(), mTempValue.end(), NAN);
        for (std::size_t i = 0; i < toUse.size(); i++) {
            mTempValue[std::size_t(toUse[i])] = data[i];
            mTempDelta[std::size_t(toUse[i])] = slope[i];
        }
        for (std::size_t i = 0; i < mTempValue.size(); i++) {
            if (std::isnan(mTempValue[i]) && (cycle == nullptr || cycle[i] == 0.0)) continue;
            const double deltaCycle = cycle != nullptr ? cycle[i] : 0.0;
            const float value = float(std::isnan(mTempValue[i]) ? deltaCycle : mTempValue[i] + deltaCycle);
            const float dvalue = float(mTempDelta[i]);
            switch (int(i)) {
                case OFF_POSITION: break;
                case OFF_X: v_x = value; dv_x = dvalue; break;
                case OFF_Y: v_y = value; dv_y = dvalue; break;
                case OFF_WIDTH: v_width = value; dv_width = dvalue; break;
                case OFF_HEIGHT: v_height = value; dv_height = dvalue; break;
                case OFF_PATH_ROTATE: path_rotate = value; break;
            }
        }
        if (mRelativeToController != nullptr) {
            float pos[2], vel[2];
            mRelativeToController->getCenter(position, pos, vel);
            const float radius = v_x, angle = v_y, dradius = dv_x, dangle = dv_y;
            const float pos_x = float(pos[0] + radius * std::sin(angle) - v_width / 2);
            const float pos_y = float(pos[1] - radius * std::cos(angle) - v_height / 2);
            const float dpos_x = float(vel[0] + dradius * std::sin(angle) + radius * std::cos(angle) * dangle);
            const float dpos_y = float(vel[1] - dradius * std::cos(angle) + radius * std::sin(angle) * dangle);
            dv_x = dpos_x;
            dv_y = dpos_y;
            v_x = pos_x;
            v_y = pos_y;
            if (slope.size() >= 2) {
                slope[0] = dpos_x;
                slope[1] = dpos_y;
            }
            if (!std::isnan(path_rotate)) view.setRotationZ(float(path_rotate + std::atan2(dv_y, dv_x) * kRadiansToDegrees));
        } else if (!std::isnan(path_rotate)) {
            float rot = 0;
            const float dx = dv_x + dv_width / 2;
            const float dy = dv_y + dv_height / 2;
            rot += float(path_rotate + std::atan2(dy, dx) * kRadiansToDegrees);
            view.setRotationZ(rot);
        }
        const int l = JavaInt(0.5f + v_x);
        const int t = JavaInt(0.5f + v_y);
        const int r = JavaInt(0.5f + v_x + v_width);
        const int b = JavaInt(0.5f + v_y + v_height);
        view.layout(l, t, r, b);
    }

    void MotionPaths::getRect(const std::vector<int>& toUse, const double* data, float* path, int offset) const {
        float v_x = mX, v_y = mY, v_width = mWidth, v_height = mHeight;
        const float rotation = 0;
        const float scaleX = 1, scaleY = 1;
        const float pivotX = NAN, pivotY = NAN;
        const float translationX = 0, translationY = 0;
        for (std::size_t i = 0; i < toUse.size(); i++) {
            const float value = float(data[i]);
            switch (toUse[i]) {
                case OFF_X: v_x = value; break;
                case OFF_Y: v_y = value; break;
                case OFF_WIDTH: v_width = value; break;
                case OFF_HEIGHT: v_height = value; break;
            }
        }
        if (mRelativeToController != nullptr) {
            const float rx = mRelativeToController->getCenterX();
            const float ry = mRelativeToController->getCenterY();
            const float radius = v_x, angle = v_y;
            v_x = float(rx + radius * std::sin(angle) - v_width / 2);
            v_y = float(ry - radius * std::cos(angle) - v_height / 2);
        }
        float x1 = v_x, y1 = v_y;
        float x2 = v_x + v_width, y2 = y1;
        float x3 = x2, y3 = v_y + v_height;
        float x4 = x1, y4 = y3;
        float cx = x1 + v_width / 2;
        float cy = y1 + v_height / 2;
        if (!std::isnan(pivotX)) cx = x1 + (x2 - x1) * pivotX;
        if (!std::isnan(pivotY)) cy = y1 + (y3 - y1) * pivotY;
        if (scaleX != 1) {
            const float midx = (x1 + x2) / 2;
            x1 = (x1 - midx) * scaleX + midx;
            x2 = (x2 - midx) * scaleX + midx;
            x3 = (x3 - midx) * scaleX + midx;
            x4 = (x4 - midx) * scaleX + midx;
        }
        if (scaleY != 1) {
            const float midy = (y1 + y3) / 2;
            y1 = (y1 - midy) * scaleY + midy;
            y2 = (y2 - midy) * scaleY + midy;
            y3 = (y3 - midy) * scaleY + midy;
            y4 = (y4 - midy) * scaleY + midy;
        }
        if (rotation != 0) {
            const float sin = float(std::sin(rotation * kDegreesToRadians));
            const float cos = float(std::cos(rotation * kDegreesToRadians));
            const float tx1 = xRotate(sin, cos, cx, cy, x1, y1), ty1 = yRotate(sin, cos, cx, cy, x1, y1);
            const float tx2 = xRotate(sin, cos, cx, cy, x2, y2), ty2 = yRotate(sin, cos, cx, cy, x2, y2);
            const float tx3 = xRotate(sin, cos, cx, cy, x3, y3), ty3 = yRotate(sin, cos, cx, cy, x3, y3);
            const float tx4 = xRotate(sin, cos, cx, cy, x4, y4), ty4 = yRotate(sin, cos, cx, cy, x4, y4);
            x1 = tx1, y1 = ty1, x2 = tx2, y2 = ty2, x3 = tx3, y3 = ty3, x4 = tx4, y4 = ty4;
        }
        path[offset++] = x1 + translationX;
        path[offset++] = y1 + translationY;
        path[offset++] = x2 + translationX;
        path[offset++] = y2 + translationY;
        path[offset++] = x3 + translationX;
        path[offset++] = y3 + translationY;
        path[offset++] = x4 + translationX;
        path[offset++] = y4 + translationY;
    }

    void MotionPaths::setDpDt(float locationX, float locationY, float* anchorDpDt, const std::vector<int>& toUse, const double* deltaData,
                              const double*) const {
        float d_x = 0, d_y = 0, d_width = 0, d_height = 0;
        const float deltaScaleX = 0, deltaScaleY = 0;
        const float deltaTranslationX = 0, deltaTranslationY = 0;
        for (std::size_t i = 0; i < toUse.size(); i++) {
            const float deltaV = float(deltaData[i]);
            switch (toUse[i]) {
                case OFF_X: d_x = deltaV; break;
                case OFF_Y: d_y = deltaV; break;
                case OFF_WIDTH: d_width = deltaV; break;
                case OFF_HEIGHT: d_height = deltaV; break;
            }
        }
        const float deltaX = d_x - deltaScaleX * d_width / 2;
        const float deltaY = d_y - deltaScaleY * d_height / 2;
        const float deltaWidth = d_width * (1 + deltaScaleX);
        const float deltaHeight = d_height * (1 + deltaScaleY);
        const float deltaRight = deltaX + deltaWidth;
        const float deltaBottom = deltaY + deltaHeight;
        anchorDpDt[0] = deltaX * (1 - locationX) + deltaRight * locationX + deltaTranslationX;
        anchorDpDt[1] = deltaY * (1 - locationY) + deltaBottom * locationY + deltaTranslationY;
    }

    void MotionPaths::fillStandard(double* data, const std::vector<int>& toUse) const {
        const float set[] = { mPosition, mX, mY, mWidth, mHeight, mPathRotate };
        int c = 0;
        for (const int u : toUse)
            if (u < 6) data[c++] = set[u];
    }

    int MotionPaths::getCustomDataCount(const std::string& name) const {
        const CustomVariable* a = mCustomAttributes.find(name);
        return a == nullptr ? 0 : a->numberOfInterpolatedValues();
    }

    int MotionPaths::getCustomData(const std::string& name, double* value, int offset) const {
        const CustomVariable* a = mCustomAttributes.find(name);
        if (a == nullptr) return 0;
        if (a->numberOfInterpolatedValues() == 1) {
            value[offset] = a->getValueToInterpolate();
            return 1;
        }
        const int n = a->numberOfInterpolatedValues();
        std::vector<float> f(static_cast<std::size_t>(n));
        a->getValuesToInterpolate(f.data());
        for (int i = 0; i < n; i++) value[offset++] = f[std::size_t(i)];
        return n;
    }

    void MotionPaths::applyParameters(MotionWidget& c) {
        mKeyFrameEasing = Easing::getInterpolator(c.mMotion.mTransitionEasing);
        mPathMotionArc = c.mMotion.mPathMotionArc;
        mAnimateRelativeTo = c.mMotion.mAnimateRelativeTo;
        mPathRotate = c.mMotion.mPathRotate;
        mDrawPath = c.mMotion.mDrawPath;
        mAnimateCircleAngleTo = c.mMotion.mAnimateCircleAngleTo;
        mProgress = c.mPropertySet.mProgress;
        if (c.mWidgetFrame.widget != nullptr) mRelativeAngle = c.mWidgetFrame.widget->mCircleConstraintAngle;
        for (const std::string& s : c.getCustomAttributeNames()) {
            CustomVariable* attr = c.getCustomAttribute(s);
            if (attr != nullptr && attr->isContinuous()) mCustomAttributes.put(s, *attr);
        }
    }

    void MotionPaths::configureRelativeTo(Motion& toOrbit) { (void)toOrbit.getPos(mProgress); }

}
