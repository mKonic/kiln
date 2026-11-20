#include "kiln/constraintlayout/core/motion/CustomVariable.h"

#include "kiln/constraintlayout/core/motion/MotionWidget.h"
#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/motion/utils/Utils.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace kiln::constraintlayout::core::motion {

    using Custom = TypedValues::Custom;

    namespace {
        int clamp(int c) { return MotionUtils::clamp(c); }
        int javaRound(float v) { return int(std::floor(v + 0.5f)); }
        int pack(int a, int r, int g, int b) { return int((unsigned(a) << 24) | (unsigned(r) << 16) | (unsigned(g) << 8) | unsigned(b)); }
        int hsv(float hue, float saturation, float value) {
            const int h = int(hue * 6);
            const float f = hue * 6 - h;
            const int p = int(0.5f + 255 * value * (1 - saturation));
            const int q = int(0.5f + 255 * value * (1 - f * saturation));
            const int t = int(0.5f + 255 * value * (1 - (1 - f) * saturation));
            const int v = int(0.5f + 255 * value);
            const unsigned opaque = 0xFF000000u;
            switch (h) {
                case 0: return int(opaque | unsigned((v << 16) + (t << 8) + p));
                case 1: return int(opaque | unsigned((q << 16) + (v << 8) + p));
                case 2: return int(opaque | unsigned((p << 16) + (v << 8) + t));
                case 3: return int(opaque | unsigned((p << 16) + (q << 8) + v));
                case 4: return int(opaque | unsigned((t << 16) + (p << 8) + v));
                case 5: return int(opaque | unsigned((v << 16) + (p << 8) + q));
                default: return 0;
            }
        }
        void colorToLinear(int color, float* ret) {
            const int a = 0xFF & (color >> 24);
            const int r = 0xFF & (color >> 16);
            const int g = 0xFF & (color >> 8);
            const int b = 0xFF & color;
            ret[0] = float(std::pow(r / 255.0f, 2.2));
            ret[1] = float(std::pow(g / 255.0f, 2.2));
            ret[2] = float(std::pow(b / 255.0f, 2.2));
            ret[3] = a / 255.0f;
        }
        std::string javaFloat(float v) {
            if (std::isnan(v)) return "NaN";
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", double(v));
            std::string s = buf;
            if (s.find_first_of(".e") == std::string::npos) s += ".0";
            return s;
        }
    }

    // CustomAttribute

    CustomAttribute::CustomAttribute(std::string name, AttributeType attributeType, const CustomValue& value, bool)
        : mName(std::move(name)), mType(attributeType) {
        setValue(value);
    }

    CustomAttribute::CustomAttribute(const CustomAttribute& source, const CustomValue& value) : mName(source.mName), mType(source.mType) {
        setValue(value);
    }

    bool CustomAttribute::isContinuous() const {
        switch (mType) {
            case AttributeType::REFERENCE_TYPE:
            case AttributeType::BOOLEAN_TYPE:
            case AttributeType::STRING_TYPE:
                return false;
            default:
                return true;
        }
    }

    int CustomAttribute::numberOfInterpolatedValues() const {
        switch (mType) {
            case AttributeType::COLOR_TYPE:
            case AttributeType::COLOR_DRAWABLE_TYPE:
                return 4;
            default:
                return 1;
        }
    }

    float CustomAttribute::getValueToInterpolate() const {
        switch (mType) {
            case AttributeType::INT_TYPE:
                return float(mIntegerValue);
            case AttributeType::FLOAT_TYPE:
                return mFloatValue;
            case AttributeType::COLOR_TYPE:
            case AttributeType::COLOR_DRAWABLE_TYPE:
                throw std::runtime_error("Color does not have a single color to interpolate");
            case AttributeType::STRING_TYPE:
                throw std::runtime_error("Cannot interpolate String");
            case AttributeType::BOOLEAN_TYPE:
                return mBooleanValue ? 1 : 0;
            case AttributeType::DIMENSION_TYPE:
                return mFloatValue;
            default:
                return NAN;
        }
    }

    void CustomAttribute::getValuesToInterpolate(float* ret) const {
        switch (mType) {
            case AttributeType::INT_TYPE:
                ret[0] = float(mIntegerValue);
                break;
            case AttributeType::FLOAT_TYPE:
                ret[0] = mFloatValue;
                break;
            case AttributeType::COLOR_DRAWABLE_TYPE:
            case AttributeType::COLOR_TYPE:
                colorToLinear(mColorValue, ret);
                break;
            case AttributeType::STRING_TYPE:
                throw std::runtime_error("Color does not have a single color to interpolate");
            case AttributeType::BOOLEAN_TYPE:
                ret[0] = mBooleanValue ? 1 : 0;
                break;
            case AttributeType::DIMENSION_TYPE:
                ret[0] = mFloatValue;
                break;
            default:
                break;
        }
    }

    void CustomAttribute::setValue(const float* value) {
        switch (mType) {
            case AttributeType::REFERENCE_TYPE:
            case AttributeType::INT_TYPE:
                mIntegerValue = int(value[0]);
                break;
            case AttributeType::FLOAT_TYPE:
                mFloatValue = value[0];
                break;
            case AttributeType::COLOR_DRAWABLE_TYPE:
            case AttributeType::COLOR_TYPE:
                mColorValue = hsvToRgb(value[0], value[1], value[2]);
                mColorValue = int((unsigned(mColorValue) & 0xFFFFFFu) | (unsigned(clamp(int(0xFF * value[3]))) << 24));
                break;
            case AttributeType::STRING_TYPE:
                throw std::runtime_error("Color does not have a single color to interpolate");
            case AttributeType::BOOLEAN_TYPE:
                mBooleanValue = value[0] > 0.5;
                break;
            case AttributeType::DIMENSION_TYPE:
                mFloatValue = value[0];
                break;
            default:
                break;
        }
    }

    int CustomAttribute::hsvToRgb(float hue, float saturation, float value) { return hsv(hue, saturation, value); }

    bool CustomAttribute::diff(const CustomAttribute* c) const {
        if (c == nullptr || mType != c->mType) return false;
        switch (mType) {
            case AttributeType::INT_TYPE:
            case AttributeType::REFERENCE_TYPE:
                return mIntegerValue == c->mIntegerValue;
            case AttributeType::FLOAT_TYPE:
                return mFloatValue == c->mFloatValue;
            case AttributeType::COLOR_TYPE:
            case AttributeType::COLOR_DRAWABLE_TYPE:
                return mColorValue == c->mColorValue;
            case AttributeType::STRING_TYPE:
                return mIntegerValue == c->mIntegerValue;
            case AttributeType::BOOLEAN_TYPE:
                return mBooleanValue == c->mBooleanValue;
            case AttributeType::DIMENSION_TYPE:
                return mFloatValue == c->mFloatValue;
            default:
                return false;
        }
    }

    void CustomAttribute::setValue(const CustomValue& value) {
        switch (mType) {
            case AttributeType::REFERENCE_TYPE:
            case AttributeType::INT_TYPE:
                mIntegerValue = std::get<int>(value);
                break;
            case AttributeType::FLOAT_TYPE:
                mFloatValue = std::get<float>(value);
                break;
            case AttributeType::COLOR_TYPE:
            case AttributeType::COLOR_DRAWABLE_TYPE:
                mColorValue = std::get<int>(value);
                break;
            case AttributeType::STRING_TYPE:
                mStringValue = std::get<std::string>(value);
                break;
            case AttributeType::BOOLEAN_TYPE:
                mBooleanValue = std::get<bool>(value);
                break;
            case AttributeType::DIMENSION_TYPE:
                mFloatValue = std::get<float>(value);
                break;
            default:
                break;
        }
    }

    // CustomVariable

    CustomVariable::CustomVariable(std::string name, int type, const std::string& value) : mName(std::move(name)), mType(type), mStringValue(value) {}

    CustomVariable::CustomVariable(std::string name, int type, int value) : mName(std::move(name)), mType(type) {
        if (type == Custom::TYPE_FLOAT) mFloatValue = float(value);
        else mIntegerValue = value;
    }

    CustomVariable::CustomVariable(std::string name, int type, float value) : mName(std::move(name)), mType(type), mFloatValue(value) {}
    CustomVariable::CustomVariable(std::string name, int type, bool value) : mName(std::move(name)), mBooleanValue(value), mType(type) {}
    CustomVariable::CustomVariable(std::string name, int attributeType) : mName(std::move(name)), mType(attributeType) {}

    CustomVariable::CustomVariable(std::string name, int attributeType, const CustomValue& value) : mName(std::move(name)), mType(attributeType) {
        setValue(value);
    }

    CustomVariable::CustomVariable(const CustomVariable& source, const CustomValue& value) : mName(source.mName), mType(source.mType) { setValue(value); }

    std::string CustomVariable::colorString(int v) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%08x", unsigned(v));
        return std::string("#") + buf;
    }

    std::string CustomVariable::toString() const {
        const std::string str = mName + ':';
        switch (mType) {
            case Custom::TYPE_INT: return str + std::to_string(mIntegerValue);
            case Custom::TYPE_FLOAT: return str + javaFloat(mFloatValue);
            case Custom::TYPE_COLOR: return str + colorString(mIntegerValue);
            case Custom::TYPE_STRING: return str + (mStringValue.empty() ? "null" : mStringValue);
            case Custom::TYPE_BOOLEAN: return str + (mBooleanValue ? "true" : "false");
            case Custom::TYPE_DIMENSION: return str + javaFloat(mFloatValue);
        }
        return str + "????";
    }

    bool CustomVariable::isContinuous() const {
        switch (mType) {
            case Custom::TYPE_REFERENCE:
            case Custom::TYPE_BOOLEAN:
            case Custom::TYPE_STRING:
                return false;
            default:
                return true;
        }
    }

    int CustomVariable::numberOfInterpolatedValues() const { return mType == Custom::TYPE_COLOR ? 4 : 1; }

    float CustomVariable::getValueToInterpolate() const {
        switch (mType) {
            case Custom::TYPE_INT: return float(mIntegerValue);
            case Custom::TYPE_FLOAT: return mFloatValue;
            case Custom::TYPE_COLOR: throw std::runtime_error("Color does not have a single color to interpolate");
            case Custom::TYPE_STRING: throw std::runtime_error("Cannot interpolate String");
            case Custom::TYPE_BOOLEAN: return mBooleanValue ? 1 : 0;
            case Custom::TYPE_DIMENSION: return mFloatValue;
        }
        return NAN;
    }

    void CustomVariable::getValuesToInterpolate(float* ret) const {
        switch (mType) {
            case Custom::TYPE_INT:
                ret[0] = float(mIntegerValue);
                break;
            case Custom::TYPE_FLOAT:
                ret[0] = mFloatValue;
                break;
            case Custom::TYPE_COLOR:
                colorToLinear(mIntegerValue, ret);
                break;
            case Custom::TYPE_STRING:
                throw std::runtime_error("Cannot interpolate String");
            case Custom::TYPE_BOOLEAN:
                ret[0] = mBooleanValue ? 1 : 0;
                break;
            case Custom::TYPE_DIMENSION:
                ret[0] = mFloatValue;
                break;
        }
    }

    void CustomVariable::setValue(const float* value) {
        switch (mType) {
            case Custom::TYPE_REFERENCE:
            case Custom::TYPE_INT:
                mIntegerValue = int(value[0]);
                break;
            case Custom::TYPE_FLOAT:
            case Custom::TYPE_DIMENSION:
                mFloatValue = value[0];
                break;
            case Custom::TYPE_COLOR: {
                const int r = 0xFF & javaRound(float(std::pow(value[0], 1.0 / 2.0)) * 255.0f);
                const int g = 0xFF & javaRound(float(std::pow(value[1], 1.0 / 2.0)) * 255.0f);
                const int b = 0xFF & javaRound(float(std::pow(value[2], 1.0 / 2.0)) * 255.0f);
                const int a = 0xFF & javaRound(value[3] * 255.0f);
                mIntegerValue = pack(a, r, g, b);
                break;
            }
            case Custom::TYPE_STRING:
                throw std::runtime_error("Cannot interpolate String");
            case Custom::TYPE_BOOLEAN:
                mBooleanValue = value[0] > 0.5;
                break;
        }
    }

    int CustomVariable::hsvToRgb(float hue, float saturation, float value) { return hsv(hue, saturation, value); }

    bool CustomVariable::diff(const CustomVariable* c) const {
        if (c == nullptr || mType != c->mType) return false;
        switch (mType) {
            case Custom::TYPE_INT:
            case Custom::TYPE_REFERENCE:
                return mIntegerValue == c->mIntegerValue;
            case Custom::TYPE_FLOAT:
                return mFloatValue == c->mFloatValue;
            case Custom::TYPE_COLOR:
                return mIntegerValue == c->mIntegerValue;
            case Custom::TYPE_STRING:
                return mIntegerValue == c->mIntegerValue;
            case Custom::TYPE_BOOLEAN:
                return mBooleanValue == c->mBooleanValue;
            case Custom::TYPE_DIMENSION:
                return mFloatValue == c->mFloatValue;
        }
        return false;
    }

    void CustomVariable::setValue(const CustomValue& value) {
        switch (mType) {
            case Custom::TYPE_REFERENCE:
            case Custom::TYPE_INT:
            case Custom::TYPE_COLOR:
                mIntegerValue = std::get<int>(value);
                break;
            case Custom::TYPE_FLOAT:
            case Custom::TYPE_DIMENSION:
                mFloatValue = std::get<float>(value);
                break;
            case Custom::TYPE_STRING:
                mStringValue = std::get<std::string>(value);
                break;
            case Custom::TYPE_BOOLEAN:
                mBooleanValue = std::get<bool>(value);
                break;
        }
    }

    int CustomVariable::getInterpolatedColor(const float* value) { return MotionUtils::getInterpolatedColor(value); }

    void CustomVariable::setInterpolatedValue(MotionWidget& view, const float* value) const {
        switch (mType) {
            case Custom::TYPE_INT:
                view.setCustomAttribute(mName, mType, int(value[0]));
                break;
            case Custom::TYPE_COLOR:
                view.setCustomAttribute(mName, mType, MotionUtils::getInterpolatedColor(value));
                break;
            case Custom::TYPE_REFERENCE:
            case Custom::TYPE_STRING:
                throw std::runtime_error("unable to interpolate " + mName);
            case Custom::TYPE_BOOLEAN:
                view.setCustomAttribute(mName, mType, value[0] > 0.5f);
                break;
            case Custom::TYPE_DIMENSION:
            case Custom::TYPE_FLOAT:
                view.setCustomAttribute(mName, mType, value[0]);
                break;
        }
    }

    int CustomVariable::rgbaTocColor(float r, float g, float b, float a) { return MotionUtils::rgbaTocColor(r, g, b, a); }

    void CustomVariable::applyToWidget(MotionWidget& view) const {
        switch (mType) {
            case Custom::TYPE_INT:
            case Custom::TYPE_COLOR:
            case Custom::TYPE_REFERENCE:
                view.setCustomAttribute(mName, mType, mIntegerValue);
                break;
            case Custom::TYPE_STRING:
                view.setCustomAttribute(mName, mType, mStringValue);
                break;
            case Custom::TYPE_BOOLEAN:
                view.setCustomAttribute(mName, mType, mBooleanValue);
                break;
            case Custom::TYPE_DIMENSION:
            case Custom::TYPE_FLOAT:
                view.setCustomAttribute(mName, mType, mFloatValue);
                break;
        }
    }

}
