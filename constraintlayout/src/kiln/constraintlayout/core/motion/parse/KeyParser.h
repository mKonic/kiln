#pragma once

// KeyParser (constraintlayout-core motion parse): reads a keyframe's attributes from JSON5
// into a TypedBundle, typed by the attribute's id.

#include "kiln/constraintlayout/core/motion/utils/TypedBundle.h"

#include <string>

namespace kiln::constraintlayout::core::motion {

    class KeyParser {
    public:
        static TypedBundle parseAttributes(const std::string& str);
    };

}
