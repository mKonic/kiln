#pragma once

// KeyCache (constraintlayout-core motion utils): float values per view, attribute and element,
// which cycles share (KeyTimeCycle).

#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln::constraintlayout::core::motion {

    class KeyCache {
    public:
        void setFloatValue(const void* view, const std::string& type, int element, float value) {
            std::vector<float>& v = mMap[view][type];
            if (int(v.size()) <= element) v.resize(std::size_t(element) + 1, 0.0f);
            v[std::size_t(element)] = value;
        }
        float getFloatValue(const void* view, const std::string& type, int element) const {
            const auto view_it = mMap.find(view);
            if (view_it == mMap.end()) return NAN;
            const auto type_it = view_it->second.find(type);
            if (type_it == view_it->second.end()) return NAN;
            if (int(type_it->second.size()) > element) return type_it->second[std::size_t(element)];
            return NAN;
        }

    private:
        std::unordered_map<const void*, std::unordered_map<std::string, std::vector<float>>> mMap;
    };

}
