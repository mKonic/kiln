#pragma once

// MotionConstrainedPoint (constraintlayout-core motion): a widget's attributes (alpha,
// rotation, scale, translation, custom) at the start or end of a motion, to find which
// attributes change and seed their splines.

#include "kiln/constraintlayout/core/motion/CustomVariable.h"
#include "kiln/constraintlayout/core/motion/utils/Easing.h"
#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"
#include "kiln/constraintlayout/core/state/JavaHashMap.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;
    class SplineSet;

    class MotionConstrainedPoint {
    public:
        static constexpr int PERPENDICULAR = 1;
        static constexpr int CARTESIAN = 2;

        void different(const MotionConstrainedPoint& points, JavaHashSet& keySet) const;
        void different(const MotionConstrainedPoint& points, bool* mask, const std::vector<std::string>& custom) const;
        void fillStandard(double* data, const std::vector<int>& toUse) const;
        bool hasCustomData(const std::string& name) const { return findCustom(name) != nullptr; }
        int getCustomDataCount(const std::string& name) const { return findCustom(name)->numberOfInterpolatedValues(); }
        int getCustomData(const std::string& name, double* value, int offset) const;
        void setBounds(float x, float y, float w, float h) {
            mX = x;
            mY = y;
            mWidth = w;
            mHeight = h;
        }
        int compareTo(const MotionConstrainedPoint& o) const { return mPosition < o.mPosition ? -1 : mPosition > o.mPosition ? 1 : 0; }
        void applyParameters(MotionWidget& view);
        void addValues(JavaHashMap<std::shared_ptr<SplineSet>>& splines, int framePosition);
        void setState(MotionWidget& view);
        void setState(const Rect& rect, MotionWidget& view, int rotation, float prevous);

        int mVisibilityMode = 0;
        int mVisibility = 0;
        float rotationY = 0;
        int mMode = 0;
        // LinkedHashMap: insertion order.
        std::vector<std::pair<std::string, CustomVariable>> mCustomVariable;

    private:
        const CustomVariable* findCustom(const std::string& name) const {
            for (const auto& [k, v] : mCustomVariable)
                if (k == name) return &v;
            return nullptr;
        }

        float mAlpha = 1;
        float mElevation = 0;
        float mRotation = 0;
        float mRotationX = 0;
        float mScaleX = 1;
        float mScaleY = 1;
        float mPivotX = NAN;
        float mPivotY = NAN;
        float mTranslationX = 0;
        float mTranslationY = 0;
        float mTranslationZ = 0;
        float mPosition = 0;
        float mX = 0;
        float mY = 0;
        float mWidth = 0;
        float mHeight = 0;
        float mPathRotate = NAN;
        float mProgress = NAN;
    };

}
