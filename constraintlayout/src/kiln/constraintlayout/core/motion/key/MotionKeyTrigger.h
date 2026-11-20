#pragma once

// MotionKeyTrigger (constraintlayout-core motion key): methods to call when the progress
// crosses a frame position. Upstream's core version records the attributes and never fires.

#include "kiln/constraintlayout/core/motion/key/MotionKey.h"
#include "kiln/constraintlayout/core/motion/utils/MotionGeometry.h"

#include <cmath>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;

    class MotionKeyTrigger : public MotionKey {
    public:
        static constexpr const char* VIEW_TRANSITION_ON_CROSS = "viewTransitionOnCross";
        static constexpr const char* VIEW_TRANSITION_ON_POSITIVE_CROSS = "viewTransitionOnPositiveCross";
        static constexpr const char* VIEW_TRANSITION_ON_NEGATIVE_CROSS = "viewTransitionOnNegativeCross";
        static constexpr const char* POST_LAYOUT = "postLayout";
        static constexpr const char* TRIGGER_SLACK = "triggerSlack";
        static constexpr const char* TRIGGER_COLLISION_VIEW = "triggerCollisionView";
        static constexpr const char* TRIGGER_COLLISION_ID = "triggerCollisionId";
        static constexpr const char* TRIGGER_ID = "triggerID";
        static constexpr const char* POSITIVE_CROSS = "positiveCross";
        static constexpr const char* NEGATIVE_CROSS = "negativeCross";
        static constexpr const char* TRIGGER_RECEIVER = "triggerReceiver";
        static constexpr const char* CROSS = "CROSS";
        static constexpr int TYPE_VIEW_TRANSITION_ON_CROSS = 301;
        static constexpr int TYPE_VIEW_TRANSITION_ON_POSITIVE_CROSS = 302;
        static constexpr int TYPE_VIEW_TRANSITION_ON_NEGATIVE_CROSS = 303;
        static constexpr int TYPE_POST_LAYOUT = 304;
        static constexpr int TYPE_TRIGGER_SLACK = 305;
        static constexpr int TYPE_TRIGGER_COLLISION_VIEW = 306;
        static constexpr int TYPE_TRIGGER_COLLISION_ID = 307;
        static constexpr int TYPE_TRIGGER_ID = 308;
        static constexpr int TYPE_POSITIVE_CROSS = 309;
        static constexpr int TYPE_NEGATIVE_CROSS = 310;
        static constexpr int TYPE_TRIGGER_RECEIVER = 311;
        static constexpr int TYPE_CROSS = 312;
        static constexpr int KEY_TYPE = 5;

        MotionKeyTrigger() { mType = KEY_TYPE; }
        void getAttributeNames(JavaHashSet&) override {}
        void addValues(SplineMap&) override {}
        int getId(const std::string& name) override;
        MotionKey& copy(const MotionKey& src) override;
        std::shared_ptr<MotionKey> clone() const override {
            auto k = std::make_shared<MotionKeyTrigger>();
            k->copy(*this);
            return k;
        }
        void conditionallyFire(float, MotionWidget&) {}
        bool setValue(int type, int value) override;
        bool setValue(int type, float value) override;
        bool setValue(int type, const std::string& value) override;
        bool setValue(int type, bool value) override;

    private:
        void fireCustom(std::string str, MotionWidget& widget);

        int mCurveFit = -1;
        std::string mCross;
        int mTriggerReceiver = UNSET;
        std::string mNegativeCross;
        std::string mPositiveCross;
        int mTriggerID = UNSET;
        int mTriggerCollisionId = UNSET;
        float mTriggerSlack = .1f;
        bool mFireCrossReset = true;
        bool mFireNegativeReset = true;
        bool mFirePositiveReset = true;
        float mFireThreshold = NAN;
        float mFireLastPos = 0;
        bool mPostLayout = false;
        int mViewTransitionOnNegativeCross = UNSET;
        int mViewTransitionOnPositiveCross = UNSET;
        int mViewTransitionOnCross = UNSET;
        FloatRect mCollisionRect;
        FloatRect mTargetRect;
    };

}
