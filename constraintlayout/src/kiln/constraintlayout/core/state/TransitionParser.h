#pragma once

// TransitionParser (constraintlayout-core state): fills a Transition from a MotionScene's
// transition JSON5: path arc, interpolator, stagger, OnSwipe and the KeyFrames (KeyPositions,
// KeyAttributes, KeyCycles).

#include "kiln/constraintlayout/core/parser/CLParser.h"

namespace kiln::constraintlayout::core::state {

    class Transition;

    class TransitionParser {
    public:
        static void parse(const CLObject& json, Transition& transition);
        static void parseKeyFrames(const CLObject& transitionCLObject, Transition& transition);

    private:
        static void parseOnSwipe(const CLContainer& onSwipe, Transition& transition);
        static void parseKeyPosition(const CLObject& keyPosition, Transition& transition);
        static void parseKeyAttribute(const CLObject& keyAttribute, Transition& transition);
        static void parseKeyCycle(const CLObject& keyCycleData, Transition& transition);
    };

}
