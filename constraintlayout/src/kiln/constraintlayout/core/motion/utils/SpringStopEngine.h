#pragma once

// SpringStopEngine (constraintlayout-core motion utils): a spring settling toward a target,
// integrated with midpoint steps, optionally bouncing off 0 and 1.

#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"

#include <cmath>

namespace kiln::constraintlayout::core::motion {

    class SpringStopEngine : public StopEngine {
    public:
        std::string debug(const std::string&, float) override { return {}; }
        void springConfig(float currentPos, float target, float currentVelocity, float mass, float stiffness, float damping, float stopThreshold,
                          int boundaryMode) {
            mTargetPos = target;
            mDamping = damping;
            mInitialized = false;
            mPos = currentPos;
            mLastVelocity = currentVelocity;
            mStiffness = stiffness;
            mMass = mass;
            mStopThreshold = stopThreshold;
            mBoundaryMode = boundaryMode;
            mLastTime = 0;
        }
        float getVelocity(float) override { return mV; }
        float getInterpolation(float time) override {
            compute(time - mLastTime);
            mLastTime = time;
            if (isStopped()) mPos = float(mTargetPos);
            return mPos;
        }
        float getAcceleration() const {
            const double k = mStiffness;
            const double c = mDamping;
            const double x = mPos - mTargetPos;
            return float(-k * x - c * mV) / mMass;
        }
        float getVelocity() override { return 0; }
        bool isStopped() override {
            const double x = mPos - mTargetPos;
            const double k = mStiffness;
            const double v = mV;
            const double m = mMass;
            const double energy = v * v * m + k * x * x;
            const double max_def = std::sqrt(energy / k);
            return max_def <= mStopThreshold;
        }

    private:
        void compute(double dt) {
            if (dt <= 0) return;
            const double k = mStiffness;
            const double c = mDamping;
            const int overSample = int(1 + 9 / (std::sqrt(mStiffness / mMass) * dt * 4));
            dt /= overSample;
            for (int i = 0; i < overSample; i++) {
                const double x = mPos - mTargetPos;
                double a = (-k * x - c * mV) / mMass;
                double avgV = mV + a * dt / 2;
                const double avgX = mPos + dt * avgV / 2 - mTargetPos;
                a = (-avgX * k - avgV * c) / mMass;
                const double dv = a * dt;
                avgV = mV + dv / 2;
                mV += float(dv);
                mPos += float(avgV * dt);
                if (mBoundaryMode > 0) {
                    if (mPos < 0 && ((mBoundaryMode & 1) == 1)) {
                        mPos = -mPos;
                        mV = -mV;
                    }
                    if (mPos > 1 && ((mBoundaryMode & 2) == 2)) {
                        mPos = 2 - mPos;
                        mV = -mV;
                    }
                }
            }
        }

        double mDamping = 0.5f;
        bool mInitialized = false;
        double mStiffness = 0;
        double mTargetPos = 0;
        double mLastVelocity = 0;
        float mLastTime = 0;
        float mPos = 0;
        float mV = 0;
        float mMass = 0;
        float mStopThreshold = 0;
        int mBoundaryMode = 0;
    };

}
