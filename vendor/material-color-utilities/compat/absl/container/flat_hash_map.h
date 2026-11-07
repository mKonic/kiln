#pragma once

// kiln's stand-in for absl::flat_hash_map in material-color-utilities (wsmeans.cc): the same
// interface from std::unordered_map.

#include <unordered_map>

namespace absl {
    template<typename K, typename V> using flat_hash_map = std::unordered_map<K, V>;
}
