#include "kiln/constraintlayout/core/state/TransitionParser.h"

#include "kiln/constraintlayout/core/motion/CustomVariable.h"
#include "kiln/constraintlayout/core/motion/utils/TypedBundle.h"
#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/state/ConstraintSetParser.h"
#include "kiln/constraintlayout/core/state/Transition.h"

#include <cmath>
#include <optional>
#include <vector>

namespace kiln::constraintlayout::core::state {

    using motion::CustomVariable;
    using motion::TypedBundle;
    using motion::TypedValues;
    using AT = TypedValues::AttributesType;
    using CT = TypedValues::CycleType;
    using PT = TypedValues::PositionType;

    namespace {
        int map(const std::optional<std::string>& val, const std::vector<std::string>& types) {
            for (std::size_t i = 0; i < types.size(); i++)
                if (val && types[i] == *val) return int(i);
            return 0;
        }

        void map(TypedBundle& bundle, int type, const std::string& val, const std::vector<std::string>& types) {
            for (std::size_t i = 0; i < types.size(); i++)
                if (types[i] == val) bundle.add(type, int(i));
        }

        void set(TypedBundle& bundle, int type, const CLArray* array, int index) {
            if (array != nullptr) bundle.add(type, array->getFloat(index));
        }
    }

    void TransitionParser::parse(const CLObject& json, Transition& transition) {
        transition.resetProperties();
        const std::optional<std::string> pathMotionArc = json.getStringOrNull("pathMotionArc");
        TypedBundle bundle;
        bool setBundle = false;
        if (pathMotionArc) {
            setBundle = true;
            static const char* const arcs[] = { "none", "startVertical", "startHorizontal", "flip", "below", "above" };
            for (int i = 0; i < 6; i++)
                if (*pathMotionArc == arcs[i]) bundle.add(PT::TYPE_PATH_MOTION_ARC, i);
        }
        if (const std::optional<std::string> interpolator = json.getStringOrNull("interpolator")) {
            setBundle = true;
            bundle.add(TypedValues::TransitionType::TYPE_INTERPOLATOR, *interpolator);
        }
        const float staggered = json.getFloatOrNaN("staggered");
        if (!std::isnan(staggered)) {
            setBundle = true;
            bundle.add(TypedValues::TransitionType::TYPE_STAGGERED, staggered);
        }
        if (setBundle) transition.setTransitionProperties(bundle);
        if (const CLContainer* onSwipe = json.getObjectOrNull("onSwipe")) parseOnSwipe(*onSwipe, transition);
        parseKeyFrames(json, transition);
    }

    void TransitionParser::parseOnSwipe(const CLContainer& onSwipe, Transition& transition) {
        using OnSwipe = Transition::OnSwipe;
        const std::optional<std::string> anchor = onSwipe.getStringOrNull("anchor");
        const int side = map(onSwipe.getStringOrNull("side"), OnSwipe::SIDES);
        const int direction = map(onSwipe.getStringOrNull("direction"), OnSwipe::DIRECTIONS);
        const float scale = onSwipe.getFloatOrNaN("scale");
        const float threshold = onSwipe.getFloatOrNaN("threshold");
        const float maxVelocity = onSwipe.getFloatOrNaN("maxVelocity");
        const float maxAccel = onSwipe.getFloatOrNaN("maxAccel");
        const std::optional<std::string> limitBounds = onSwipe.getStringOrNull("limitBounds");
        const int autoCompleteMode = map(onSwipe.getStringOrNull("mode"), OnSwipe::MODE);
        const int touchUp = map(onSwipe.getStringOrNull("touchUp"), OnSwipe::TOUCH_UP);
        const float springMass = onSwipe.getFloatOrNaN("springMass");
        const float springStiffness = onSwipe.getFloatOrNaN("springStiffness");
        const float springDamping = onSwipe.getFloatOrNaN("springDamping");
        const float stopThreshold = onSwipe.getFloatOrNaN("stopThreshold");
        const int springBoundary = map(onSwipe.getStringOrNull("springBoundary"), OnSwipe::BOUNDARY);
        const std::optional<std::string> around = onSwipe.getStringOrNull("around");
        OnSwipe& swipe = transition.createOnSwipe();
        swipe.setAnchorId(anchor);
        swipe.setAnchorSide(side);
        swipe.setDragDirection(direction);
        swipe.setDragScale(scale);
        swipe.setDragThreshold(threshold);
        swipe.setMaxVelocity(maxVelocity);
        swipe.setMaxAcceleration(maxAccel);
        swipe.setLimitBoundsTo(limitBounds);
        swipe.setAutoCompleteMode(autoCompleteMode);
        swipe.setOnTouchUp(touchUp);
        swipe.setSpringMass(springMass);
        swipe.setSpringStiffness(springStiffness);
        swipe.setSpringDamping(springDamping);
        swipe.setSpringStopThreshold(stopThreshold);
        swipe.setSpringBoundary(springBoundary);
        swipe.setRotationCenterId(around);
    }

    void TransitionParser::parseKeyFrames(const CLObject& transitionCLObject, Transition& transition) {
        const CLContainer* keyframes = transitionCLObject.getObjectOrNull("KeyFrames");
        if (keyframes == nullptr) return;
        if (const CLArray* keyPositions = keyframes->getArrayOrNull("KeyPositions")) {
            for (int i = 0; i < keyPositions->size(); i++) {
                CLElement* keyPosition = keyPositions->get(i);
                if (keyPosition->is(CLElement::KIND_OBJECT)) parseKeyPosition(static_cast<const CLObject&>(*keyPosition), transition);
            }
        }
        if (const CLArray* keyAttributes = keyframes->getArrayOrNull("KeyAttributes")) {
            for (int i = 0; i < keyAttributes->size(); i++) {
                CLElement* keyAttribute = keyAttributes->get(i);
                if (keyAttribute->is(CLElement::KIND_OBJECT)) parseKeyAttribute(static_cast<const CLObject&>(*keyAttribute), transition);
            }
        }
        if (const CLArray* keyCycles = keyframes->getArrayOrNull("KeyCycles")) {
            for (int i = 0; i < keyCycles->size(); i++) {
                CLElement* keyCycle = keyCycles->get(i);
                if (keyCycle->is(CLElement::KIND_OBJECT)) parseKeyCycle(static_cast<const CLObject&>(*keyCycle), transition);
            }
        }
    }

    void TransitionParser::parseKeyPosition(const CLObject& keyPosition, Transition& transition) {
        TypedBundle bundle;
        const CLArray* targets = keyPosition.getArray("target");
        const CLArray* frames = keyPosition.getArray("frames");
        const CLArray* percentX = keyPosition.getArrayOrNull("percentX");
        const CLArray* percentY = keyPosition.getArrayOrNull("percentY");
        const CLArray* percentWidth = keyPosition.getArrayOrNull("percentWidth");
        const CLArray* percentHeight = keyPosition.getArrayOrNull("percentHeight");
        const std::optional<std::string> pathMotionArc = keyPosition.getStringOrNull("pathMotionArc");
        const std::optional<std::string> transitionEasing = keyPosition.getStringOrNull("transitionEasing");
        const std::optional<std::string> curveFit = keyPosition.getStringOrNull("curveFit");
        const std::string type = keyPosition.getStringOrNull("type").value_or("parentRelative");
        if (percentX != nullptr && frames->size() != percentX->size()) return;
        if (percentY != nullptr && frames->size() != percentY->size()) return;
        for (int i = 0; i < targets->size(); i++) {
            const std::string target = targets->getString(i);
            const int pos_type = map(type, { "deltaRelative", "pathRelative", "parentRelative" });
            bundle.clear();
            bundle.add(PT::TYPE_POSITION_TYPE, pos_type);
            if (curveFit) map(bundle, PT::TYPE_CURVE_FIT, *curveFit, { "spline", "linear" });
            bundle.addIfNotNull(PT::TYPE_TRANSITION_EASING, transitionEasing.value_or(""));
            if (pathMotionArc) map(bundle, PT::TYPE_PATH_MOTION_ARC, *pathMotionArc, { "none", "startVertical", "startHorizontal", "flip", "below", "above" });
            for (int j = 0; j < frames->size(); j++) {
                bundle.add(TypedValues::TYPE_FRAME_POSITION, frames->getInt(j));
                set(bundle, PT::TYPE_PERCENT_X, percentX, j);
                set(bundle, PT::TYPE_PERCENT_Y, percentY, j);
                set(bundle, PT::TYPE_PERCENT_WIDTH, percentWidth, j);
                set(bundle, PT::TYPE_PERCENT_HEIGHT, percentHeight, j);
                transition.addKeyPosition(target, bundle);
            }
        }
    }

    void TransitionParser::parseKeyAttribute(const CLObject& keyAttribute, Transition& transition) {
        const CLArray* targets = keyAttribute.getArrayOrNull("target");
        if (targets == nullptr) return;
        const CLArray* frames = keyAttribute.getArrayOrNull("frames");
        if (frames == nullptr) return;
        const std::optional<std::string> transitionEasing = keyAttribute.getStringOrNull("transitionEasing");
        const char* const attrNames[] = { AT::S_SCALE_X,  AT::S_SCALE_Y,    AT::S_TRANSLATION_X, AT::S_TRANSLATION_Y, AT::S_TRANSLATION_Z,
                                          AT::S_ROTATION_X, AT::S_ROTATION_Y, AT::S_ROTATION_Z,    AT::S_ALPHA };
        const int attrIds[] = { AT::TYPE_SCALE_X,    AT::TYPE_SCALE_Y,    AT::TYPE_TRANSLATION_X, AT::TYPE_TRANSLATION_Y, AT::TYPE_TRANSLATION_Z,
                                AT::TYPE_ROTATION_X, AT::TYPE_ROTATION_Y, AT::TYPE_ROTATION_Z,    AT::TYPE_ALPHA };
        const bool scaleTypes[] = { false, false, true, true, true, false, false, false, false };
        const std::size_t count = std::size_t(frames->size());
        std::vector<TypedBundle> bundles(count);
        std::vector<std::vector<std::optional<CustomVariable>>> customVars;
        bool hasCustom = false;
        for (int k = 0; k < 9; k++) {
            const std::string attrName = attrNames[k];
            const CLArray* arrayValues = keyAttribute.getArrayOrNull(attrName);
            if (arrayValues != nullptr && std::size_t(arrayValues->size()) != count)
                throw CLParsingException("incorrect size for " + attrName + " array, not matching targets array!", &keyAttribute);
            if (arrayValues != nullptr) {
                for (std::size_t i = 0; i < count; i++) {
                    float value = arrayValues->getFloat(int(i));
                    if (scaleTypes[k]) value = transition.mToPixel(value);
                    bundles[i].add(attrIds[k], value);
                }
            } else {
                float value = keyAttribute.getFloatOrNaN(attrName);
                if (!std::isnan(value)) {
                    if (scaleTypes[k]) value = transition.mToPixel(value);
                    for (std::size_t i = 0; i < count; i++) bundles[i].add(attrIds[k], value);
                }
            }
        }
        CLElement* customElement = keyAttribute.getOrNull("custom");
        if (customElement != nullptr && customElement->is(CLElement::KIND_OBJECT)) {
            auto* customObj = static_cast<CLObject*>(customElement);
            const int n = customObj->size();
            hasCustom = true;
            customVars.assign(count, std::vector<std::optional<CustomVariable>>(std::size_t(n)));
            for (int i = 0; i < n; i++) {
                auto* key = static_cast<CLKey*>(customObj->get(i));
                const std::string customName = key->content();
                CLElement* value = key->getValue();
                if (value->is(CLElement::KIND_ARRAY)) {
                    auto* arrayValues = static_cast<CLArray*>(value);
                    const std::size_t vSize = std::size_t(arrayValues->size());
                    if (vSize == count && vSize > 0) {
                        if (arrayValues->get(0)->is(CLElement::KIND_NUMBER)) {
                            for (std::size_t j = 0; j < count; j++)
                                customVars[j][std::size_t(i)] = CustomVariable(customName, TypedValues::Custom::TYPE_FLOAT, arrayValues->get(int(j))->getFloat());
                        } else {
                            for (std::size_t j = 0; j < count; j++) {
                                const int64_t color = ConstraintSetParser::parseColorString(arrayValues->get(int(j))->content());
                                if (color != -1) customVars[j][std::size_t(i)] = CustomVariable(customName, TypedValues::Custom::TYPE_COLOR, int(color));
                            }
                        }
                    }
                } else if (value->is(CLElement::KIND_NUMBER)) {
                    const float fValue = value->getFloat();
                    for (std::size_t j = 0; j < count; j++) customVars[j][std::size_t(i)] = CustomVariable(customName, TypedValues::Custom::TYPE_FLOAT, fValue);
                } else {
                    const int64_t cValue = ConstraintSetParser::parseColorString(value->content());
                    if (cValue != -1) {
                        for (std::size_t j = 0; j < count; j++)
                            customVars[j][std::size_t(i)] = CustomVariable(customName, TypedValues::Custom::TYPE_COLOR, int(cValue));
                    }
                }
            }
        }
        const std::optional<std::string> curveFit = keyAttribute.getStringOrNull("curveFit");
        for (int i = 0; i < targets->size(); i++) {
            for (std::size_t j = 0; j < count; j++) {
                const std::string target = targets->getString(i);
                TypedBundle& bundle = bundles[j];
                if (curveFit) bundle.add(PT::TYPE_CURVE_FIT, map(curveFit, { "spline", "linear" }));
                bundle.addIfNotNull(PT::TYPE_TRANSITION_EASING, transitionEasing.value_or(""));
                bundle.add(TypedValues::TYPE_FRAME_POSITION, frames->getInt(int(j)));
                transition.addKeyAttribute(target, bundle, hasCustom ? &customVars[j] : nullptr);
            }
        }
    }

    void TransitionParser::parseKeyCycle(const CLObject& keyCycleData, Transition& transition) {
        const CLArray* targets = keyCycleData.getArray("target");
        const CLArray* frames = keyCycleData.getArray("frames");
        const std::optional<std::string> transitionEasing = keyCycleData.getStringOrNull("transitionEasing");
        const char* const attrNames[] = { CT::S_SCALE_X,    CT::S_SCALE_Y,    CT::S_TRANSLATION_X, CT::S_TRANSLATION_Y, CT::S_TRANSLATION_Z, CT::S_ROTATION_X,
                                          CT::S_ROTATION_Y, CT::S_ROTATION_Z, CT::S_ALPHA,         CT::S_WAVE_PERIOD,   CT::S_WAVE_OFFSET,   CT::S_WAVE_PHASE };
        const int attrIds[] = { CT::TYPE_SCALE_X,    CT::TYPE_SCALE_Y,    CT::TYPE_TRANSLATION_X, CT::TYPE_TRANSLATION_Y,
                                CT::TYPE_TRANSLATION_Z, CT::TYPE_ROTATION_X, CT::TYPE_ROTATION_Y,    CT::TYPE_ROTATION_Z,
                                CT::TYPE_ALPHA,      CT::TYPE_WAVE_PERIOD, CT::TYPE_WAVE_OFFSET,  CT::TYPE_WAVE_PHASE };
        const int scaleTypes[] = { 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 2, 0 };
        const std::size_t count = std::size_t(frames->size());
        std::vector<TypedBundle> bundles(count);
        bool scaleOffset = false;
        for (int k = 0; k < 12; k++)
            if (keyCycleData.has(attrNames[k]) && scaleTypes[k] == 1) scaleOffset = true;
        for (int k = 0; k < 12; k++) {
            const std::string attrName = attrNames[k];
            const int scale = scaleTypes[k];
            const CLArray* arrayValues = keyCycleData.getArrayOrNull(attrName);
            if (arrayValues != nullptr && std::size_t(arrayValues->size()) != count)
                throw CLParsingException("incorrect size for $attrName array, not matching targets array!", &keyCycleData);   // sic
            auto scaled = [&](float value) {
                if (scale == 1) return transition.mToPixel(value);
                if (scale == 2 && scaleOffset) return transition.mToPixel(value);
                return value;
            };
            if (arrayValues != nullptr) {
                for (std::size_t i = 0; i < count; i++) bundles[i].add(attrIds[k], scaled(arrayValues->getFloat(int(i))));
            } else {
                const float value = keyCycleData.getFloatOrNaN(attrName);
                if (!std::isnan(value)) {
                    const float v = scaled(value);
                    for (std::size_t i = 0; i < count; i++) bundles[i].add(attrIds[k], v);
                }
            }
        }
        const std::optional<std::string> curveFit = keyCycleData.getStringOrNull(CT::S_CURVE_FIT);
        const std::optional<std::string> easing = keyCycleData.getStringOrNull(CT::S_EASING);
        const std::optional<std::string> waveShape = keyCycleData.getStringOrNull(CT::S_WAVE_SHAPE);
        const std::optional<std::string> customWave = keyCycleData.getStringOrNull(CT::S_CUSTOM_WAVE_SHAPE);
        for (int i = 0; i < targets->size(); i++) {
            for (std::size_t j = 0; j < count; j++) {
                const std::string target = targets->getString(i);
                TypedBundle& bundle = bundles[j];
                if (curveFit) {
                    if (*curveFit == "spline") bundle.add(CT::TYPE_CURVE_FIT, 0);
                    else if (*curveFit == "linear") bundle.add(CT::TYPE_CURVE_FIT, 1);
                }
                bundle.addIfNotNull(PT::TYPE_TRANSITION_EASING, transitionEasing.value_or(""));
                if (easing) bundle.add(CT::TYPE_EASING, *easing);
                if (waveShape) bundle.add(CT::TYPE_WAVE_SHAPE, *waveShape);
                if (customWave) bundle.add(CT::TYPE_CUSTOM_WAVE_SHAPE, *customWave);
                bundle.add(TypedValues::TYPE_FRAME_POSITION, frames->getInt(int(j)));
                transition.addKeyCycle(target, bundle);
            }
        }
    }

}
