#pragma once

// TypedBundle (constraintlayout-core motion utils): typed values keyed by TypedValues ids, in
// the order they were added, applied to a TypedValues object (or another bundle).

#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"

#include <string>
#include <utility>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class TypedBundle {
    public:
        int getInteger(int type) const {
            for (const auto& [t, v] : mInts)
                if (t == type) return v;
            return -1;
        }
        void add(int type, int value) { mInts.emplace_back(type, value); }
        void add(int type, float value) { mFloats.emplace_back(type, value); }
        void add(int type, const std::string& value) { mStrings.emplace_back(type, value); }
        void add(int type, const char* value) { add(type, std::string(value)); }
        void add(int type, bool value) { mBooleans.emplace_back(type, value); }
        // Java's addIfNotNull: an empty string is null here.
        void addIfNotNull(int type, const std::string& value) {
            if (!value.empty()) add(type, value);
        }
        void applyDelta(TypedValues& values) const {
            for (const auto& [t, v] : mInts) values.setValue(t, v);
            for (const auto& [t, v] : mFloats) values.setValue(t, v);
            for (const auto& [t, v] : mStrings) values.setValue(t, v);
            for (const auto& [t, v] : mBooleans) values.setValue(t, v);
        }
        void applyDelta(TypedBundle& values) const {
            for (const auto& [t, v] : mInts) values.add(t, v);
            for (const auto& [t, v] : mFloats) values.add(t, v);
            for (const auto& [t, v] : mStrings) values.add(t, v);
            for (const auto& [t, v] : mBooleans) values.add(t, v);
        }
        void clear() {
            mInts.clear();
            mFloats.clear();
            mStrings.clear();
            mBooleans.clear();
        }
        std::string toString() const {
            return "TypedBundle{mCountInt=" + std::to_string(mInts.size()) + ", mCountFloat=" + std::to_string(mFloats.size()) +
                   ", mCountString=" + std::to_string(mStrings.size()) + ", mCountBoolean=" + std::to_string(mBooleans.size()) + "}";
        }

    private:
        std::vector<std::pair<int, int>> mInts;
        std::vector<std::pair<int, float>> mFloats;
        std::vector<std::pair<int, std::string>> mStrings;
        std::vector<std::pair<int, bool>> mBooleans;
    };

}
