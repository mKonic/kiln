#include "kiln/constraintlayout/core/motion/utils/Easing.h"

#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    std::string javaTrim(const std::string& s) {
        std::size_t b = 0, e = s.size();
        while (b < e && static_cast<unsigned char>(s[b]) <= ' ') b++;
        while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') e--;
        return s.substr(b, e - b);
    }

    double parseJavaDouble(const std::string& s) {
        std::string t = javaTrim(s);
        if (!t.empty() && (t.back() == 'd' || t.back() == 'D' || t.back() == 'f' || t.back() == 'F')) t.pop_back();
        if (t.empty()) throw std::invalid_argument("empty String");
        char* end = nullptr;
        const double v = std::strtod(t.c_str(), &end);
        if (end != t.c_str() + t.size()) throw std::invalid_argument("For input string: \"" + s + "\"");
        return v;
    }

    namespace {
        const char* const STANDARD = "cubic(0.4, 0.0, 0.2, 1)";
        const char* const ACCELERATE = "cubic(0.4, 0.05, 0.8, 0.7)";
        const char* const DECELERATE = "cubic(0.0, 0.0, 0.2, 0.95)";
        const char* const LINEAR = "cubic(1, 1, 0, 0)";
        const char* const ANTICIPATE = "cubic(0.36, 0, 0.66, -0.56)";
        const char* const OVERSHOOT = "cubic(0.34, 1.56, 0.64, 1)";

        bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

        // Java's String.indexOf(char, from): -1 when absent.
        int indexOf(const std::string& s, char c, int from = 0) {
            if (from < 0) from = 0;
            const std::size_t at = s.find(c, std::size_t(from));
            return at == std::string::npos ? -1 : int(at);
        }

        std::string substring(const std::string& s, int begin, int end) {
            if (begin < 0 || end > int(s.size()) || begin > end) throw std::out_of_range("substring");
            return s.substr(std::size_t(begin), std::size_t(end - begin));
        }

        // The values of "name(v0, v1, ...)".
        std::vector<double> readValues(const std::string& configString) {
            std::vector<double> values;
            int start = indexOf(configString, '(') + 1;
            int off1 = indexOf(configString, ',', start);
            while (off1 != -1) {
                values.push_back(parseJavaDouble(substring(configString, start, off1)));
                start = off1 + 1;
                off1 = indexOf(configString, ',', start);
            }
            off1 = indexOf(configString, ')', start);
            values.push_back(parseJavaDouble(substring(configString, start, off1)));
            return values;
        }
    }

    std::shared_ptr<Easing> Easing::getInterpolator(const std::string& configString) {
        static const std::shared_ptr<Easing> sDefault = std::make_shared<Easing>();
        if (configString.empty()) return nullptr;
        if (startsWith(configString, "cubic")) return std::make_shared<CubicEasing>(configString);
        if (startsWith(configString, "spline")) return std::make_shared<StepCurve>(configString);
        if (startsWith(configString, "Schlick")) return std::make_shared<Schlick>(configString);
        if (configString == "standard") return std::make_shared<CubicEasing>(STANDARD);
        if (configString == "accelerate") return std::make_shared<CubicEasing>(ACCELERATE);
        if (configString == "decelerate") return std::make_shared<CubicEasing>(DECELERATE);
        if (configString == "linear") return std::make_shared<CubicEasing>(LINEAR);
        if (configString == "anticipate") return std::make_shared<CubicEasing>(ANTICIPATE);
        if (configString == "overshoot") return std::make_shared<CubicEasing>(OVERSHOOT);
        std::fprintf(stderr,
                     "transitionEasing syntax error syntax:transitionEasing=\"cubic(1.0,0.5,0.0,0.6)\" or [standard, accelerate, decelerate, linear]\n");
        return sDefault;
    }

    // CubicEasing

    CubicEasing::CubicEasing(const std::string& configString) {
        mStr = configString;
        const int start = indexOf(configString, '(');
        const int off1 = indexOf(configString, ',', start);
        mX1 = parseJavaDouble(substring(configString, start + 1, off1));
        const int off2 = indexOf(configString, ',', off1 + 1);
        mY1 = parseJavaDouble(substring(configString, off1 + 1, off2));
        const int off3 = indexOf(configString, ',', off2 + 1);
        mX2 = parseJavaDouble(substring(configString, off2 + 1, off3));
        const int end = indexOf(configString, ')', off3 + 1);
        mY2 = parseJavaDouble(substring(configString, off3 + 1, end));
    }

    double CubicEasing::getX(double t) const {
        const double t1 = 1 - t;
        const double f1 = 3 * t1 * t1 * t;
        const double f2 = 3 * t1 * t * t;
        const double f3 = t * t * t;
        return mX1 * f1 + mX2 * f2 + f3;
    }

    double CubicEasing::getY(double t) const {
        const double t1 = 1 - t;
        const double f1 = 3 * t1 * t1 * t;
        const double f2 = 3 * t1 * t * t;
        const double f3 = t * t * t;
        return mY1 * f1 + mY2 * f2 + f3;
    }

    double CubicEasing::getDiff(double x) {
        double t = 0.5;
        double range = 0.5;
        while (range > sDError) {
            const double tx = getX(t);
            range *= 0.5;
            if (tx < x) t += range;
            else t -= range;
        }
        const double x1 = getX(t - range);
        const double x2 = getX(t + range);
        const double y1 = getY(t - range);
        const double y2 = getY(t + range);
        return (y2 - y1) / (x2 - x1);
    }

    double CubicEasing::get(double x) {
        if (x <= 0.0) return 0;
        if (x >= 1.0) return 1.0;
        double t = 0.5;
        double range = 0.5;
        while (range > sError) {
            const double tx = getX(t);
            range *= 0.5;
            if (tx < x) t += range;
            else t -= range;
        }
        const double x1 = getX(t - range);
        const double x2 = getX(t + range);
        const double y1 = getY(t - range);
        const double y2 = getY(t + range);
        return (y2 - y1) * (x - x1) / (x2 - x1) + y1;
    }

    // StepCurve

    StepCurve::StepCurve(const std::string& configString) {
        mStr = configString;
        // genSpline: the same periodic spline as MonotonicCurveFit.buildWave.
        mCurveFit = MonotonicCurveFit::buildWave(readValues(configString));
    }

    double StepCurve::getDiff(double x) { return mCurveFit->getSlope(x, 0); }
    double StepCurve::get(double x) { return mCurveFit->getPos(x, 0); }

    // Schlick

    Schlick::Schlick(const std::string& configString) {
        mStr = configString;
        const int start = indexOf(configString, '(');
        const int off1 = indexOf(configString, ',', start);
        mS = parseJavaDouble(substring(configString, start + 1, off1));
        const int off2 = indexOf(configString, ',', off1 + 1);
        mT = parseJavaDouble(substring(configString, off1 + 1, off2));
    }

    double Schlick::func(double x) const {
        if (x < mT) return mT * x / (x + mS * (mT - x));
        return ((1 - mT) * (x - 1)) / (1 - x - mS * (mT - x));
    }

    double Schlick::dfunc(double x) const {
        if (x < mT) return (mS * mT * mT) / ((mS * (mT - x) + x) * (mS * (mT - x) + x));
        return (mS * (mT - 1) * (mT - 1)) / ((-mS * (mT - x) - x + 1) * (-mS * (mT - x) - x + 1));
    }

}
