#include "kiln/constraintlayout/core/motion/MotionConstrainedPoint.h"

#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/utils/SplineSet.h"

#include <cstdio>

namespace kiln::constraintlayout::core::motion {

    using AT = TypedValues::AttributesType;

    namespace {
        bool diff(float a, float b) {
            if (std::isnan(a) || std::isnan(b)) return std::isnan(a) != std::isnan(b);
            return std::abs(a - b) > 0.000001f;
        }
    }

    void MotionConstrainedPoint::different(const MotionConstrainedPoint& points, JavaHashSet& keySet) const {
        if (diff(mAlpha, points.mAlpha)) keySet.add(AT::S_ALPHA);
        if (diff(mElevation, points.mElevation)) keySet.add(AT::S_TRANSLATION_Z);
        if (mVisibility != points.mVisibility && mVisibilityMode == MotionWidget::VISIBILITY_MODE_NORMAL &&
            (mVisibility == MotionWidget::VISIBLE || points.mVisibility == MotionWidget::VISIBLE))
            keySet.add(AT::S_ALPHA);
        if (diff(mRotation, points.mRotation)) keySet.add(AT::S_ROTATION_Z);
        if (!(std::isnan(mPathRotate) && std::isnan(points.mPathRotate))) keySet.add(AT::S_PATH_ROTATE);
        if (!(std::isnan(mProgress) && std::isnan(points.mProgress))) keySet.add(AT::S_PROGRESS);
        if (diff(mRotationX, points.mRotationX)) keySet.add(AT::S_ROTATION_X);
        if (diff(rotationY, points.rotationY)) keySet.add(AT::S_ROTATION_Y);
        if (diff(mPivotX, points.mPivotX)) keySet.add(AT::S_PIVOT_X);
        if (diff(mPivotY, points.mPivotY)) keySet.add(AT::S_PIVOT_Y);
        if (diff(mScaleX, points.mScaleX)) keySet.add(AT::S_SCALE_X);
        if (diff(mScaleY, points.mScaleY)) keySet.add(AT::S_SCALE_Y);
        if (diff(mTranslationX, points.mTranslationX)) keySet.add(AT::S_TRANSLATION_X);
        if (diff(mTranslationY, points.mTranslationY)) keySet.add(AT::S_TRANSLATION_Y);
        if (diff(mTranslationZ, points.mTranslationZ)) keySet.add(AT::S_TRANSLATION_Z);
        if (diff(mElevation, points.mElevation)) keySet.add(AT::S_ELEVATION);
    }

    void MotionConstrainedPoint::different(const MotionConstrainedPoint& points, bool* mask, const std::vector<std::string>&) const {
        int c = 0;
        mask[c++] |= diff(mPosition, points.mPosition);
        mask[c++] |= diff(mX, points.mX);
        mask[c++] |= diff(mY, points.mY);
        mask[c++] |= diff(mWidth, points.mWidth);
        mask[c++] |= diff(mHeight, points.mHeight);
    }

    void MotionConstrainedPoint::fillStandard(double* data, const std::vector<int>& toUse) const {
        const float set[] = { mPosition, mX,      mY,      mWidth,       mHeight,       mAlpha,        mElevation, mRotation, mRotationX,
                              rotationY, mScaleX, mScaleY, mPivotX,      mPivotY,       mTranslationX, mTranslationY, mTranslationZ, mPathRotate };
        int c = 0;
        for (const int u : toUse)
            if (u < 18) data[c++] = set[u];
    }

    int MotionConstrainedPoint::getCustomData(const std::string& name, double* value, int offset) const {
        const CustomVariable* a = findCustom(name);
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

    void MotionConstrainedPoint::applyParameters(MotionWidget& view) {
        mVisibility = view.getVisibility();
        mAlpha = view.getVisibility() != MotionWidget::VISIBLE ? 0.0f : view.getAlpha();
        mRotation = view.getRotationZ();
        mRotationX = view.getRotationX();
        rotationY = view.getRotationY();
        mScaleX = view.getScaleX();
        mScaleY = view.getScaleY();
        mPivotX = view.getPivotX();
        mPivotY = view.getPivotY();
        mTranslationX = view.getTranslationX();
        mTranslationY = view.getTranslationY();
        mTranslationZ = view.getTranslationZ();
        for (const std::string& s : view.getCustomAttributeNames()) {
            CustomVariable* attr = view.getCustomAttribute(s);
            if (attr == nullptr || !attr->isContinuous()) continue;
            bool replaced = false;
            for (auto& [k, v] : mCustomVariable)
                if (k == s) {
                    v = *attr;
                    replaced = true;
                }
            if (!replaced) mCustomVariable.emplace_back(s, *attr);
        }
    }

    void MotionConstrainedPoint::addValues(JavaHashMap<std::shared_ptr<SplineSet>>& splines, int framePosition) {
        for (const std::string& s : splines.keys()) {
            SplineSet& spline = **splines.find(s);
            auto point = [&](float v, float fallback) { spline.setPoint(framePosition, std::isnan(v) ? fallback : v); };
            if (s == AT::S_ALPHA) point(mAlpha, 1);
            else if (s == AT::S_ROTATION_Z) point(mRotation, 0);
            else if (s == AT::S_ROTATION_X) point(mRotationX, 0);
            else if (s == AT::S_ROTATION_Y) point(rotationY, 0);
            else if (s == AT::S_PIVOT_X) point(mPivotX, 0);
            else if (s == AT::S_PIVOT_Y) point(mPivotY, 0);
            else if (s == AT::S_PATH_ROTATE) point(mPathRotate, 0);
            else if (s == AT::S_PROGRESS) point(mProgress, 0);
            else if (s == AT::S_SCALE_X) point(mScaleX, 1);
            else if (s == AT::S_SCALE_Y) point(mScaleY, 1);
            else if (s == AT::S_TRANSLATION_X) point(mTranslationX, 0);
            else if (s == AT::S_TRANSLATION_Y) point(mTranslationY, 0);
            else if (s == AT::S_TRANSLATION_Z) point(mTranslationZ, 0);
            else if (s.rfind("CUSTOM", 0) == 0) {
                const std::size_t comma = s.find(',');
                const std::size_t next = s.find(',', comma + 1);
                const std::string customName = s.substr(comma + 1, next == std::string::npos ? std::string::npos : next - comma - 1);
                if (const CustomVariable* custom = findCustom(customName)) {
                    if (CustomSpline* cs = spline.asCustomSpline()) cs->setPoint(framePosition, *custom);
                    else std::fprintf(stderr, "MotionPaths: %s ViewSpline not a CustomSet frame = %d\n", s.c_str(), framePosition);
                }
            } else {
                std::fprintf(stderr, "MotionPaths: UNKNOWN spline %s\n", s.c_str());
            }
        }
    }

    void MotionConstrainedPoint::setState(MotionWidget& view) {
        setBounds(float(view.getX()), float(view.getY()), float(view.getWidth()), float(view.getHeight()));
        applyParameters(view);
    }

    void MotionConstrainedPoint::setState(const Rect& rect, MotionWidget& view, int rotation, float prevous) {
        setBounds(float(rect.left), float(rect.top), float(rect.width()), float(rect.height()));
        applyParameters(view);
        mPivotX = NAN;
        mPivotY = NAN;
        switch (rotation) {
            case MotionWidget::ROTATE_PORTRATE_OF_LEFT: mRotation = prevous + 90; break;
            case MotionWidget::ROTATE_PORTRATE_OF_RIGHT: mRotation = prevous - 90; break;
        }
    }

}
