#pragma once

// KeyFrameArray (constraintlayout-core motion utils): values at keyframe positions 0..100,
// kept by position: custom attributes, custom variables and float arrays.

#include "kiln/constraintlayout/core/motion/CustomVariable.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    template<typename T>
    class KeyFrameSlots {
    public:
        KeyFrameSlots() { clear(); }
        void clear() {
            mKeys.fill(EMPTY);
            for (auto& v : mValues) v.reset();
            mCount = 0;
        }
        int size() const { return mCount; }
        T& valueAt(int i) { return *mValues[std::size_t(mKeys[std::size_t(i)])]; }
        int keyAt(int i) const { return mKeys[std::size_t(i)]; }
        void append(int position, T value) {
            if (mValues[std::size_t(position)]) remove(position);
            mValues[std::size_t(position)] = std::move(value);
            mKeys[std::size_t(mCount++)] = position;
            std::sort(mKeys.begin(), mKeys.end());
        }
        void remove(int position) {
            mValues[std::size_t(position)].reset();
            for (int j = 0, i = 0; i < mCount; i++) {
                if (position == mKeys[std::size_t(i)]) {
                    mKeys[std::size_t(i)] = EMPTY;
                    j++;
                }
                if (i != j) mKeys[std::size_t(i)] = mKeys[std::size_t(j)];
                j++;
            }
            mCount--;
        }

    private:
        static constexpr int EMPTY = 999;
        std::array<int, 101> mKeys{};
        std::array<std::optional<T>, 101> mValues;
        int mCount = 0;
    };

    struct KeyFrameArray {
        using CustomArray = KeyFrameSlots<CustomAttribute>;
        using CustomVar = KeyFrameSlots<CustomVariable>;
        using FloatArray = KeyFrameSlots<std::vector<float>>;
    };

}
