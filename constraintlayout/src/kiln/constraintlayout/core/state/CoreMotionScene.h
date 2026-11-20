#pragma once

// CoreMotionScene (constraintlayout-core state): where a parsed MotionScene's constraint sets
// and transitions (JSON5 strings) are kept, by name.

#include <optional>
#include <string>

namespace kiln::constraintlayout::core::state {

    class CoreMotionScene {
    public:
        virtual ~CoreMotionScene() = default;
        virtual void setTransitionContent(const std::string& elementName, const std::string& toJSON) = 0;
        virtual std::optional<std::string> getConstraintSet(const std::string& ext) = 0;
        virtual void setConstraintSetContent(const std::string& csName, const std::string& toJSON) = 0;
        virtual void setDebugName(const std::string& name) = 0;
        virtual std::optional<std::string> getTransition(const std::string& str) = 0;
        virtual std::optional<std::string> getConstraintSet(int index) = 0;
    };

}
