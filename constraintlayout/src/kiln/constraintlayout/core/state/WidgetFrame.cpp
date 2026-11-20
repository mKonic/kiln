#include "kiln/constraintlayout/core/state/WidgetFrame.h"

#include "kiln/constraintlayout/core/parser/CLParser.h"
#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/LinearSystem.h"
#include "kiln/constraintlayout/core/state/Transition.h"
#include "kiln/constraintlayout/core/widgets/ConstraintWidget.h"

#include <cstdio>
#include <regex>

namespace kiln::constraintlayout::core {

    using motion::CustomVariable;
    using Custom = motion::TypedValues::Custom;

    void WidgetFrame::updateAttributes(const WidgetFrame& frame) {
        pivotX = frame.pivotX;
        pivotY = frame.pivotY;
        rotationX = frame.rotationX;
        rotationY = frame.rotationY;
        rotationZ = frame.rotationZ;
        translationX = frame.translationX;
        translationY = frame.translationY;
        translationZ = frame.translationZ;
        scaleX = frame.scaleX;
        scaleY = frame.scaleY;
        alpha = frame.alpha;
        visibility = frame.visibility;
        setMotionAttributes(frame.mMotionProperties);
        JavaHashMap<CustomVariable> custom;
        frame.mCustom.forEach([&](const std::string&, const CustomVariable& c) { custom.put(c.getName(), c.copy()); });
        mCustom = std::move(custom);
    }

    WidgetFrame& WidgetFrame::update() {
        if (widget != nullptr) {
            left = widget->getLeft();
            top = widget->getTop();
            right = widget->getRight();
            bottom = widget->getBottom();
            updateAttributes(widget->frame);
        }
        return *this;
    }

    void WidgetFrame::interpolate(int parentWidth, int parentHeight, WidgetFrame& frame, const WidgetFrame& start, const WidgetFrame& end,
                                  state::Transition& transition, float progress) {
        const int frameNumber = JavaInt(progress * 100);
        int startX = start.left;
        int startY = start.top;
        int endX = end.left;
        int endY = end.top;
        int startWidth = start.right - start.left;
        int startHeight = start.bottom - start.top;
        int endWidth = end.right - end.left;
        int endHeight = end.bottom - end.top;
        float progressPosition = progress;
        float startAlpha = start.alpha;
        float endAlpha = end.alpha;
        if (start.visibility == ConstraintWidget::GONE) {
            // On visibility gone, keep the same size to do an alpha to zero
            startX -= JavaInt(endWidth / 2.0f);
            startY -= JavaInt(endHeight / 2.0f);
            startWidth = endWidth;
            startHeight = endHeight;
            if (std::isnan(startAlpha)) startAlpha = 0.0f;
        }
        if (end.visibility == ConstraintWidget::GONE) {
            endX -= JavaInt(startWidth / 2.0f);
            endY -= JavaInt(startHeight / 2.0f);
            endWidth = startWidth;
            endHeight = startHeight;
            if (std::isnan(endAlpha)) endAlpha = 0.0f;
        }
        if (std::isnan(startAlpha) && !std::isnan(endAlpha)) startAlpha = 1.0f;
        if (!std::isnan(startAlpha) && std::isnan(endAlpha)) endAlpha = 1.0f;
        if (start.visibility == ConstraintWidget::INVISIBLE) startAlpha = 0.0f;
        if (end.visibility == ConstraintWidget::INVISIBLE) endAlpha = 0.0f;
        if (frame.widget != nullptr && transition.hasPositionKeyframes()) {
            const auto* firstPosition = transition.findPreviousPosition(frame.widget->stringId, frameNumber);
            const auto* lastPosition = transition.findNextPosition(frame.widget->stringId, frameNumber);
            if (firstPosition == lastPosition) lastPosition = nullptr;
            int interpolateStartFrame = 0;
            int interpolateEndFrame = 100;
            if (firstPosition != nullptr) {
                startX = JavaInt(firstPosition->mX * parentWidth);
                startY = JavaInt(firstPosition->mY * parentHeight);
                interpolateStartFrame = firstPosition->mFrame;
            }
            if (lastPosition != nullptr) {
                endX = JavaInt(lastPosition->mX * parentWidth);
                endY = JavaInt(lastPosition->mY * parentHeight);
                interpolateEndFrame = lastPosition->mFrame;
            }
            progressPosition = (progress * 100.0f - interpolateStartFrame) / float(interpolateEndFrame - interpolateStartFrame);
        }
        frame.widget = start.widget;
        frame.left = JavaInt(startX + progressPosition * (endX - startX));
        frame.top = JavaInt(startY + progressPosition * (endY - startY));
        const int width = JavaInt((1 - progress) * startWidth + (progress * endWidth));
        const int height = JavaInt((1 - progress) * startHeight + (progress * endHeight));
        frame.right = frame.left + width;
        frame.bottom = frame.top + height;
        frame.pivotX = interpolate(start.pivotX, end.pivotX, 0.5f, progress);
        frame.pivotY = interpolate(start.pivotY, end.pivotY, 0.5f, progress);
        frame.rotationX = interpolate(start.rotationX, end.rotationX, 0.0f, progress);
        frame.rotationY = interpolate(start.rotationY, end.rotationY, 0.0f, progress);
        frame.rotationZ = interpolate(start.rotationZ, end.rotationZ, 0.0f, progress);
        frame.scaleX = interpolate(start.scaleX, end.scaleX, 1.0f, progress);
        frame.scaleY = interpolate(start.scaleY, end.scaleY, 1.0f, progress);
        frame.translationX = interpolate(start.translationX, end.translationX, 0.0f, progress);
        frame.translationY = interpolate(start.translationY, end.translationY, 0.0f, progress);
        frame.translationZ = interpolate(start.translationZ, end.translationZ, 0.0f, progress);
        frame.alpha = interpolate(startAlpha, endAlpha, 1.0f, progress);
        const std::vector<std::string> keys = end.mCustom.keys();
        frame.mCustom.clear();
        for (const std::string& key : keys) {
            const motion::CustomVariable* startVariable = start.mCustom.find(key);
            if (startVariable == nullptr) continue;
            const motion::CustomVariable* endVariable = end.mCustom.find(key);
            motion::CustomVariable& interpolated = frame.mCustom.put(key, *startVariable);
            if (startVariable->numberOfInterpolatedValues() == 1) {
                interpolated.setValue(motion::CustomValue(
                    interpolate(startVariable->getValueToInterpolate(), endVariable->getValueToInterpolate(), 0.0f, progress)));
            } else {
                const int n = startVariable->numberOfInterpolatedValues();
                std::vector<float> startValues(static_cast<std::size_t>(n)), endValues(static_cast<std::size_t>(n));
                startVariable->getValuesToInterpolate(startValues.data());
                endVariable->getValuesToInterpolate(endValues.data());
                for (int i = 0; i < n; i++) {
                    startValues[std::size_t(i)] = interpolate(startValues[std::size_t(i)], endValues[std::size_t(i)], 0.0f, progress);
                    interpolated.setValue(startValues.data());
                }
            }
        }
    }

    float WidgetFrame::interpolate(float start, float end, float defaultValue, float progress) {
        const bool isStartUnset = std::isnan(start);
        const bool isEndUnset = std::isnan(end);
        if (isStartUnset && isEndUnset) return kNaN;
        if (isStartUnset) start = defaultValue;
        if (isEndUnset) end = defaultValue;
        return start + progress * (end - start);
    }

    void WidgetFrame::addCustomColor(const std::string& n, int color) { setCustomAttribute(n, Custom::TYPE_COLOR, color); }

    int WidgetFrame::getCustomColor(const std::string& n) const {
        if (const CustomVariable* c = mCustom.find(n)) return c->getColorValue();
        return int(0xFFFFAA88u);
    }

    void WidgetFrame::addCustomFloat(const std::string& n, float value) { setCustomAttribute(n, Custom::TYPE_FLOAT, value); }

    float WidgetFrame::getCustomFloat(const std::string& n) const {
        if (const CustomVariable* c = mCustom.find(n)) return c->getFloatValue();
        return kNaN;
    }

    void WidgetFrame::setCustomAttribute(const std::string& n, int type, float value) {
        if (CustomVariable* c = mCustom.find(n)) c->setFloatValue(value);
        else mCustom.put(n, CustomVariable(n, type, value));
    }
    void WidgetFrame::setCustomAttribute(const std::string& n, int type, int value) {
        if (CustomVariable* c = mCustom.find(n)) c->setIntValue(value);
        else mCustom.put(n, CustomVariable(n, type, value));
    }
    void WidgetFrame::setCustomAttribute(const std::string& n, int type, bool value) {
        if (CustomVariable* c = mCustom.find(n)) c->setBooleanValue(value);
        else mCustom.put(n, CustomVariable(n, type, value));
    }
    void WidgetFrame::setCustomAttribute(const std::string& n, int type, const std::string& value) {
        if (CustomVariable* c = mCustom.find(n)) c->setStringValue(value);
        else mCustom.put(n, CustomVariable(n, type, value));
    }

    bool WidgetFrame::setValue(const std::string& key, CLElement* value) {
        if (key == "pivotX") pivotX = value->getFloat();
        else if (key == "pivotY") pivotY = value->getFloat();
        else if (key == "rotationX") rotationX = value->getFloat();
        else if (key == "rotationY") rotationY = value->getFloat();
        else if (key == "rotationZ") rotationZ = value->getFloat();
        else if (key == "translationX") translationX = value->getFloat();
        else if (key == "translationY") translationY = value->getFloat();
        else if (key == "translationZ") translationZ = value->getFloat();
        else if (key == "scaleX") scaleX = value->getFloat();
        else if (key == "scaleY") scaleY = value->getFloat();
        else if (key == "alpha") alpha = value->getFloat();
        else if (key == "interpolatedPos") interpolatedPos = value->getFloat();
        else if (key == "phone_orientation") phone_orientation = value->getFloat();
        else if (key == "top") top = value->getInt();
        else if (key == "left") left = value->getInt();
        else if (key == "right") right = value->getInt();
        else if (key == "bottom") bottom = value->getInt();
        else if (key == "custom") parseCustom(value);
        else return false;
        return true;
    }

    std::string WidgetFrame::getId() const {
        if (widget == nullptr) return "unknown";
        return widget->stringId;
    }

    void WidgetFrame::parseCustom(CLElement* custom) {
        auto* obj = static_cast<CLObject*>(custom);
        const int n = obj->size();
        static const std::regex kColor("#[0-9a-fA-F]+");
        for (int i = 0; i < n; i++) {
            auto* k = static_cast<CLKey*>(obj->get(i));
            const std::string customName = k->content();
            CLElement* v = k->getValue();
            const std::string vStr = v->content();
            if (vStr.size() < 128 && std::regex_match(vStr, kColor)) {
                const int color = int(std::stoul(vStr.substr(1), nullptr, 16));
                setCustomAttribute(customName, Custom::TYPE_COLOR, color);
            } else if (v->is(CLElement::KIND_NUMBER)) {
                setCustomAttribute(customName, Custom::TYPE_FLOAT, v->getFloat());
            } else {
                setCustomAttribute(customName, Custom::TYPE_STRING, vStr);
            }
        }
    }

    namespace {
        std::string javaFloat(float v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", double(v));
            std::string s = buf;
            if (s.find_first_of(".e") == std::string::npos && s.find("inf") == std::string::npos) s += ".0";
            return s;
        }
        void add(std::string& s, const char* title, int value) { s += std::string(title) + ": " + std::to_string(value) + ",\n"; }
        void add(std::string& s, const char* title, float value) {
            if (std::isnan(value)) return;
            s += std::string(title) + ": " + javaFloat(value) + ",\n";
        }
        const char* typeName(ConstraintAnchor::Type t) {
            switch (t) {
                case ConstraintAnchor::Type::NONE: return "NONE";
                case ConstraintAnchor::Type::LEFT: return "LEFT";
                case ConstraintAnchor::Type::TOP: return "TOP";
                case ConstraintAnchor::Type::RIGHT: return "RIGHT";
                case ConstraintAnchor::Type::BOTTOM: return "BOTTOM";
                case ConstraintAnchor::Type::BASELINE: return "BASELINE";
                case ConstraintAnchor::Type::CENTER: return "CENTER";
                case ConstraintAnchor::Type::CENTER_X: return "CENTER_X";
                case ConstraintAnchor::Type::CENTER_Y: return "CENTER_Y";
            }
            return "NONE";
        }
    }

    std::string WidgetFrame::serialize(bool sendPhoneOrientation) const {
        std::string ret = "{\n";
        add(ret, "left", left);
        add(ret, "top", top);
        add(ret, "right", right);
        add(ret, "bottom", bottom);
        add(ret, "pivotX", pivotX);
        add(ret, "pivotY", pivotY);
        add(ret, "rotationX", rotationX);
        add(ret, "rotationY", rotationY);
        add(ret, "rotationZ", rotationZ);
        add(ret, "translationX", translationX);
        add(ret, "translationY", translationY);
        add(ret, "translationZ", translationZ);
        add(ret, "scaleX", scaleX);
        add(ret, "scaleY", scaleY);
        add(ret, "alpha", alpha);
        add(ret, "visibility", visibility);
        add(ret, "interpolatedPos", interpolatedPos);
        if (widget != nullptr) {
            for (auto side : { ConstraintAnchor::Type::NONE, ConstraintAnchor::Type::LEFT, ConstraintAnchor::Type::TOP, ConstraintAnchor::Type::RIGHT,
                               ConstraintAnchor::Type::BOTTOM, ConstraintAnchor::Type::BASELINE, ConstraintAnchor::Type::CENTER,
                               ConstraintAnchor::Type::CENTER_X, ConstraintAnchor::Type::CENTER_Y }) {
                ConstraintAnchor* anchor = widget->getAnchor(side);
                if (anchor == nullptr || anchor->mTarget == nullptr) continue;
                const std::string& str = anchor->mTarget->getOwner()->stringId;
                ret += std::string("Anchor") + typeName(side) + ": ['" + (str.empty() ? "#PARENT" : str) + "', '" + typeName(anchor->mTarget->getType()) +
                       "', '" + std::to_string(anchor->mMargin) + "'],\n";
            }
        }
        if (sendPhoneOrientation) {
            // Upstream adds it twice.
            add(ret, "phone_orientation", phone_orientation);
            add(ret, "phone_orientation", phone_orientation);
        }
        if (mCustom.size() != 0) {
            ret += "custom : {\n";
            mCustom.forEach([&](const std::string& s, const CustomVariable& value) {
                ret += s + ": ";
                switch (value.getType()) {
                    case Custom::TYPE_INT:
                        ret += std::to_string(value.getIntegerValue()) + ",\n";
                        break;
                    case Custom::TYPE_FLOAT:
                    case Custom::TYPE_DIMENSION:
                        ret += javaFloat(value.getFloatValue()) + ",\n";
                        break;
                    case Custom::TYPE_COLOR:
                        ret += "'" + CustomVariable::colorString(value.getIntegerValue()) + "',\n";
                        break;
                    case Custom::TYPE_STRING:
                        ret += "'" + value.getStringValue() + "',\n";
                        break;
                    case Custom::TYPE_BOOLEAN:
                        ret += std::string("'") + (value.getBooleanValue() ? "true" : "false") + "',\n";
                        break;
                }
            });
            ret += "}\n";
        }
        ret += "}\n";
        return ret;
    }

}
