#pragma once

// Easing (constraintlayout-core motion utils): MotionLayout's easing curves: cubic Béziers
// (named or "cubic(x1, y1, x2, y2)"), step splines ("spline(...)") and Schlick's bias / gain
// ("Schlick(s, t)").

#include <memory>
#include <string>

namespace kiln::constraintlayout::core::motion {

    class MonotonicCurveFit;

    class Easing {
    public:
        static constexpr const char* NAMED_EASING[] = { "standard", "accelerate", "decelerate", "linear" };

        virtual ~Easing() = default;
        // Null for an empty string (Java's null); the identity for one it cannot read.
        static std::shared_ptr<Easing> getInterpolator(const std::string& configString);
        virtual double get(double x) { return x; }
        virtual double getDiff(double /*x*/) { return 1; }
        std::string toString() const { return mStr; }

    protected:
        std::string mStr = "identity";
    };

    class CubicEasing : public Easing {
    public:
        explicit CubicEasing(const std::string& configString);
        CubicEasing(double x1, double y1, double x2, double y2) { setup(x1, y1, x2, y2); }
        void setup(double x1, double y1, double x2, double y2) {
            mX1 = x1;
            mY1 = y1;
            mX2 = x2;
            mY2 = y2;
        }
        double getDiff(double x) override;
        double get(double x) override;

    private:
        double getX(double t) const;
        double getY(double t) const;
        static constexpr double sError = 0.01;
        static constexpr double sDError = 0.0001;
        double mX1 = 0, mY1 = 0, mX2 = 0, mY2 = 0;
    };

    class StepCurve : public Easing {
    public:
        explicit StepCurve(const std::string& configString);
        double getDiff(double x) override;
        double get(double x) override;

    private:
        std::shared_ptr<MonotonicCurveFit> mCurveFit;
    };

    class Schlick : public Easing {
    public:
        explicit Schlick(const std::string& configString);
        double getDiff(double x) override { return dfunc(x); }
        double get(double x) override { return func(x); }

    private:
        double func(double x) const;
        double dfunc(double x) const;
        double mS = 0, mT = 0;
    };

    // Double.parseDouble of a trimmed substring; throws std::invalid_argument when it is not a number.
    double parseJavaDouble(const std::string& s);
    std::string javaTrim(const std::string& s);

}
