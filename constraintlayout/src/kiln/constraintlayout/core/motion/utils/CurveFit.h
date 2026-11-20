#pragma once

// CurveFit and its kinds (constraintlayout-core motion utils): curves through keyframe values,
// one dimension per value: monotonic splines, straight lines, a constant, and arcs (two
// dimensions, x and y, for paths drawn as quarter ellipses).

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class CurveFit {
    public:
        static constexpr int SPLINE = 0;
        static constexpr int LINEAR = 1;
        static constexpr int CONSTANT = 2;

        virtual ~CurveFit() = default;
        static std::shared_ptr<CurveFit> get(int type, const std::vector<double>& time, const std::vector<std::vector<double>>& y);
        static std::shared_ptr<CurveFit> getArc(const std::vector<int>& arcModes, const std::vector<double>& time,
                                                const std::vector<std::vector<double>>& y);
        virtual void getPos(double t, double* v) = 0;
        virtual void getPos(double t, float* v) = 0;
        virtual double getPos(double t, int j) = 0;
        virtual void getSlope(double t, double* v) = 0;
        virtual double getSlope(double t, int j) = 0;
        virtual std::vector<double> getTimePoints() const = 0;
    };

    class ConstantCurveFit : public CurveFit {
    public:
        ConstantCurveFit(double time, std::vector<double> value) : mTime(time), mValue(std::move(value)) {}
        void getPos(double, double* v) override;
        void getPos(double, float* v) override;
        double getPos(double, int j) override { return mValue[std::size_t(j)]; }
        void getSlope(double, double* v) override;
        double getSlope(double, int) override { return 0; }
        std::vector<double> getTimePoints() const override { return { mTime }; }

    private:
        double mTime;
        std::vector<double> mValue;
    };

    class MonotonicCurveFit : public CurveFit {
    public:
        MonotonicCurveFit(std::vector<double> time, std::vector<std::vector<double>> y);
        void getPos(double t, double* v) override;
        void getPos(double t, float* v) override;
        double getPos(double t, int j) override;
        void getSlope(double t, double* v) override;
        double getSlope(double t, int j) override;
        std::vector<double> getTimePoints() const override { return mT; }
        // A periodic wave from "name(v0, v1, ...)" (KeyCycle's custom wave shapes).
        static std::shared_ptr<MonotonicCurveFit> buildWave(const std::string& configString);
        static std::shared_ptr<MonotonicCurveFit> buildWave(const std::vector<double>& values);

    private:
        static double interpolate(double h, double x, double y1, double y2, double t1, double t2);
        static double diff(double h, double x, double y1, double y2, double t1, double t2);
        std::vector<double> mT;
        std::vector<std::vector<double>> mY;
        std::vector<std::vector<double>> mTangent;
        bool mExtrapolate = true;
        std::vector<double> mSlopeTemp;
    };

    class LinearCurveFit : public CurveFit {
    public:
        LinearCurveFit(std::vector<double> time, std::vector<std::vector<double>> y);
        void getPos(double t, double* v) override;
        void getPos(double t, float* v) override;
        double getPos(double t, int j) override;
        void getSlope(double t, double* v) override;
        double getSlope(double t, int j) override;
        std::vector<double> getTimePoints() const override { return mT; }

    private:
        std::vector<double> mT;
        std::vector<std::vector<double>> mY;
        double mTotalLength = NAN;
        bool mExtrapolate = true;
        std::vector<double> mSlopeTemp;
    };

    class ArcCurveFit : public CurveFit {
    public:
        static constexpr int ARC_START_VERTICAL = 1;
        static constexpr int ARC_START_HORIZONTAL = 2;
        static constexpr int ARC_START_FLIP = 3;
        static constexpr int ARC_BELOW = 4;
        static constexpr int ARC_ABOVE = 5;
        static constexpr int ARC_START_LINEAR = 0;

        ArcCurveFit(const std::vector<int>& arcModes, std::vector<double> time, const std::vector<std::vector<double>>& y);
        void getPos(double t, double* v) override;
        void getPos(double t, float* v) override;
        void getSlope(double t, double* v) override;
        double getPos(double t, int j) override;
        double getSlope(double t, int j) override;
        std::vector<double> getTimePoints() const override { return mTime; }

    private:
        static constexpr int START_VERTICAL = 1;
        static constexpr int START_HORIZONTAL = 2;
        static constexpr int START_LINEAR = 3;
        static constexpr int DOWN_ARC = 4;
        static constexpr int UP_ARC = 5;

        struct Arc {
            Arc(int mode, double t1, double t2, double x1, double y1, double x2, double y2);
            void setPoint(double time);
            double getX() const { return mEllipseCenterX + mEllipseA * mTmpSinAngle; }
            double getY() const { return mEllipseCenterY + mEllipseB * mTmpCosAngle; }
            double getDX() const;
            double getDY() const;
            double getLinearX(double t) const;
            double getLinearY(double t) const;
            double getLinearDX(double) const { return mEllipseCenterX; }
            double getLinearDY(double) const { return mEllipseCenterY; }
            double lookup(double v) const;
            void buildTable(double x1, double y1, double x2, double y2);

            std::vector<double> mLut;
            double mArcDistance = 0;
            double mTime1 = 0;
            double mTime2 = 0;
            double mX1 = 0, mX2 = 0, mY1 = 0, mY2 = 0;
            double mOneOverDeltaTime = 0;
            double mEllipseA = 0;
            double mEllipseB = 0;
            double mEllipseCenterX = 0;
            double mEllipseCenterY = 0;
            double mArcVelocity = 0;
            double mTmpSinAngle = 0;
            double mTmpCosAngle = 0;
            bool mVertical = false;
            bool mLinear = false;
            static constexpr double EPSILON = 0.001;
        };

        std::vector<double> mTime;
        std::vector<Arc> mArcs;
        bool mExtrapolate = true;
    };

}
