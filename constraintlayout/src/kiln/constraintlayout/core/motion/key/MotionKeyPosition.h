#pragma once

// MotionKeyPosition (constraintlayout-core motion key): a point a widget's path passes
// through at a frame position, relative to the path (TYPE_PATH), the start and end
// (TYPE_CARTESIAN) or the parent (TYPE_SCREEN), optionally resizing it.

#include "kiln/constraintlayout/core/motion/key/MotionKey.h"
#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"

#include <cmath>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;

    class MotionKeyPosition : public MotionKey {
    public:
        static constexpr const char* NAME = "KeyPosition";
        static constexpr int TYPE_SCREEN = 2;
        static constexpr int TYPE_PATH = 1;
        static constexpr int TYPE_CARTESIAN = 0;
        static constexpr int KEY_TYPE = 2;

        MotionKeyPosition() { mType = KEY_TYPE; }
        // attribute is two names, empty for Java's null.
        void positionAttributes(MotionWidget& view, const FloatRect& start, const FloatRect& end, float x, float y, std::string* attribute, float* value);
        bool intersects(int layoutWidth, int layoutHeight, const FloatRect& start, const FloatRect& end, float x, float y);
        MotionKey& copy(const MotionKey& src) override;
        std::shared_ptr<MotionKey> clone() const override {
            auto k = std::make_shared<MotionKeyPosition>();
            k->copy(*this);
            return k;
        }
        void getAttributeNames(JavaHashSet&) override {}
        void addValues(SplineMap&) override {}
        bool setValue(int type, int value) override;
        bool setValue(int type, float value) override;
        bool setValue(int type, const std::string& value) override;
        bool setValue(int type, bool value) override { return MotionKey::setValue(type, value); }
        int getId(const std::string& name) override { return PositionType::getId(name); }

        int mCurveFit = UNSET;
        std::string mTransitionEasing;   // empty: Java's null
        int mPathMotionArc = UNSET;
        int mDrawPath = 0;
        float mPercentWidth = NAN;
        float mPercentHeight = NAN;
        float mPercentX = NAN;
        float mPercentY = NAN;
        float mAltPercentX = NAN;
        float mAltPercentY = NAN;
        int mPositionType = TYPE_CARTESIAN;

        float getPositionX() const { return mCalculatedPositionX; }
        float getPositionY() const { return mCalculatedPositionY; }
        void calcPosition(int layoutWidth, int layoutHeight, float startX, float startY, float endX, float endY);

    private:
        static constexpr float SELECTION_SLOPE = 20;
        void calcScreenPosition(int layoutWidth, int layoutHeight);
        void calcPathPosition(float startX, float startY, float endX, float endY);
        void calcCartesianPosition(float startX, float startY, float endX, float endY);
        void positionPathAttributes(const FloatRect& start, const FloatRect& end, float x, float y, std::string* attribute, float* value);
        void positionScreenAttributes(MotionWidget& view, const FloatRect& start, const FloatRect& end, float x, float y, std::string* attribute,
                                      float* value);
        void positionCartAttributes(const FloatRect& start, const FloatRect& end, float x, float y, std::string* attribute, float* value);

        float mCalculatedPositionX = NAN;
        float mCalculatedPositionY = NAN;
    };

}
