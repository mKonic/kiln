#include "kiln/constraintlayout/core/motion/key/MotionKeyTrigger.h"

#include "kiln/constraintlayout/core/motion/MotionWidget.h"

#include <algorithm>
#include <cctype>

namespace kiln::constraintlayout::core::motion {

    using TT = TypedValues::TriggerType;

    int MotionKeyTrigger::getId(const std::string& name) {
        if (name == VIEW_TRANSITION_ON_CROSS) return TYPE_VIEW_TRANSITION_ON_CROSS;
        if (name == VIEW_TRANSITION_ON_POSITIVE_CROSS) return TYPE_VIEW_TRANSITION_ON_POSITIVE_CROSS;
        if (name == VIEW_TRANSITION_ON_NEGATIVE_CROSS) return TYPE_VIEW_TRANSITION_ON_NEGATIVE_CROSS;
        if (name == POST_LAYOUT) return TYPE_POST_LAYOUT;
        if (name == TRIGGER_SLACK) return TYPE_TRIGGER_SLACK;
        if (name == TRIGGER_COLLISION_VIEW) return TYPE_TRIGGER_COLLISION_VIEW;
        if (name == TRIGGER_COLLISION_ID) return TYPE_TRIGGER_COLLISION_ID;
        if (name == TRIGGER_ID) return TYPE_TRIGGER_ID;
        if (name == POSITIVE_CROSS) return TYPE_POSITIVE_CROSS;
        if (name == NEGATIVE_CROSS) return TYPE_NEGATIVE_CROSS;
        if (name == TRIGGER_RECEIVER) return TYPE_TRIGGER_RECEIVER;
        return -1;
    }

    MotionKey& MotionKeyTrigger::copy(const MotionKey& src) {
        MotionKey::copy(src);
        const auto& k = static_cast<const MotionKeyTrigger&>(src);
        mCurveFit = k.mCurveFit;
        mCross = k.mCross;
        mTriggerReceiver = k.mTriggerReceiver;
        mNegativeCross = k.mNegativeCross;
        mPositiveCross = k.mPositiveCross;
        mTriggerID = k.mTriggerID;
        mTriggerCollisionId = k.mTriggerCollisionId;
        mTriggerSlack = k.mTriggerSlack;
        mFireCrossReset = k.mFireCrossReset;
        mFireNegativeReset = k.mFireNegativeReset;
        mFirePositiveReset = k.mFirePositiveReset;
        mFireThreshold = k.mFireThreshold;
        mFireLastPos = k.mFireLastPos;
        mPostLayout = k.mPostLayout;
        mCollisionRect = k.mCollisionRect;
        mTargetRect = k.mTargetRect;
        return *this;
    }

    void MotionKeyTrigger::fireCustom(std::string str, MotionWidget& widget) {
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return s;
        };
        const bool callAll = str.size() == 1;
        if (!callAll) str = lower(str.substr(1));
        for (const std::string& name : mCustom.keys()) {
            if (callAll || std::regex_match(lower(name), std::regex(str))) {
                if (const CustomVariable* custom = mCustom.find(name)) custom->applyToWidget(widget);
            }
        }
    }

    bool MotionKeyTrigger::setValue(int type, int value) {
        switch (type) {
            case TT::TYPE_TRIGGER_RECEIVER: mTriggerReceiver = value; break;
            case TT::TYPE_TRIGGER_ID: mTriggerID = value; break;
            case TT::TYPE_TRIGGER_COLLISION_ID: mTriggerCollisionId = value; break;
            case TT::TYPE_VIEW_TRANSITION_ON_NEGATIVE_CROSS: mViewTransitionOnNegativeCross = value; break;
            case TT::TYPE_VIEW_TRANSITION_ON_POSITIVE_CROSS: mViewTransitionOnPositiveCross = value; break;
            case TT::TYPE_VIEW_TRANSITION_ON_CROSS: mViewTransitionOnCross = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    bool MotionKeyTrigger::setValue(int type, float value) {
        if (type == TT::TYPE_TRIGGER_SLACK) {
            mTriggerSlack = value;
            return true;
        }
        return MotionKey::setValue(type, value);
    }

    bool MotionKeyTrigger::setValue(int type, const std::string& value) {
        switch (type) {
            case TT::TYPE_CROSS: mCross = value; break;
            case TT::TYPE_NEGATIVE_CROSS: mNegativeCross = value; break;
            case TT::TYPE_POSITIVE_CROSS: mPositiveCross = value; break;
            default: return MotionKey::setValue(type, value);
        }
        return true;
    }

    bool MotionKeyTrigger::setValue(int type, bool value) {
        if (type == TT::TYPE_POST_LAYOUT) {
            mPostLayout = value;
            return true;
        }
        return MotionKey::setValue(type, value);
    }

}
