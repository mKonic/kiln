#pragma once

// MotionKey (constraintlayout-core motion key): a keyframe of a MotionLayout transition: at a
// frame position (0..100) for a target, with custom variables. KeyAttributes, KeyCycle,
// KeyTimeCycle, KeyPosition and KeyTrigger derive from it.

#include "kiln/constraintlayout/core/motion/CustomVariable.h"
#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/state/JavaHashMap.h"

#include <memory>
#include <optional>
#include <regex>
#include <string>

namespace kiln::constraintlayout::core::motion {

    class SplineSet;
    using SplineMap = JavaHashMap<std::shared_ptr<SplineSet>>;

    class MotionKey : public TypedValues {
    public:
        static constexpr int UNSET = -1;
        static constexpr const char* ALPHA = "alpha";
        static constexpr const char* ELEVATION = "elevation";
        static constexpr const char* ROTATION = "rotationZ";
        static constexpr const char* ROTATION_X = "rotationX";
        static constexpr const char* TRANSITION_PATH_ROTATE = "transitionPathRotate";
        static constexpr const char* SCALE_X = "scaleX";
        static constexpr const char* SCALE_Y = "scaleY";
        static constexpr const char* TRANSLATION_X = "translationX";
        static constexpr const char* TRANSLATION_Y = "translationY";
        static constexpr const char* CUSTOM = "CUSTOM";
        static constexpr const char* VISIBILITY = "visibility";

        virtual void getAttributeNames(JavaHashSet& attributes) = 0;
        virtual void addValues(SplineMap& splines) = 0;
        virtual void setInterpolation(JavaHashMap<int>&) {}
        virtual MotionKey& copy(const MotionKey& src) {
            mFramePosition = src.mFramePosition;
            mTargetId = src.mTargetId;
            mTargetString = src.mTargetString;
            mType = src.mType;
            return *this;
        }
        // Upstream's KeyAttributes and KeyCycle return null.
        virtual std::shared_ptr<MotionKey> clone() const = 0;
        MotionKey& setViewId(int id) {
            mTargetId = id;
            return *this;
        }
        void setFramePosition(int pos) { mFramePosition = pos; }
        int getFramePosition() const { return mFramePosition; }

        bool setValue(int type, int value) override {
            if (type == TYPE_FRAME_POSITION) {
                mFramePosition = value;
                return true;
            }
            return false;
        }
        bool setValue(int, float) override { return false; }
        bool setValue(int type, const std::string& value) override {
            if (type == TYPE_TARGET) {
                mTargetString = value;
                return true;
            }
            return false;
        }
        bool setValue(int, bool) override { return false; }

        void setCustomAttribute(const std::string& name, int type, float value) { mCustom.put(name, CustomVariable(name, type, value)); }
        void setCustomAttribute(const std::string& name, int type, int value) { mCustom.put(name, CustomVariable(name, type, value)); }
        void setCustomAttribute(const std::string& name, int type, bool value) { mCustom.put(name, CustomVariable(name, type, value)); }
        void setCustomAttribute(const std::string& name, int type, const std::string& value) { mCustom.put(name, CustomVariable(name, type, value)); }

        int mFramePosition = UNSET;
        int mType = 0;
        JavaHashMap<CustomVariable> mCustom;

    protected:
        bool matches(const std::optional<std::string>& constraintTag) const {
            if (!mTargetString || !constraintTag) return false;
            return std::regex_match(*constraintTag, std::regex(*mTargetString));
        }

        int mTargetId = UNSET;
        std::optional<std::string> mTargetString;
    };

}
