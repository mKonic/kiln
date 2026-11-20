#pragma once

// WidgetFrame (constraintlayout-core): a widget's laid-out bounds plus the graphics layer values
// a ConstraintSet gives it (pivot, rotation, translation, scale, alpha). The layout subset: the
// custom attributes and motion properties MotionLayout interpolates are not ported.

#include <algorithm>
#include <cmath>
#include <limits>

namespace kiln::constraintlayout::core {

    class ConstraintWidget;

    class WidgetFrame {
    public:
        static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

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

        WidgetFrame() = default;
        explicit WidgetFrame(ConstraintWidget* w) : widget(w) {}
        WidgetFrame(const WidgetFrame& frame) { *this = frame; }
        WidgetFrame& operator=(const WidgetFrame& frame) {
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

        void updateAttributes(const WidgetFrame& frame) {
            pivotX = frame.pivotX;
            pivotY = frame.pivotY;
            rotationX = frame.rotationX;
            rotationY = frame.rotationY;
            rotationZ = frame.rotationZ;
            translationX = frame.translationX;
            translationY = frame.translationY;
            translationZ = frame.translationZ;
            scaleX = frame.scaleX;
            scaleY = frame.scaleY;
            alpha = frame.alpha;
            visibility = frame.visibility;
        }

        bool isDefaultTransform() const {
            return std::isnan(rotationX) && std::isnan(rotationY) && std::isnan(rotationZ) && std::isnan(translationX) &&
                   std::isnan(translationY) && std::isnan(translationZ) && std::isnan(scaleX) && std::isnan(scaleY) && std::isnan(alpha);
        }

        // Takes the widget's laid-out bounds and its own frame's attributes.
        WidgetFrame& update();
        WidgetFrame& update(ConstraintWidget* w) {
            if (w == nullptr) return *this;
            widget = w;
            return update();
        }
    };

}
