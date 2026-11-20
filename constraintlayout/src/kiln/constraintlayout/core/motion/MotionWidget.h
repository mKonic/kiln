#pragma once

// MotionWidget (constraintlayout-core motion): what MotionLayout animates: a WidgetFrame plus
// the motion attributes (path arc, easing, stagger, quantize) and visibility of one widget.

#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/state/WidgetFrame.h"

#include <climits>
#include <memory>
#include <cmath>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class CustomAttribute;

    class MotionWidget : public TypedValues {
    public:
        static constexpr int VISIBILITY_MODE_NORMAL = 0;
        static constexpr int VISIBILITY_MODE_IGNORE = 1;
        static constexpr int INVISIBLE = 0;
        static constexpr int VISIBLE = 4;
        static constexpr int ROTATE_NONE = 0;
        static constexpr int ROTATE_PORTRATE_OF_RIGHT = 1;
        static constexpr int ROTATE_PORTRATE_OF_LEFT = 2;
        static constexpr int ROTATE_RIGHT_OF_PORTRATE = 3;
        static constexpr int ROTATE_LEFT_OF_PORTRATE = 4;
        static constexpr int UNSET = -1;
        static constexpr int MATCH_CONSTRAINT = 0;
        static constexpr int PARENT_ID = 0;
        static constexpr int FILL_PARENT = -1;
        static constexpr int MATCH_PARENT = -1;
        static constexpr int WRAP_CONTENT = -2;
        static constexpr int GONE_UNSET = INT_MIN;
        static constexpr int MATCH_CONSTRAINT_WRAP = 1;   // ConstraintWidget::MATCH_CONSTRAINT_WRAP

        struct Motion {
            std::string mAnimateRelativeTo;   // empty: Java's null
            int mAnimateCircleAngleTo = 0;
            std::string mTransitionEasing;    // empty: Java's null
            int mPathMotionArc = UNSET;
            int mDrawPath = 0;
            float mMotionStagger = NAN;
            int mPolarRelativeTo = UNSET;
            float mPathRotate = NAN;
            float mQuantizeMotionPhase = NAN;
            int mQuantizeMotionSteps = UNSET;
            std::string mQuantizeInterpolatorString;
            int mQuantizeInterpolatorType = INTERPOLATOR_UNDEFINED;
            int mQuantizeInterpolatorID = -1;
            static constexpr int INTERPOLATOR_UNDEFINED = -3;
        };

        struct PropertySet {
            int visibility = VISIBLE;
            int mVisibilityMode = VISIBILITY_MODE_NORMAL;
            float alpha = 1;
            float mProgress = NAN;
        };

        // Owns its frame, or shares one (Transition's WidgetState frames), as Java's reference does.
        MotionWidget() : mOwnedFrame(std::make_unique<WidgetFrame>()), mWidgetFrame(*mOwnedFrame) {}
        explicit MotionWidget(WidgetFrame& f) : mWidgetFrame(f) {}
        MotionWidget(const MotionWidget&) = delete;
        MotionWidget& operator=(const MotionWidget&) = delete;

        virtual MotionWidget* getParent() { return nullptr; }
        virtual MotionWidget* findViewById(int) { return nullptr; }
        void setVisibility(int visibility) { mPropertySet.visibility = visibility; }
        std::string getName() const { return mWidgetFrame.getId(); }
        void layout(int l, int t, int r, int b) { setBounds(l, t, r, b); }
        std::string toString() const;
        void setBounds(int left, int top, int right, int bottom) {
            mWidgetFrame.top = top;
            mWidgetFrame.left = left;
            mWidgetFrame.right = right;
            mWidgetFrame.bottom = bottom;
        }
        void updateMotion(TypedValues& toUpdate) {
            if (mWidgetFrame.getMotionProperties() != nullptr) mWidgetFrame.getMotionProperties()->applyDelta(toUpdate);
        }

        bool setValue(int id, int value) override;
        bool setValue(int id, float value) override;
        bool setValue(int id, const std::string& value) override;
        bool setValue(int id, bool value) override { (void)id; (void)value; return false; }
        bool setValueMotion(int id, int value);
        bool setValueMotion(int id, const std::string& value);
        bool setValueMotion(int id, float value);
        bool setValueAttributes(int id, float value);
        float getValueAttributes(int id) const;
        int getId(const std::string& name) override;

        int getTop() const { return mWidgetFrame.top; }
        int getLeft() const { return mWidgetFrame.left; }
        int getBottom() const { return mWidgetFrame.bottom; }
        int getRight() const { return mWidgetFrame.right; }
        void setPivotX(float px) { mWidgetFrame.pivotX = px; }
        void setPivotY(float py) { mWidgetFrame.pivotY = py; }
        float getRotationX() const { return mWidgetFrame.rotationX; }
        void setRotationX(float v) { mWidgetFrame.rotationX = v; }
        float getRotationY() const { return mWidgetFrame.rotationY; }
        void setRotationY(float v) { mWidgetFrame.rotationY = v; }
        float getRotationZ() const { return mWidgetFrame.rotationZ; }
        void setRotationZ(float v) { mWidgetFrame.rotationZ = v; }
        float getTranslationX() const { return mWidgetFrame.translationX; }
        void setTranslationX(float v) { mWidgetFrame.translationX = v; }
        float getTranslationY() const { return mWidgetFrame.translationY; }
        void setTranslationY(float v) { mWidgetFrame.translationY = v; }
        void setTranslationZ(float v) { mWidgetFrame.translationZ = v; }
        float getTranslationZ() const { return mWidgetFrame.translationZ; }
        float getScaleX() const { return mWidgetFrame.scaleX; }
        void setScaleX(float v) { mWidgetFrame.scaleX = v; }
        float getScaleY() const { return mWidgetFrame.scaleY; }
        void setScaleY(float v) { mWidgetFrame.scaleY = v; }
        int getVisibility() const { return mPropertySet.visibility; }
        float getPivotX() const { return mWidgetFrame.pivotX; }
        float getPivotY() const { return mWidgetFrame.pivotY; }
        float getAlpha() const { return mWidgetFrame.alpha; }
        int getX() const { return mWidgetFrame.left; }
        int getY() const { return mWidgetFrame.top; }
        int getWidth() const { return mWidgetFrame.right - mWidgetFrame.left; }
        int getHeight() const { return mWidgetFrame.bottom - mWidgetFrame.top; }
        WidgetFrame& getWidgetFrame() { return mWidgetFrame; }
        std::vector<std::string> getCustomAttributeNames() const { return mWidgetFrame.getCustomAttributeNames(); }
        void setCustomAttribute(const std::string& name, int type, float value) { mWidgetFrame.setCustomAttribute(name, type, value); }
        void setCustomAttribute(const std::string& name, int type, int value) { mWidgetFrame.setCustomAttribute(name, type, value); }
        void setCustomAttribute(const std::string& name, int type, bool value) { mWidgetFrame.setCustomAttribute(name, type, value); }
        void setCustomAttribute(const std::string& name, int type, const std::string& value) { mWidgetFrame.setCustomAttribute(name, type, value); }
        CustomVariable* getCustomAttribute(const std::string& name) { return mWidgetFrame.getCustomAttribute(name); }
        void setInterpolatedValue(const CustomAttribute& attribute, const float* cache);

    private:
        std::unique_ptr<WidgetFrame> mOwnedFrame;

    public:
        WidgetFrame& mWidgetFrame;
        Motion mMotion;
        PropertySet mPropertySet;
        float mTransitionPathRotate = 0;

    private:
        float mProgress = 0;
    };

}
