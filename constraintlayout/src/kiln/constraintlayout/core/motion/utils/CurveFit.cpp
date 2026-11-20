#include "kiln/constraintlayout/core/motion/utils/CurveFit.h"

#include "kiln/constraintlayout/core/motion/utils/Easing.h"

#include <algorithm>
#include <cmath>

namespace kiln::constraintlayout::core::motion {

    std::shared_ptr<CurveFit> CurveFit::get(int type, const std::vector<double>& time, const std::vector<std::vector<double>>& y) {
        if (time.size() == 1) type = CONSTANT;
        switch (type) {
            case SPLINE:
                return std::make_shared<MonotonicCurveFit>(time, y);
            case CONSTANT:
                return std::make_shared<ConstantCurveFit>(time[0], y[0]);
            default:
                return std::make_shared<LinearCurveFit>(time, y);
        }
    }

    std::shared_ptr<CurveFit> CurveFit::getArc(const std::vector<int>& arcModes, const std::vector<double>& time,
                                               const std::vector<std::vector<double>>& y) {
        return std::make_shared<ArcCurveFit>(arcModes, time, y);
    }

    // ConstantCurveFit

    void ConstantCurveFit::getPos(double, double* v) { std::copy(mValue.begin(), mValue.end(), v); }
    void ConstantCurveFit::getPos(double, float* v) {
        for (std::size_t i = 0; i < mValue.size(); i++) v[i] = float(mValue[i]);
    }
    void ConstantCurveFit::getSlope(double, double* v) {
        for (std::size_t i = 0; i < mValue.size(); i++) v[i] = 0;
    }

    // MonotonicCurveFit

    MonotonicCurveFit::MonotonicCurveFit(std::vector<double> time, std::vector<std::vector<double>> y) {
        const int n = int(time.size());
        const int dim = int(y[0].size());
        mSlopeTemp.assign(std::size_t(dim), 0);
        std::vector<std::vector<double>> slope(std::size_t(n - 1), std::vector<double>(std::size_t(dim)));
        std::vector<std::vector<double>> tangent(static_cast<std::size_t>(n), std::vector<double>(static_cast<std::size_t>(dim)));
        for (int j = 0; j < dim; j++) {
            for (int i = 0; i < n - 1; i++) {
                const double dt = time[std::size_t(i + 1)] - time[std::size_t(i)];
                slope[i][j] = (y[i + 1][j] - y[i][j]) / dt;
                if (i == 0) tangent[i][j] = slope[i][j];
                else tangent[i][j] = (slope[i - 1][j] + slope[i][j]) * 0.5f;
            }
            tangent[n - 1][j] = slope[n - 2][j];
        }
        for (int i = 0; i < n - 1; i++) {
            for (int j = 0; j < dim; j++) {
                if (slope[i][j] == 0.) {
                    tangent[i][j] = 0.;
                    tangent[i + 1][j] = 0.;
                } else {
                    const double a = tangent[i][j] / slope[i][j];
                    const double b = tangent[i + 1][j] / slope[i][j];
                    const double h = std::hypot(a, b);
                    if (h > 9.0) {
                        const double t = 3. / h;
                        tangent[i][j] = t * a * slope[i][j];
                        tangent[i + 1][j] = t * b * slope[i][j];
                    }
                }
            }
        }
        mT = std::move(time);
        mY = std::move(y);
        mTangent = std::move(tangent);
    }

    void MonotonicCurveFit::getPos(double t, double* v) {
        const int n = int(mT.size());
        const int dim = int(mY[0].size());
        if (mExtrapolate) {
            if (t <= mT[0]) {
                getSlope(mT[0], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = mY[0][j] + (t - mT[0]) * mSlopeTemp[j];
                return;
            }
            if (t >= mT[n - 1]) {
                getSlope(mT[n - 1], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = mY[n - 1][j] + (t - mT[n - 1]) * mSlopeTemp[j];
                return;
            }
        } else {
            if (t <= mT[0]) {
                for (int j = 0; j < dim; j++) v[j] = mY[0][j];
                return;
            }
            if (t >= mT[n - 1]) {
                for (int j = 0; j < dim; j++) v[j] = mY[n - 1][j];
                return;
            }
        }
        for (int i = 0; i < n - 1; i++) {
            if (t == mT[i])
                for (int j = 0; j < dim; j++) v[j] = mY[i][j];
            if (t < mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                for (int j = 0; j < dim; j++) v[j] = interpolate(h, x, mY[i][j], mY[i + 1][j], mTangent[i][j], mTangent[i + 1][j]);
                return;
            }
        }
    }

    void MonotonicCurveFit::getPos(double t, float* v) {
        const int n = int(mT.size());
        const int dim = int(mY[0].size());
        if (mExtrapolate) {
            if (t <= mT[0]) {
                getSlope(mT[0], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = float(mY[0][j] + (t - mT[0]) * mSlopeTemp[j]);
                return;
            }
            if (t >= mT[n - 1]) {
                getSlope(mT[n - 1], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = float(mY[n - 1][j] + (t - mT[n - 1]) * mSlopeTemp[j]);
                return;
            }
        } else {
            if (t <= mT[0]) {
                for (int j = 0; j < dim; j++) v[j] = float(mY[0][j]);
                return;
            }
            if (t >= mT[n - 1]) {
                for (int j = 0; j < dim; j++) v[j] = float(mY[n - 1][j]);
                return;
            }
        }
        for (int i = 0; i < n - 1; i++) {
            if (t == mT[i])
                for (int j = 0; j < dim; j++) v[j] = float(mY[i][j]);
            if (t < mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                for (int j = 0; j < dim; j++) v[j] = float(interpolate(h, x, mY[i][j], mY[i + 1][j], mTangent[i][j], mTangent[i + 1][j]));
                return;
            }
        }
    }

    double MonotonicCurveFit::getPos(double t, int j) {
        const int n = int(mT.size());
        if (mExtrapolate) {
            if (t <= mT[0]) return mY[0][j] + (t - mT[0]) * getSlope(mT[0], j);
            if (t >= mT[n - 1]) return mY[n - 1][j] + (t - mT[n - 1]) * getSlope(mT[n - 1], j);
        } else {
            if (t <= mT[0]) return mY[0][j];
            if (t >= mT[n - 1]) return mY[n - 1][j];
        }
        for (int i = 0; i < n - 1; i++) {
            if (t == mT[i]) return mY[i][j];
            if (t < mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                return interpolate(h, x, mY[i][j], mY[i + 1][j], mTangent[i][j], mTangent[i + 1][j]);
            }
        }
        return 0;
    }

    void MonotonicCurveFit::getSlope(double t, double* v) {
        const int n = int(mT.size());
        const int dim = int(mY[0].size());
        if (t <= mT[0]) t = mT[0];
        else if (t >= mT[n - 1]) t = mT[n - 1];
        for (int i = 0; i < n - 1; i++) {
            if (t <= mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                for (int j = 0; j < dim; j++) v[j] = diff(h, x, mY[i][j], mY[i + 1][j], mTangent[i][j], mTangent[i + 1][j]) / h;
                break;
            }
        }
    }

    double MonotonicCurveFit::getSlope(double t, int j) {
        const int n = int(mT.size());
        if (t < mT[0]) t = mT[0];
        else if (t >= mT[n - 1]) t = mT[n - 1];
        for (int i = 0; i < n - 1; i++) {
            if (t <= mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                return diff(h, x, mY[i][j], mY[i + 1][j], mTangent[i][j], mTangent[i + 1][j]) / h;
            }
        }
        return 0;
    }

    double MonotonicCurveFit::interpolate(double h, double x, double y1, double y2, double t1, double t2) {
        const double x2 = x * x;
        const double x3 = x2 * x;
        return -2 * x3 * y2 + 3 * x2 * y2 + 2 * x3 * y1 - 3 * x2 * y1 + y1 + h * t2 * x3 + h * t1 * x3 - h * t2 * x2 - 2 * h * t1 * x2 + h * t1 * x;
    }

    double MonotonicCurveFit::diff(double h, double x, double y1, double y2, double t1, double t2) {
        const double x2 = x * x;
        return -6 * x2 * y2 + 6 * x * y2 + 6 * x2 * y1 - 6 * x * y1 + 3 * h * t2 * x2 + 3 * h * t1 * x2 - 2 * h * t2 * x - 4 * h * t1 * x + h * t1;
    }

    std::shared_ptr<MonotonicCurveFit> MonotonicCurveFit::buildWave(const std::string& configString) {
        std::vector<double> values;
        std::size_t start = configString.find('(') == std::string::npos ? 0 : configString.find('(') + 1;
        std::size_t off1 = configString.find(',', start);
        while (off1 != std::string::npos) {
            values.push_back(parseJavaDouble(configString.substr(start, off1 - start)));
            start = off1 + 1;
            off1 = configString.find(',', start);
        }
        off1 = configString.find(')', start);
        values.push_back(parseJavaDouble(configString.substr(start, off1 == std::string::npos ? std::string::npos : off1 - start)));
        return buildWave(values);
    }

    std::shared_ptr<MonotonicCurveFit> MonotonicCurveFit::buildWave(const std::vector<double>& values) {
        const int length = int(values.size()) * 3 - 2;
        const int len = int(values.size()) - 1;
        const double gap = 1.0 / len;
        std::vector<std::vector<double>> points(static_cast<std::size_t>(length), std::vector<double>(1));
        std::vector<double> time(static_cast<std::size_t>(length));
        for (int i = 0; i < int(values.size()); i++) {
            const double v = values[std::size_t(i)];
            points[i + len][0] = v;
            time[i + len] = i * gap;
            if (i > 0) {
                points[i + len * 2][0] = v + 1;
                time[i + len * 2] = i * gap + 1;
                points[i - 1][0] = v - 1 - gap;
                time[i - 1] = i * gap + -1 - gap;
            }
        }
        return std::make_shared<MonotonicCurveFit>(std::move(time), std::move(points));
    }

    // LinearCurveFit

    LinearCurveFit::LinearCurveFit(std::vector<double> time, std::vector<std::vector<double>> y) {
        const int dim = int(y[0].size());
        mSlopeTemp.assign(std::size_t(dim), 0);
        // Upstream sums a 2D length it never uses for more than two dimensions; only its
        // result, zero, is kept.
        if (dim > 2) mTotalLength = 0;
        mT = std::move(time);
        mY = std::move(y);
    }

    void LinearCurveFit::getPos(double t, double* v) {
        const int n = int(mT.size());
        const int dim = int(mY[0].size());
        if (mExtrapolate) {
            if (t <= mT[0]) {
                getSlope(mT[0], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = mY[0][j] + (t - mT[0]) * mSlopeTemp[j];
                return;
            }
            if (t >= mT[n - 1]) {
                getSlope(mT[n - 1], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = mY[n - 1][j] + (t - mT[n - 1]) * mSlopeTemp[j];
                return;
            }
        } else {
            if (t <= mT[0]) {
                for (int j = 0; j < dim; j++) v[j] = mY[0][j];
                return;
            }
            if (t >= mT[n - 1]) {
                for (int j = 0; j < dim; j++) v[j] = mY[n - 1][j];
                return;
            }
        }
        for (int i = 0; i < n - 1; i++) {
            if (t == mT[i])
                for (int j = 0; j < dim; j++) v[j] = mY[i][j];
            if (t < mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                for (int j = 0; j < dim; j++) v[j] = mY[i][j] * (1 - x) + mY[i + 1][j] * x;
                return;
            }
        }
    }

    void LinearCurveFit::getPos(double t, float* v) {
        const int n = int(mT.size());
        const int dim = int(mY[0].size());
        if (mExtrapolate) {
            if (t <= mT[0]) {
                getSlope(mT[0], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = float(mY[0][j] + (t - mT[0]) * mSlopeTemp[j]);
                return;
            }
            if (t >= mT[n - 1]) {
                getSlope(mT[n - 1], mSlopeTemp.data());
                for (int j = 0; j < dim; j++) v[j] = float(mY[n - 1][j] + (t - mT[n - 1]) * mSlopeTemp[j]);
                return;
            }
        } else {
            if (t <= mT[0]) {
                for (int j = 0; j < dim; j++) v[j] = float(mY[0][j]);
                return;
            }
            if (t >= mT[n - 1]) {
                for (int j = 0; j < dim; j++) v[j] = float(mY[n - 1][j]);
                return;
            }
        }
        for (int i = 0; i < n - 1; i++) {
            if (t == mT[i])
                for (int j = 0; j < dim; j++) v[j] = float(mY[i][j]);
            if (t < mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                for (int j = 0; j < dim; j++) v[j] = float(mY[i][j] * (1 - x) + mY[i + 1][j] * x);
                return;
            }
        }
    }

    double LinearCurveFit::getPos(double t, int j) {
        const int n = int(mT.size());
        if (mExtrapolate) {
            if (t <= mT[0]) return mY[0][j] + (t - mT[0]) * getSlope(mT[0], j);
            if (t >= mT[n - 1]) return mY[n - 1][j] + (t - mT[n - 1]) * getSlope(mT[n - 1], j);
        } else {
            if (t <= mT[0]) return mY[0][j];
            if (t >= mT[n - 1]) return mY[n - 1][j];
        }
        for (int i = 0; i < n - 1; i++) {
            if (t == mT[i]) return mY[i][j];
            if (t < mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                const double x = (t - mT[i]) / h;
                return mY[i][j] * (1 - x) + mY[i + 1][j] * x;
            }
        }
        return 0;
    }

    void LinearCurveFit::getSlope(double t, double* v) {
        const int n = int(mT.size());
        const int dim = int(mY[0].size());
        if (t <= mT[0]) t = mT[0];
        else if (t >= mT[n - 1]) t = mT[n - 1];
        for (int i = 0; i < n - 1; i++) {
            if (t <= mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                for (int j = 0; j < dim; j++) v[j] = (mY[i + 1][j] - mY[i][j]) / h;
                break;
            }
        }
    }

    double LinearCurveFit::getSlope(double t, int j) {
        const int n = int(mT.size());
        if (t < mT[0]) t = mT[0];
        else if (t >= mT[n - 1]) t = mT[n - 1];
        for (int i = 0; i < n - 1; i++) {
            if (t <= mT[i + 1]) {
                const double h = mT[i + 1] - mT[i];
                return (mY[i + 1][j] - mY[i][j]) / h;
            }
        }
        return 0;
    }

    // ArcCurveFit

    ArcCurveFit::ArcCurveFit(const std::vector<int>& arcModes, std::vector<double> time, const std::vector<std::vector<double>>& y)
        : mTime(std::move(time)) {
        int mode = START_VERTICAL;
        int last = START_VERTICAL;
        for (std::size_t i = 0; i + 1 < mTime.size(); i++) {
            switch (arcModes[i]) {
                case ARC_START_VERTICAL:
                    last = mode = START_VERTICAL;
                    break;
                case ARC_START_HORIZONTAL:
                    last = mode = START_HORIZONTAL;
                    break;
                case ARC_START_FLIP:
                    mode = (last == START_VERTICAL) ? START_HORIZONTAL : START_VERTICAL;
                    last = mode;
                    break;
                case ARC_START_LINEAR:
                    mode = START_LINEAR;
                    break;
                case ARC_ABOVE:
                    mode = UP_ARC;
                    break;
                case ARC_BELOW:
                    mode = DOWN_ARC;
                    break;
            }
            mArcs.emplace_back(mode, mTime[i], mTime[i + 1], y[i][0], y[i][1], y[i + 1][0], y[i + 1][1]);
        }
    }

    void ArcCurveFit::getPos(double t, double* v) {
        if (mExtrapolate) {
            if (t < mArcs[0].mTime1) {
                const double t0 = mArcs[0].mTime1;
                const double dt = t - mArcs[0].mTime1;
                Arc& a = mArcs[0];
                if (a.mLinear) {
                    v[0] = a.getLinearX(t0) + dt * a.getLinearDX(t0);
                    v[1] = a.getLinearY(t0) + dt * a.getLinearDY(t0);
                } else {
                    a.setPoint(t0);
                    v[0] = a.getX() + dt * a.getDX();
                    v[1] = a.getY() + dt * a.getDY();
                }
                return;
            }
            if (t > mArcs.back().mTime2) {
                const double t0 = mArcs.back().mTime2;
                const double dt = t - t0;
                Arc& a = mArcs.back();
                if (a.mLinear) {
                    v[0] = a.getLinearX(t0) + dt * a.getLinearDX(t0);
                    v[1] = a.getLinearY(t0) + dt * a.getLinearDY(t0);
                } else {
                    a.setPoint(t);
                    v[0] = a.getX() + dt * a.getDX();
                    v[1] = a.getY() + dt * a.getDY();
                }
                return;
            }
        } else {
            if (t < mArcs[0].mTime1) t = mArcs[0].mTime1;
            if (t > mArcs.back().mTime2) t = mArcs.back().mTime2;
        }
        for (Arc& a : mArcs) {
            if (t <= a.mTime2) {
                if (a.mLinear) {
                    v[0] = a.getLinearX(t);
                    v[1] = a.getLinearY(t);
                    return;
                }
                a.setPoint(t);
                v[0] = a.getX();
                v[1] = a.getY();
                return;
            }
        }
    }

    void ArcCurveFit::getPos(double t, float* v) {
        if (mExtrapolate) {
            if (t < mArcs[0].mTime1) {
                const double t0 = mArcs[0].mTime1;
                const double dt = t - mArcs[0].mTime1;
                Arc& a = mArcs[0];
                if (a.mLinear) {
                    v[0] = float(a.getLinearX(t0) + dt * a.getLinearDX(t0));
                    v[1] = float(a.getLinearY(t0) + dt * a.getLinearDY(t0));
                } else {
                    a.setPoint(t0);
                    v[0] = float(a.getX() + dt * a.getDX());
                    v[1] = float(a.getY() + dt * a.getDY());
                }
                return;
            }
            if (t > mArcs.back().mTime2) {
                const double t0 = mArcs.back().mTime2;
                const double dt = t - t0;
                Arc& a = mArcs.back();
                if (a.mLinear) {
                    v[0] = float(a.getLinearX(t0) + dt * a.getLinearDX(t0));
                    v[1] = float(a.getLinearY(t0) + dt * a.getLinearDY(t0));
                } else {
                    a.setPoint(t);
                    v[0] = float(a.getX());
                    v[1] = float(a.getY());
                }
                return;
            }
        } else {
            if (t < mArcs[0].mTime1) t = mArcs[0].mTime1;
            else if (t > mArcs.back().mTime2) t = mArcs.back().mTime2;
        }
        for (Arc& a : mArcs) {
            if (t <= a.mTime2) {
                if (a.mLinear) {
                    v[0] = float(a.getLinearX(t));
                    v[1] = float(a.getLinearY(t));
                    return;
                }
                a.setPoint(t);
                v[0] = float(a.getX());
                v[1] = float(a.getY());
                return;
            }
        }
    }

    void ArcCurveFit::getSlope(double t, double* v) {
        if (t < mArcs[0].mTime1) t = mArcs[0].mTime1;
        else if (t > mArcs.back().mTime2) t = mArcs.back().mTime2;
        for (Arc& a : mArcs) {
            if (t <= a.mTime2) {
                if (a.mLinear) {
                    v[0] = a.getLinearDX(t);
                    v[1] = a.getLinearDY(t);
                    return;
                }
                a.setPoint(t);
                v[0] = a.getDX();
                v[1] = a.getDY();
                return;
            }
        }
    }

    double ArcCurveFit::getPos(double t, int j) {
        if (mExtrapolate) {
            if (t < mArcs[0].mTime1) {
                const double t0 = mArcs[0].mTime1;
                const double dt = t - mArcs[0].mTime1;
                Arc& a = mArcs[0];
                if (a.mLinear) {
                    if (j == 0) return a.getLinearX(t0) + dt * a.getLinearDX(t0);
                    return a.getLinearY(t0) + dt * a.getLinearDY(t0);
                }
                a.setPoint(t0);
                if (j == 0) return a.getX() + dt * a.getDX();
                return a.getY() + dt * a.getDY();
            }
            if (t > mArcs.back().mTime2) {
                const double t0 = mArcs.back().mTime2;
                const double dt = t - t0;
                Arc& a = mArcs.back();
                if (j == 0) return a.getLinearX(t0) + dt * a.getLinearDX(t0);
                return a.getLinearY(t0) + dt * a.getLinearDY(t0);
            }
        } else {
            if (t < mArcs[0].mTime1) t = mArcs[0].mTime1;
            else if (t > mArcs.back().mTime2) t = mArcs.back().mTime2;
        }
        for (Arc& a : mArcs) {
            if (t <= a.mTime2) {
                if (a.mLinear) {
                    if (j == 0) return a.getLinearX(t);
                    return a.getLinearY(t);
                }
                a.setPoint(t);
                if (j == 0) return a.getX();
                return a.getY();
            }
        }
        return NAN;
    }

    double ArcCurveFit::getSlope(double t, int j) {
        if (t < mArcs[0].mTime1) t = mArcs[0].mTime1;
        if (t > mArcs.back().mTime2) t = mArcs.back().mTime2;
        for (Arc& a : mArcs) {
            if (t <= a.mTime2) {
                if (a.mLinear) {
                    if (j == 0) return a.getLinearDX(t);
                    return a.getLinearDY(t);
                }
                a.setPoint(t);
                if (j == 0) return a.getDX();
                return a.getDY();
            }
        }
        return NAN;
    }

    namespace {
        // Arc.sOurPercent: shared by every arc, as upstream's static array is.
        double sOurPercent[91];
        constexpr double kDegreesToRadians = 0.017453292519943295;   // Math.toRadians
        constexpr double kPi = 3.141592653589793;
    }

    ArcCurveFit::Arc::Arc(int mode, double t1, double t2, double x1, double y1, double x2, double y2) {
        const double dx = x2 - x1;
        const double dy = y2 - y1;
        switch (mode) {
            case START_VERTICAL:
                mVertical = true;
                break;
            case UP_ARC:
                mVertical = dy < 0;
                break;
            case DOWN_ARC:
                mVertical = dy > 0;
                break;
            default:
                mVertical = false;
        }
        mTime1 = t1;
        mTime2 = t2;
        mOneOverDeltaTime = 1 / (mTime2 - mTime1);
        if (START_LINEAR == mode) mLinear = true;
        if (mLinear || std::abs(dx) < EPSILON || std::abs(dy) < EPSILON) {
            mLinear = true;
            mX1 = x1;
            mX2 = x2;
            mY1 = y1;
            mY2 = y2;
            mArcDistance = std::hypot(dy, dx);
            mArcVelocity = mArcDistance * mOneOverDeltaTime;
            mEllipseCenterX = dx / (mTime2 - mTime1);
            mEllipseCenterY = dy / (mTime2 - mTime1);
            return;
        }
        mLut.assign(101, 0);
        mEllipseA = dx * (mVertical ? -1 : 1);
        mEllipseB = dy * (mVertical ? 1 : -1);
        mEllipseCenterX = mVertical ? x2 : x1;
        mEllipseCenterY = mVertical ? y1 : y2;
        buildTable(x1, y1, x2, y2);
        mArcVelocity = mArcDistance * mOneOverDeltaTime;
    }

    void ArcCurveFit::Arc::setPoint(double time) {
        const double percent = (mVertical ? (mTime2 - time) : (time - mTime1)) * mOneOverDeltaTime;
        const double angle = kPi * 0.5 * lookup(percent);
        mTmpSinAngle = std::sin(angle);
        mTmpCosAngle = std::cos(angle);
    }

    double ArcCurveFit::Arc::getDX() const {
        const double vx = mEllipseA * mTmpCosAngle;
        const double vy = -mEllipseB * mTmpSinAngle;
        const double norm = mArcVelocity / std::hypot(vx, vy);
        return mVertical ? -vx * norm : vx * norm;
    }

    double ArcCurveFit::Arc::getDY() const {
        const double vx = mEllipseA * mTmpCosAngle;
        const double vy = -mEllipseB * mTmpSinAngle;
        const double norm = mArcVelocity / std::hypot(vx, vy);
        return mVertical ? -vy * norm : vy * norm;
    }

    double ArcCurveFit::Arc::getLinearX(double t) const {
        t = (t - mTime1) * mOneOverDeltaTime;
        return mX1 + t * (mX2 - mX1);
    }

    double ArcCurveFit::Arc::getLinearY(double t) const {
        t = (t - mTime1) * mOneOverDeltaTime;
        return mY1 + t * (mY2 - mY1);
    }

    double ArcCurveFit::Arc::lookup(double v) const {
        if (v <= 0) return 0;
        if (v >= 1) return 1;
        const double pos = v * double(mLut.size() - 1);
        const int iv = int(pos);
        const double off = pos - int(pos);
        return mLut[std::size_t(iv)] + (off * (mLut[std::size_t(iv) + 1] - mLut[std::size_t(iv)]));
    }

    void ArcCurveFit::Arc::buildTable(double x1, double y1, double x2, double y2) {
        const double a = x2 - x1;
        const double b = y1 - y2;
        double lx = 0, ly = 0;
        double dist = 0;
        constexpr int n = int(sizeof(sOurPercent) / sizeof(sOurPercent[0]));
        for (int i = 0; i < n; i++) {
            const double angle = (90.0 * i / (n - 1)) * kDegreesToRadians;
            const double s = std::sin(angle);
            const double c = std::cos(angle);
            const double px = a * s;
            const double py = b * c;
            if (i > 0) {
                dist += std::hypot(px - lx, py - ly);
                sOurPercent[i] = dist;
            }
            lx = px;
            ly = py;
        }
        mArcDistance = dist;
        for (int i = 0; i < n; i++) sOurPercent[i] /= dist;
        for (std::size_t i = 0; i < mLut.size(); i++) {
            const double pos = double(i) / double(mLut.size() - 1);
            // Arrays.binarySearch: the index, or -(insertion point) - 1.
            const double* found = std::lower_bound(sOurPercent, sOurPercent + n, pos);
            int index;
            if (found != sOurPercent + n && *found == pos) index = int(found - sOurPercent);
            else index = -int(found - sOurPercent) - 1;
            if (index >= 0) {
                mLut[i] = index / double(n - 1);
            } else if (index == -1) {
                mLut[i] = 0;
            } else {
                const int p1 = -index - 2;
                const int p2 = -index - 1;
                mLut[i] = (p1 + (pos - sOurPercent[p1]) / (sOurPercent[p2] - sOurPercent[p1])) / (n - 1);
            }
        }
    }

}
