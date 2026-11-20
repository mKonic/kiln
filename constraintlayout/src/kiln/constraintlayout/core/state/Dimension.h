#pragma once

// Dimension (constraintlayout-core state): how a reference is sized (fixed, wrap, spread,
// parent, percent or ratio, with bounds), applied to its widget.

#include <climits>
#include <string>

namespace kiln::constraintlayout::core {
    class ConstraintWidget;
}

namespace kiln::constraintlayout::core::state {

    class State;

    class Dimension {
    public:
        // Java's sentinel objects for mInitialValue; NONE is Java's null (a fixed value).
        enum class Value { NONE, FIXED_DIMENSION, WRAP_DIMENSION, SPREAD_DIMENSION, PARENT_DIMENSION, PERCENT_DIMENSION, RATIO_DIMENSION };
        enum class Type { FIXED, WRAP, MATCH_PARENT, MATCH_CONSTRAINT };

        bool equalsFixedValue(int value) const { return mInitialValue == Value::NONE && mValue == value; }

        static Dimension createSuggested(int value) {
            Dimension dimension;
            dimension.suggested(value);
            return dimension;
        }
        static Dimension createSuggested(Value startValue) {
            Dimension dimension;
            dimension.suggested(startValue);
            return dimension;
        }
        static Dimension createFixed(int value) {
            Dimension dimension(Value::FIXED_DIMENSION);
            dimension.fixed(value);
            return dimension;
        }
        static Dimension createFixed(Value value) {
            Dimension dimension(Value::FIXED_DIMENSION);
            dimension.fixed(value);
            return dimension;
        }
        static Dimension createPercent(float value) {
            Dimension dimension(Value::PERCENT_DIMENSION);
            dimension.percent(value);
            return dimension;
        }
        static Dimension createParent() { return Dimension(Value::PARENT_DIMENSION); }
        static Dimension createWrap() { return Dimension(Value::WRAP_DIMENSION); }
        static Dimension createSpread() { return Dimension(Value::SPREAD_DIMENSION); }
        static Dimension createRatio(const std::string& ratio) {
            Dimension dimension(Value::RATIO_DIMENSION);
            dimension.ratio(ratio);
            return dimension;
        }

        Dimension& percent(float value) {
            mPercent = value;
            return *this;
        }
        Dimension& min(int value) {
            if (value >= 0) mMin = value;
            return *this;
        }
        Dimension& min(Value value) {
            if (value == Value::WRAP_DIMENSION) mMin = kWrapContent;
            return *this;
        }
        Dimension& max(int value) {
            if (mMax >= 0) mMax = value;
            return *this;
        }
        Dimension& max(Value value) {
            if (value == Value::WRAP_DIMENSION && mIsSuggested) {
                mInitialValue = Value::WRAP_DIMENSION;
                mMax = INT_MAX;
            }
            return *this;
        }
        Dimension& suggested(int value) {
            mIsSuggested = true;
            if (value >= 0) mMax = value;
            return *this;
        }
        Dimension& suggested(Value value) {
            mInitialValue = value;
            mIsSuggested = true;
            return *this;
        }
        Dimension& fixed(Value value) {
            mInitialValue = value;
            return *this;
        }
        Dimension& fixed(int value) {
            mInitialValue = Value::NONE;
            mValue = value;
            return *this;
        }
        Dimension& ratio(const std::string& ratio) {
            mRatioString = ratio;
            return *this;
        }
        void setValue(int value) {
            mIsSuggested = false;
            mInitialValue = Value::NONE;
            mValue = value;
        }
        int getValue() const { return mValue; }

        void apply(State& state, ConstraintWidget* constraintWidget, int orientation) const;

    private:
        Dimension() = default;
        explicit Dimension(Value type) : mInitialValue(type) {}

        static constexpr int kWrapContent = -2;
        int mMin = 0;
        int mMax = INT_MAX;
        float mPercent = 1.0f;
        int mValue = 0;
        std::string mRatioString;   // empty: Java's null
        Value mInitialValue = Value::WRAP_DIMENSION;
        bool mIsSuggested = false;
    };

}
