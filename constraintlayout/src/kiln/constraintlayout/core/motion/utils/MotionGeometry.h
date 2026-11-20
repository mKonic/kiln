#pragma once

// Small motion utils (constraintlayout-core motion utils): Rect, FloatRect, ViewState, and
// the StopEngine / DifferentialInterpolator interfaces of animated stops.

#include <string>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;

    struct Rect {
        int bottom = 0;
        int left = 0;
        int right = 0;
        int top = 0;
        int width() const { return right - left; }
        int height() const { return bottom - top; }
    };

    struct FloatRect {
        float bottom = 0;
        float left = 0;
        float right = 0;
        float top = 0;
        float centerX() const { return (left + right) * 0.5f; }
        float centerY() const { return (top + bottom) * 0.5f; }
    };

    struct ViewState {
        float rotation = 0;
        int left = 0, top = 0, right = 0, bottom = 0;
        void getState(MotionWidget& v);
        int width() const { return right - left; }
        int height() const { return bottom - top; }
    };

    class StopEngine {
    public:
        virtual ~StopEngine() = default;
        virtual std::string debug(const std::string& desc, float time) = 0;
        virtual float getVelocity(float time) = 0;
        virtual float getInterpolation(float time) = 0;
        virtual float getVelocity() = 0;
        virtual bool isStopped() = 0;
    };

    class DifferentialInterpolator {
    public:
        virtual ~DifferentialInterpolator() = default;
        virtual float getInterpolation(float v) = 0;
        virtual float getVelocity() = 0;
    };

}
