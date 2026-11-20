#pragma once

// WidgetFrame (constraintlayout-core state): a widget's laid-out bounds plus the graphics
// layer values a ConstraintSet gives it (pivot, rotation, translation, scale, alpha), its
// custom attributes and motion properties; what MotionLayout interpolates between two states.

#include "kiln/constraintlayout/core/motion/CustomVariable.h"
#include "kiln/constraintlayout/core/motion/utils/TypedBundle.h"
#include "kiln/constraintlayout/core/state/JavaHashMap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core {

    class ConstraintWidget;
    class CLElement;
    namespace state {
        class Transition;
    }

    class WidgetFrame {
    public:
        static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        static inline float phone_orientation = kNaN;

        ConstraintWidget* widget = nullptr;
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        float pivotX = kNaN;
        float pivotY = kNaN;
        float rotationX = kNaN;
        float rotationY = kNaN;
        float rotationZ = kNaN;
        float translationX = kNaN;
        float translationY = kNaN;
        float translationZ = kNaN;
        float scaleX = kNaN;
        float scaleY = kNaN;
        float alpha = kNaN;
        float interpolatedPos = kNaN;
        int visibility = 0;   // ConstraintWidget::VISIBLE
        std::string name;     // empty: Java's null

        WidgetFrame() = default;
        explicit WidgetFrame(ConstraintWidget* w) : widget(w) {}
        WidgetFrame(const WidgetFrame& frame) { *this = frame; }
        WidgetFrame& operator=(const WidgetFrame& frame) {
            if (this == &frame) return *this;
            widget = frame.widget;
            left = frame.left;
            top = frame.top;
            right = frame.right;
            bottom = frame.bottom;
            updateAttributes(frame);
            return *this;
        }

        int width() const { return std::max(0, right - left); }
        int height() const { return std::max(0, bottom - top); }
        float centerX() const { return float(left) + float(right - left) / 2.0f; }
        float centerY() const { return float(top) + float(bottom - top) / 2.0f; }

        void updateAttributes(const WidgetFrame& frame);
        bool isDefaultTransform() const {
            return std::isnan(rotationX) && std::isnan(rotationY) && std::isnan(rotationZ) && std::isnan(translationX) &&
                   std::isnan(translationY) && std::isnan(translationZ) && std::isnan(scaleX) && std::isnan(scaleY) && std::isnan(alpha);
        }
        // The frame between start and end at progress, following the transition's key positions.
        static void interpolate(int parentWidth, int parentHeight, WidgetFrame& frame, const WidgetFrame& start, const WidgetFrame& end,
                                state::Transition& transition, float progress);

        // Takes the widget's laid-out bounds and its own frame's attributes.
        WidgetFrame& update();
        WidgetFrame& update(ConstraintWidget* w) {
            if (w == nullptr) return *this;
            widget = w;
            return update();
        }

        bool containsCustom(const std::string& n) const { return mCustom.contains(n); }
        void addCustomColor(const std::string& n, int color);
        int getCustomColor(const std::string& n) const;
        void addCustomFloat(const std::string& n, float value);
        float getCustomFloat(const std::string& n) const;
        void setCustomAttribute(const std::string& n, int type, float value);
        void setCustomAttribute(const std::string& n, int type, int value);
        void setCustomAttribute(const std::string& n, int type, bool value);
        void setCustomAttribute(const std::string& n, int type, const std::string& value);
        // Null when it has none.
        motion::CustomVariable* getCustomAttribute(const std::string& n) { return mCustom.find(n); }
        std::vector<std::string> getCustomAttributeNames() const { return mCustom.keys(); }
        // Reads a value from a JSON frame; false for an unknown key.
        bool setValue(const std::string& key, CLElement* value);
        std::string getId() const;
        std::string serialize(bool sendPhoneOrientation = false) const;
        void setMotionAttributes(std::shared_ptr<motion::TypedBundle> motionProperties) { mMotionProperties = std::move(motionProperties); }
        const std::shared_ptr<motion::TypedBundle>& getMotionProperties() const { return mMotionProperties; }

    private:
        static float interpolate(float start, float end, float defaultValue, float progress);
        void parseCustom(CLElement* custom);

        JavaHashMap<motion::CustomVariable> mCustom;
        std::shared_ptr<motion::TypedBundle> mMotionProperties;
    };

}
