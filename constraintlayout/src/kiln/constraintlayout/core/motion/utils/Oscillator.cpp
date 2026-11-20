#include "kiln/constraintlayout/core/motion/utils/Oscillator.h"

#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"

#include <algorithm>
#include <cmath>

namespace kiln::constraintlayout::core::motion {

    namespace {
        // Arrays.binarySearch: the index, or -(insertion point) - 1.
        int binarySearch(const std::vector<double>& a, double key) {
            const auto it = std::lower_bound(a.begin(), a.end(), key);
            if (it != a.end() && *it == key) return int(it - a.begin());
            return -int(it - a.begin()) - 1;
        }
        double signum(double v) { return v > 0 ? 1.0 : v < 0 ? -1.0 : v; }

        template<typename T>
        std::string arrayString(const std::vector<T>& a) {
            std::string s = "[";
            for (std::size_t i = 0; i < a.size(); i++) s += (i ? ", " : "") + std::to_string(a[i]);
            return s + "]";
        }
    }

    std::string Oscillator::toString() const { return "pos =" + arrayString(mPosition) + " period=" + arrayString(mPeriod); }

    void Oscillator::setType(int type, const std::string& customType) {
        mType = type;
        mCustomType = customType;
        if (!mCustomType.empty()) mCustomCurve = MonotonicCurveFit::buildWave(customType);
    }

    void Oscillator::addPoint(double position, float period) {
        const int len = int(mPeriod.size()) + 1;
        int j = binarySearch(mPosition, position);
        if (j < 0) j = -j - 1;
        mPosition.resize(std::size_t(len), 0.0);
        mPeriod.resize(std::size_t(len), 0.0f);
        mArea.assign(std::size_t(len), 0.0);
        // System.arraycopy(mPosition, j, mPosition, j + 1, len - j - 1); upstream shifts only the
        // positions, not the periods.
        std::copy_backward(mPosition.begin() + j, mPosition.begin() + (len - 1), mPosition.begin() + len);
        mPosition[std::size_t(j)] = position;
        mPeriod[std::size_t(j)] = period;
        mNormalized = false;
    }

    void Oscillator::normalize() {
        double totalArea = 0;
        double totalCount = 0;
        for (float p : mPeriod) totalCount += p;
        for (std::size_t i = 1; i < mPeriod.size(); i++) {
            const float h = (mPeriod[i - 1] + mPeriod[i]) / 2;
            const double w = mPosition[i] - mPosition[i - 1];
            totalArea = totalArea + w * h;
        }
        for (float& p : mPeriod) p *= float(totalCount / totalArea);
        mArea[0] = 0;
        for (std::size_t i = 1; i < mPeriod.size(); i++) {
            const float h = (mPeriod[i - 1] + mPeriod[i]) / 2;
            const double w = mPosition[i] - mPosition[i - 1];
            mArea[i] = mArea[i - 1] + w * h;
        }
        mNormalized = true;
    }

    double Oscillator::getP(double time) {
        if (time <= 0.0) return 0.0;
        if (time >= 1.0) return 1.0;
        int index = binarySearch(mPosition, time);
        if (index < 0) index = -index - 1;
        const std::size_t i = std::size_t(index);
        const double m = (mPeriod[i] - mPeriod[i - 1]) / (mPosition[i] - mPosition[i - 1]);
        return mArea[i - 1] + (mPeriod[i - 1] - m * mPosition[i - 1]) * (time - mPosition[i - 1]) +
               m * (time * time - mPosition[i - 1] * mPosition[i - 1]) / 2;
    }

    double Oscillator::getValue(double time, double phase) {
        const double angle = phase + getP(time);
        switch (mType) {
            default:
            case SIN_WAVE:
                return std::sin(mPI2 * angle);
            case SQUARE_WAVE:
                return signum(0.5 - std::fmod(angle, 1));
            case TRIANGLE_WAVE:
                return 1 - std::abs(std::fmod(angle * 4 + 1, 4) - 2);
            case SAW_WAVE:
                return std::fmod(angle * 2 + 1, 2) - 1;
            case REVERSE_SAW_WAVE:
                return 1 - std::fmod(angle * 2 + 1, 2);
            case COS_WAVE:
                return std::cos(mPI2 * (phase + angle));
            case BOUNCE: {
                const double x = 1 - std::abs(std::fmod(angle * 4, 4) - 2);
                return 1 - x * x;
            }
            case CUSTOM:
                return mCustomCurve->getPos(std::fmod(angle, 1), 0);
        }
    }

    double Oscillator::getDP(double time) {
        if (time <= 0.0) return 0.0;
        if (time >= 1.0) return 1.0;
        int index = binarySearch(mPosition, time);
        if (index < 0) index = -index - 1;
        const std::size_t i = std::size_t(index);
        const double m = (mPeriod[i] - mPeriod[i - 1]) / (mPosition[i] - mPosition[i - 1]);
        return m * time + (mPeriod[i - 1] - m * mPosition[i - 1]);
    }

    double Oscillator::getSlope(double time, double phase, double dphase) {
        const double angle = phase + getP(time);
        const double dangle_dtime = getDP(time) + dphase;
        switch (mType) {
            default:
            case SIN_WAVE:
                return mPI2 * dangle_dtime * std::cos(mPI2 * angle);
            case SQUARE_WAVE:
                return 0;
            case TRIANGLE_WAVE:
                return 4 * dangle_dtime * signum(std::fmod(angle * 4 + 3, 4) - 2);
            case SAW_WAVE:
                return dangle_dtime * 2;
            case REVERSE_SAW_WAVE:
                return -dangle_dtime * 2;
            case COS_WAVE:
                return -mPI2 * dangle_dtime * std::sin(mPI2 * angle);
            case BOUNCE:
                return 4 * dangle_dtime * (std::fmod(angle * 4 + 2, 4) - 2);
            case CUSTOM:
                return mCustomCurve->getSlope(std::fmod(angle, 1), 0);
        }
    }

}
