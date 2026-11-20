#pragma once

// HyperSpline (constraintlayout-core motion utils): natural cubic splines through points in
// any number of dimensions, parameterised by arc length.

#include <cmath>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class HyperSpline {
    public:
        struct Cubic {
            double mA = 0, mB = 0, mC = 0, mD = 0;
            double eval(double u) const { return (((mD * u) + mC) * u + mB) * u + mA; }
            double vel(double v) const { return (mD * 3 * v + mC * 2) * v + mB; }
        };

        HyperSpline() = default;
        explicit HyperSpline(const std::vector<std::vector<double>>& points) { setup(points); }

        void setup(const std::vector<std::vector<double>>& points) {
            mDimensionality = int(points[0].size());
            mPoints = int(points.size());
            mCtl.assign(std::size_t(mDimensionality), std::vector<double>(std::size_t(mPoints)));
            mCurve.assign(std::size_t(mDimensionality), {});
            for (int d = 0; d < mDimensionality; d++)
                for (int p = 0; p < mPoints; p++) mCtl[std::size_t(d)][std::size_t(p)] = points[std::size_t(p)][std::size_t(d)];
            for (int d = 0; d < mDimensionality; d++) mCurve[std::size_t(d)] = calcNaturalCubic(int(mCtl[std::size_t(d)].size()), mCtl[std::size_t(d)]);
            mCurveLength.assign(std::size_t(mPoints - 1), 0);
            mTotalLength = 0;
            std::vector<Cubic> temp(static_cast<std::size_t>(mDimensionality));
            for (std::size_t p = 0; p < mCurveLength.size(); p++) {
                for (int d = 0; d < mDimensionality; d++) temp[std::size_t(d)] = mCurve[std::size_t(d)][p];
                mTotalLength += mCurveLength[p] = approxLength(temp);
            }
        }

        void getVelocity(double p, double* v, int n) const {
            double pos = p * mTotalLength;
            const std::size_t k = segment(pos);
            for (int i = 0; i < n; i++) v[i] = mCurve[std::size_t(i)][k].vel(pos / mCurveLength[k]);
        }
        void getPos(double p, double* x, int n) const {
            double pos = p * mTotalLength;
            const std::size_t k = segment(pos);
            for (int i = 0; i < n; i++) x[i] = mCurve[std::size_t(i)][k].eval(pos / mCurveLength[k]);
        }
        void getPos(double p, float* x, int n) const {
            double pos = p * mTotalLength;
            const std::size_t k = segment(pos);
            for (int i = 0; i < n; i++) x[i] = float(mCurve[std::size_t(i)][k].eval(pos / mCurveLength[k]));
        }
        double getPos(double p, int splineNumber) const {
            double pos = p * mTotalLength;
            const std::size_t k = segment(pos);
            return mCurve[std::size_t(splineNumber)][k].eval(pos / mCurveLength[k]);
        }

        static double approxLength(const std::vector<Cubic>& curve) {
            double sum = 0;
            const std::size_t n = curve.size();
            std::vector<double> old(n);
            for (double i = 0; i < 1; i += .1) {
                double s = 0;
                for (std::size_t j = 0; j < n; j++) {
                    double tmp = old[j];
                    tmp -= old[j] = curve[j].eval(i);
                    s += tmp * tmp;
                }
                if (i > 0) sum += std::sqrt(s);
            }
            double s = 0;
            for (std::size_t j = 0; j < n; j++) {
                double tmp = old[j];
                tmp -= old[j] = curve[j].eval(1);
                s += tmp * tmp;
            }
            sum += std::sqrt(s);
            return sum;
        }

    private:
        std::size_t segment(double& pos) const {
            std::size_t k = 0;
            for (; k + 1 < mCurveLength.size() && mCurveLength[k] < pos; k++) pos -= mCurveLength[k];
            return k;
        }

        static std::vector<Cubic> calcNaturalCubic(int n, const std::vector<double>& x) {
            std::vector<double> gamma(static_cast<std::size_t>(n)), delta(static_cast<std::size_t>(n)), d(static_cast<std::size_t>(n));
            n -= 1;
            gamma[0] = 1.0f / 2.0f;
            for (int i = 1; i < n; i++) gamma[std::size_t(i)] = 1 / (4 - gamma[std::size_t(i - 1)]);
            gamma[std::size_t(n)] = 1 / (2 - gamma[std::size_t(n - 1)]);
            delta[0] = 3 * (x[1] - x[0]) * gamma[0];
            for (int i = 1; i < n; i++)
                delta[std::size_t(i)] = (3 * (x[std::size_t(i + 1)] - x[std::size_t(i - 1)]) - delta[std::size_t(i - 1)]) * gamma[std::size_t(i)];
            delta[std::size_t(n)] = (3 * (x[std::size_t(n)] - x[std::size_t(n - 1)]) - delta[std::size_t(n - 1)]) * gamma[std::size_t(n)];
            d[std::size_t(n)] = delta[std::size_t(n)];
            for (int i = n - 1; i >= 0; i--) d[std::size_t(i)] = delta[std::size_t(i)] - gamma[std::size_t(i)] * d[std::size_t(i + 1)];
            std::vector<Cubic> c(static_cast<std::size_t>(n));
            for (int i = 0; i < n; i++) {
                const std::size_t u = std::size_t(i);
                c[u] = { double(float(x[u])), d[u], 3 * (x[u + 1] - x[u]) - 2 * d[u] - d[u + 1], 2 * (x[u] - x[u + 1]) + d[u] + d[u + 1] };
            }
            return c;
        }

        int mPoints = 0;
        std::vector<std::vector<Cubic>> mCurve;
        int mDimensionality = 0;
        std::vector<double> mCurveLength;
        double mTotalLength = 0;
        std::vector<std::vector<double>> mCtl;
    };

}
