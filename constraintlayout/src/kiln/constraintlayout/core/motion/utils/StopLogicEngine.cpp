#include "kiln/constraintlayout/core/motion/utils/StopLogicEngine.h"

#include <cmath>
#include <cstdio>

namespace kiln::constraintlayout::core::motion {

    namespace {
        std::string f(float v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", double(v));
            std::string s = buf;
            if (s.find_first_of(".en") == std::string::npos) s += ".0";
            return s;
        }
    }

    std::string StopLogicEngine::debug(const std::string& desc, float time) {
        std::string ret = desc + " ===== " + mType + "\n";
        ret += desc + (mBackwards ? "backwards" : "forward ") + " time = " + f(time) + "  stages " + std::to_string(mNumberOfStages) + "\n";
        ret += desc + " dur " + f(mStage1Duration) + " vel " + f(mStage1Velocity) + " pos " + f(mStage1EndPosition) + "\n";
        if (mNumberOfStages > 1) ret += desc + " dur " + f(mStage2Duration) + " vel " + f(mStage2Velocity) + " pos " + f(mStage2EndPosition) + "\n";
        if (mNumberOfStages > 2) ret += desc + " dur " + f(mStage3Duration) + " vel " + f(mStage3Velocity) + " pos " + f(mStage3EndPosition) + "\n";
        if (time <= mStage1Duration) return ret + desc + "stage 0\n";
        if (mNumberOfStages == 1) return ret + desc + "end stage 0\n";
        time -= mStage1Duration;
        if (time < mStage2Duration) return ret + desc + " stage 1\n";
        if (mNumberOfStages == 2) return ret + desc + "end stage 1\n";
        time -= mStage2Duration;
        if (time < mStage3Duration) return ret + desc + " stage 2\n";
        return ret + desc + " end stage 2\n";
    }

    float StopLogicEngine::getVelocity(float x) {
        if (x <= mStage1Duration) return mStage1Velocity + (mStage2Velocity - mStage1Velocity) * x / mStage1Duration;
        if (mNumberOfStages == 1) return 0;
        x -= mStage1Duration;
        if (x < mStage2Duration) return mStage2Velocity + (mStage3Velocity - mStage2Velocity) * x / mStage2Duration;
        if (mNumberOfStages == 2) return 0;
        x -= mStage2Duration;
        if (x < mStage3Duration) return mStage3Velocity - mStage3Velocity * x / mStage3Duration;
        return 0;
    }

    float StopLogicEngine::calcY(float time) {
        mDone = false;
        if (time <= mStage1Duration) return mStage1Velocity * time + (mStage2Velocity - mStage1Velocity) * time * time / (2 * mStage1Duration);
        if (mNumberOfStages == 1) return mStage1EndPosition;
        time -= mStage1Duration;
        if (time < mStage2Duration)
            return mStage1EndPosition + mStage2Velocity * time + (mStage3Velocity - mStage2Velocity) * time * time / (2 * mStage2Duration);
        if (mNumberOfStages == 2) return mStage2EndPosition;
        time -= mStage2Duration;
        if (time <= mStage3Duration) return mStage2EndPosition + mStage3Velocity * time - mStage3Velocity * time * time / (2 * mStage3Duration);
        mDone = true;
        return mStage3EndPosition;
    }

    void StopLogicEngine::config(float currentPos, float destination, float currentVelocity, float maxTime, float maxAcceleration, float maxVelocity) {
        mDone = false;
        mStartPosition = currentPos;
        mBackwards = currentPos > destination;
        if (mBackwards) setup(-currentVelocity, currentPos - destination, maxAcceleration, maxVelocity, maxTime);
        else setup(currentVelocity, destination - currentPos, maxAcceleration, maxVelocity, maxTime);
    }

    float StopLogicEngine::getInterpolation(float v) {
        const float y = calcY(v);
        mLastPosition = y;
        mLastTime = v;
        return mBackwards ? mStartPosition - y : mStartPosition + y;
    }

    bool StopLogicEngine::isStopped() { return getVelocity() < EPSILON && std::abs(mStage3EndPosition - mLastPosition) < EPSILON; }

    void StopLogicEngine::setup(float velocity, float distance, float maxAcceleration, float maxVelocity, float maxTime) {
        mDone = false;
        mStage3EndPosition = distance;
        if (velocity == 0) velocity = 0.0001f;
        const float min_time_to_stop = velocity / maxAcceleration;
        const float stopDistance = min_time_to_stop * velocity / 2;
        if (velocity < 0) {
            const float timeToZeroVelocity = -velocity / maxAcceleration;
            const float reversDistanceTraveled = timeToZeroVelocity * velocity / 2;
            const float totalDistance = distance - reversDistanceTraveled;
            const float peak_v = float(std::sqrt(double(maxAcceleration * totalDistance)));
            if (peak_v < maxVelocity) {
                mType = "backward accelerate, decelerate";
                mNumberOfStages = 2;
                mStage1Velocity = velocity;
                mStage2Velocity = peak_v;
                mStage3Velocity = 0;
                mStage1Duration = (peak_v - velocity) / maxAcceleration;
                mStage2Duration = peak_v / maxAcceleration;
                mStage1EndPosition = (velocity + peak_v) * mStage1Duration / 2;
                mStage2EndPosition = distance;
                mStage3EndPosition = distance;
                return;
            }
            mType = "backward accelerate cruse decelerate";
            mNumberOfStages = 3;
            mStage1Velocity = velocity;
            mStage2Velocity = maxVelocity;
            mStage3Velocity = maxVelocity;
            mStage1Duration = (maxVelocity - velocity) / maxAcceleration;
            mStage3Duration = maxVelocity / maxAcceleration;
            const float accDist = (velocity + maxVelocity) * mStage1Duration / 2;
            const float decDist = (maxVelocity * mStage3Duration) / 2;
            mStage2Duration = (distance - accDist - decDist) / maxVelocity;
            mStage1EndPosition = accDist;
            mStage2EndPosition = distance - decDist;
            mStage3EndPosition = distance;
            return;
        }
        if (stopDistance >= distance) {
            mType = "hard stop";
            const float time = 2 * distance / velocity;
            mNumberOfStages = 1;
            mStage1Velocity = velocity;
            mStage2Velocity = 0;
            mStage1EndPosition = distance;
            mStage1Duration = time;
            return;
        }
        const float distance_before_break = distance - stopDistance;
        const float cruseTime = distance_before_break / velocity;
        if (cruseTime + min_time_to_stop < maxTime) {
            mType = "cruse decelerate";
            mNumberOfStages = 2;
            mStage1Velocity = velocity;
            mStage2Velocity = velocity;
            mStage3Velocity = 0;
            mStage1EndPosition = distance_before_break;
            mStage2EndPosition = distance;
            mStage1Duration = cruseTime;
            mStage2Duration = velocity / maxAcceleration;
            return;
        }
        const float peak_v = float(std::sqrt(double(maxAcceleration * distance + velocity * velocity / 2)));
        mStage1Duration = (peak_v - velocity) / maxAcceleration;
        mStage2Duration = peak_v / maxAcceleration;
        if (peak_v < maxVelocity) {
            mType = "accelerate decelerate";
            mNumberOfStages = 2;
            mStage1Velocity = velocity;
            mStage2Velocity = peak_v;
            mStage3Velocity = 0;
            mStage1Duration = (peak_v - velocity) / maxAcceleration;
            mStage2Duration = peak_v / maxAcceleration;
            mStage1EndPosition = (velocity + peak_v) * mStage1Duration / 2;
            mStage2EndPosition = distance;
            return;
        }
        mType = "accelerate cruse decelerate";
        mNumberOfStages = 3;
        mStage1Velocity = velocity;
        mStage2Velocity = maxVelocity;
        mStage3Velocity = maxVelocity;
        mStage1Duration = (maxVelocity - velocity) / maxAcceleration;
        mStage3Duration = maxVelocity / maxAcceleration;
        const float accDist = (velocity + maxVelocity) * mStage1Duration / 2;
        const float decDist = (maxVelocity * mStage3Duration) / 2;
        mStage2Duration = (distance - accDist - decDist) / maxVelocity;
        mStage1EndPosition = accDist;
        mStage2EndPosition = distance - decDist;
        mStage3EndPosition = distance;
    }

    std::string StopLogicEngine::Decelerate::debug(const std::string&, float) { return f(mDuration) + " " + f(mLastVelocity); }

}
