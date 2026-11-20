#pragma once

// CustomAttribute and CustomVariable (constraintlayout-core motion): a named custom value a
// MotionLayout interpolates (int, float, colour, string, boolean, dimension, reference).
// Colours interpolate as linear RGB with alpha. Java's Object setters take a CustomValue; a
// value of the wrong kind throws, as Java's cast does.

#include <climits>
#include <cmath>
#include <string>
#include <variant>

namespace kiln::constraintlayout::core::motion {

    class MotionWidget;
    using CustomValue = std::variant<int, float, bool, std::string>;

    class CustomAttribute {
    public:
        enum class AttributeType { INT_TYPE, FLOAT_TYPE, COLOR_TYPE, COLOR_DRAWABLE_TYPE, STRING_TYPE, BOOLEAN_TYPE, DIMENSION_TYPE, REFERENCE_TYPE };

        CustomAttribute(std::string name, AttributeType attributeType) : mName(std::move(name)), mType(attributeType) {}
        CustomAttribute(std::string name, AttributeType attributeType, const CustomValue& value, bool method);
        CustomAttribute(const CustomAttribute& source, const CustomValue& value);

        AttributeType getType() const { return mType; }
        bool isContinuous() const;
        void setFloatValue(float value) { mFloatValue = value; }
        void setColorValue(int value) { mColorValue = value; }
        void setIntValue(int value) { mIntegerValue = value; }
        void setStringValue(const std::string& value) { mStringValue = value; }
        int numberOfInterpolatedValues() const;
        float getValueToInterpolate() const;
        void getValuesToInterpolate(float* ret) const;
        void setValue(const float* value);
        void setValue(const CustomValue& value);
        static int hsvToRgb(float hue, float saturation, float value);
        bool diff(const CustomAttribute* customAttribute) const;

        std::string mName;
        bool mBooleanValue = false;

    private:
        AttributeType mType;
        int mIntegerValue = 0;
        float mFloatValue = 0;
        std::string mStringValue;
        int mColorValue = 0;
    };

    class CustomVariable {
    public:
        CustomVariable(std::string name, int type, const std::string& value);
        CustomVariable(std::string name, int type, const char* value) : CustomVariable(std::move(name), type, std::string(value)) {}
        CustomVariable(std::string name, int type, int value);
        CustomVariable(std::string name, int type, float value);
        CustomVariable(std::string name, int type, bool value);
        CustomVariable(std::string name, int attributeType);
        CustomVariable(std::string name, int attributeType, const CustomValue& value);
        CustomVariable(const CustomVariable& source, const CustomValue& value);
        CustomVariable(const CustomVariable&) = default;
        CustomVariable& operator=(const CustomVariable&) = default;
        CustomVariable copy() const { return *this; }

        static std::string colorString(int v);
        std::string toString() const;
        int getType() const { return mType; }
        bool getBooleanValue() const { return mBooleanValue; }
        float getFloatValue() const { return mFloatValue; }
        int getColorValue() const { return mIntegerValue; }
        int getIntegerValue() const { return mIntegerValue; }
        const std::string& getStringValue() const { return mStringValue; }
        bool isContinuous() const;
        void setFloatValue(float value) { mFloatValue = value; }
        void setBooleanValue(bool value) { mBooleanValue = value; }
        void setIntValue(int value) { mIntegerValue = value; }
        void setStringValue(const std::string& value) { mStringValue = value; }
        int numberOfInterpolatedValues() const;
        float getValueToInterpolate() const;
        void getValuesToInterpolate(float* ret) const;
        void setValue(const float* value);
        void setValue(const CustomValue& value);
        static int hsvToRgb(float hue, float saturation, float value);
        bool diff(const CustomVariable* customAttribute) const;
        static int getInterpolatedColor(const float* value);
        void setInterpolatedValue(MotionWidget& view, const float* value) const;
        static int rgbaTocColor(float r, float g, float b, float a);
        void applyToWidget(MotionWidget& view) const;
        const std::string& getName() const { return mName; }

        std::string mName;
        bool mBooleanValue = false;

    private:
        int mType = 0;
        int mIntegerValue = INT_MIN;
        float mFloatValue = NAN;
        std::string mStringValue;   // empty: Java's null
    };

}
