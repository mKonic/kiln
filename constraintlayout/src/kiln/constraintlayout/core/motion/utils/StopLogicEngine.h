#pragma once

// StopLogicEngine (constraintlayout-core motion utils): how a released swipe reaches its end:
// up to three stages (accelerate, cruise, decelerate) under a maximum acceleration and
// velocity; Decelerate is a plain deceleration to the destination.

#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"

#include <string>

namespace kiln::constraintlayout::core::motion {

    class StopLogicEngine : public StopEngine {
    public:
        std::string debug(const std::string& desc, float time) override;
        float getVelocity(float x) override;
        void config(float currentPos, float destination, float currentVelocity, float maxTime, float maxAcceleration, float maxVelocity);
        float getInterpolation(float v) override;
        float getVelocity() override { return mBackwards ? -getVelocity(mLastTime) : getVelocity(mLastTime); }
        bool isStopped() override;

        class Decelerate : public StopEngine {
        public:
            std::string debug(const std::string& desc, float time) override;
            float getVelocity(float time) override {
                if (time > mDuration) return 0;
                return mLastVelocity = mInitialVelocity + mAcceleration * time;
            }
            float getInterpolation(float time) override {
                if (time > mDuration) {
                    mDone = true;
                    return mDestination;
                }
                getVelocity(time);
                return mInitialPos + (mInitialVelocity + mAcceleration * time / 2) * time;
            }
            float getVelocity() override { return mLastVelocity; }
            bool isStopped() override { return mDone; }
            void config(float currentPos, float destination, float currentVelocity) {
                mDone = false;
                mDestination = destination;
                mInitialVelocity = currentVelocity;
                mInitialPos = currentPos;
                const float distance = mDestination - currentPos;
                mDuration = distance / (currentVelocity / 2);
                mAcceleration = -currentVelocity / mDuration;
            }

        private:
            float mDestination = 0;
            float mInitialVelocity = 0;
            float mAcceleration = 0;
            float mLastVelocity = 0;
            float mDuration = 0;
            float mInitialPos = 0;
            bool mDone = false;
        };

    private:
        float calcY(float time);
        void setup(float velocity, float distance, float maxAcceleration, float maxVelocity, float maxTime);

        float mStage1Velocity = 0, mStage2Velocity = 0, mStage3Velocity = 0;
        float mStage1Duration = 0, mStage2Duration = 0, mStage3Duration = 0;
        float mStage1EndPosition = 0, mStage2EndPosition = 0, mStage3EndPosition = 0;
        int mNumberOfStages = 0;
        std::string mType;
        bool mBackwards = false;
        float mStartPosition = 0;
        float mLastPosition = 0;
        float mLastTime = 0;
        bool mDone = false;
        static constexpr float EPSILON = 0.00001f;
    };

}
