#pragma once

// TypedValues (constraintlayout-core motion utils): the attribute ids and names of
// MotionLayout's keyframes, cycles, triggers, positions, transitions and swipes, and the
// interface objects that take them by id implement. Generated from TypedValues.java, then
// reviewed: Java's string switches are if chains.

#include <string>

namespace kiln::constraintlayout::core::motion {

    class TypedValues {
    public:
        virtual ~TypedValues() = default;
        static constexpr const char* S_CUSTOM = "CUSTOM";
        static constexpr int BOOLEAN_MASK = 1;
        static constexpr int INT_MASK = 2;
        static constexpr int FLOAT_MASK = 4;
        static constexpr int STRING_MASK = 8;
        virtual bool setValue(int id, int value) = 0;
        virtual bool setValue(int id, float value) = 0;
        virtual bool setValue(int id, const std::string& value) = 0;
        virtual bool setValue(int id, bool value) = 0;
        virtual int getId(const std::string& name) = 0;
        static constexpr int TYPE_FRAME_POSITION = 100;
        static constexpr int TYPE_TARGET = 101;
        struct AttributesType {
            static constexpr const char* NAME = "KeyAttributes";
            static constexpr int TYPE_CURVE_FIT = 301;
            static constexpr int TYPE_VISIBILITY = 302;
            static constexpr int TYPE_ALPHA = 303;
            static constexpr int TYPE_TRANSLATION_X = 304;
            static constexpr int TYPE_TRANSLATION_Y = 305;
            static constexpr int TYPE_TRANSLATION_Z = 306;
            static constexpr int TYPE_ELEVATION = 307;
            static constexpr int TYPE_ROTATION_X = 308;
            static constexpr int TYPE_ROTATION_Y = 309;
            static constexpr int TYPE_ROTATION_Z = 310;
            static constexpr int TYPE_SCALE_X = 311;
            static constexpr int TYPE_SCALE_Y = 312;
            static constexpr int TYPE_PIVOT_X = 313;
            static constexpr int TYPE_PIVOT_Y = 314;
            static constexpr int TYPE_PROGRESS = 315;
            static constexpr int TYPE_PATH_ROTATE = 316;
            static constexpr int TYPE_EASING = 317;
            static constexpr int TYPE_PIVOT_TARGET = 318;
            static constexpr const char* S_CURVE_FIT = "curveFit";
            static constexpr const char* S_VISIBILITY = "visibility";
            static constexpr const char* S_ALPHA = "alpha";
            static constexpr const char* S_TRANSLATION_X = "translationX";
            static constexpr const char* S_TRANSLATION_Y = "translationY";
            static constexpr const char* S_TRANSLATION_Z = "translationZ";
            static constexpr const char* S_ELEVATION = "elevation";
            static constexpr const char* S_ROTATION_X = "rotationX";
            static constexpr const char* S_ROTATION_Y = "rotationY";
            static constexpr const char* S_ROTATION_Z = "rotationZ";
            static constexpr const char* S_SCALE_X = "scaleX";
            static constexpr const char* S_SCALE_Y = "scaleY";
            static constexpr const char* S_PIVOT_X = "pivotX";
            static constexpr const char* S_PIVOT_Y = "pivotY";
            static constexpr const char* S_PROGRESS = "progress";
            static constexpr const char* S_PATH_ROTATE = "pathRotate";
            static constexpr const char* S_EASING = "easing";
            static constexpr const char* S_CUSTOM = "CUSTOM";
            static constexpr const char* S_FRAME = "frame";
            static constexpr const char* S_TARGET = "target";
            static constexpr const char* S_PIVOT_TARGET = "pivotTarget";
            static constexpr const char* KEY_WORDS[] = {
                S_CURVE_FIT,  S_VISIBILITY,  S_ALPHA,      S_TRANSLATION_X, S_TRANSLATION_Y, S_TRANSLATION_Z, S_ELEVATION,
                S_ROTATION_X, S_ROTATION_Y,  S_ROTATION_Z, S_SCALE_X,       S_SCALE_Y,       S_PIVOT_X,       S_PIVOT_Y,
                S_PROGRESS,   S_PATH_ROTATE, S_EASING,     S_CUSTOM,        S_FRAME,         S_TARGET,        S_PIVOT_TARGET,
            };
            static int getId(const std::string& name) {
                if (name == S_CURVE_FIT) return TYPE_CURVE_FIT;
                if (name == S_VISIBILITY) return TYPE_VISIBILITY;
                if (name == S_ALPHA) return TYPE_ALPHA;
                if (name == S_TRANSLATION_X) return TYPE_TRANSLATION_X;
                if (name == S_TRANSLATION_Y) return TYPE_TRANSLATION_Y;
                if (name == S_TRANSLATION_Z) return TYPE_TRANSLATION_Z;
                if (name == S_ELEVATION) return TYPE_ELEVATION;
                if (name == S_ROTATION_X) return TYPE_ROTATION_X;
                if (name == S_ROTATION_Y) return TYPE_ROTATION_Y;
                if (name == S_ROTATION_Z) return TYPE_ROTATION_Z;
                if (name == S_SCALE_X) return TYPE_SCALE_X;
                if (name == S_SCALE_Y) return TYPE_SCALE_Y;
                if (name == S_PIVOT_X) return TYPE_PIVOT_X;
                if (name == S_PIVOT_Y) return TYPE_PIVOT_Y;
                if (name == S_PROGRESS) return TYPE_PROGRESS;
                if (name == S_PATH_ROTATE) return TYPE_PATH_ROTATE;
                if (name == S_EASING) return TYPE_EASING;
                if (name == S_FRAME) return TYPE_FRAME_POSITION;
                if (name == S_TARGET) return TYPE_TARGET;
                if (name == S_PIVOT_TARGET) return TYPE_PIVOT_TARGET;
                return -1;
            }
            static int getType(int name) {
                switch (name) {
                case TYPE_CURVE_FIT:
                case TYPE_VISIBILITY:
                case TYPE_FRAME_POSITION:
                    return INT_MASK;
                case TYPE_ALPHA:
                case TYPE_TRANSLATION_X:
                case TYPE_TRANSLATION_Y:
                case TYPE_TRANSLATION_Z:
                case TYPE_ELEVATION:
                case TYPE_ROTATION_X:
                case TYPE_ROTATION_Y:
                case TYPE_ROTATION_Z:
                case TYPE_SCALE_X:
                case TYPE_SCALE_Y:
                case TYPE_PIVOT_X:
                case TYPE_PIVOT_Y:
                case TYPE_PROGRESS:
                case TYPE_PATH_ROTATE:
                    return FLOAT_MASK;
                case TYPE_EASING:
                case TYPE_TARGET:
                case TYPE_PIVOT_TARGET:
                    return STRING_MASK;
                }
                return -1;
            }
        };
        struct CycleType {
            static constexpr const char* NAME = "KeyCycle";
            static constexpr int TYPE_CURVE_FIT = 401;
            static constexpr int TYPE_VISIBILITY = 402;
            static constexpr int TYPE_ALPHA = 403;
            static constexpr int TYPE_TRANSLATION_X = AttributesType::TYPE_TRANSLATION_X;
            static constexpr int TYPE_TRANSLATION_Y = AttributesType::TYPE_TRANSLATION_Y;
            static constexpr int TYPE_TRANSLATION_Z = AttributesType::TYPE_TRANSLATION_Z;
            static constexpr int TYPE_ELEVATION = AttributesType::TYPE_ELEVATION;
            static constexpr int TYPE_ROTATION_X = AttributesType::TYPE_ROTATION_X;
            static constexpr int TYPE_ROTATION_Y = AttributesType::TYPE_ROTATION_Y;
            static constexpr int TYPE_ROTATION_Z = AttributesType::TYPE_ROTATION_Z;
            static constexpr int TYPE_SCALE_X = AttributesType::TYPE_SCALE_X;
            static constexpr int TYPE_SCALE_Y = AttributesType::TYPE_SCALE_Y;
            static constexpr int TYPE_PIVOT_X = AttributesType::TYPE_PIVOT_X;
            static constexpr int TYPE_PIVOT_Y = AttributesType::TYPE_PIVOT_Y;
            static constexpr int TYPE_PROGRESS = AttributesType::TYPE_PROGRESS;
            static constexpr int TYPE_PATH_ROTATE = 416;
            static constexpr int TYPE_EASING = 420;
            static constexpr int TYPE_WAVE_SHAPE = 421;
            static constexpr int TYPE_CUSTOM_WAVE_SHAPE = 422;
            static constexpr int TYPE_WAVE_PERIOD = 423;
            static constexpr int TYPE_WAVE_OFFSET = 424;
            static constexpr int TYPE_WAVE_PHASE = 425;
            static constexpr const char* S_CURVE_FIT = "curveFit";
            static constexpr const char* S_VISIBILITY = "visibility";
            static constexpr const char* S_ALPHA = AttributesType::S_ALPHA;
            static constexpr const char* S_TRANSLATION_X = AttributesType::S_TRANSLATION_X;
            static constexpr const char* S_TRANSLATION_Y = AttributesType::S_TRANSLATION_Y;
            static constexpr const char* S_TRANSLATION_Z = AttributesType::S_TRANSLATION_Z;
            static constexpr const char* S_ELEVATION = AttributesType::S_ELEVATION;
            static constexpr const char* S_ROTATION_X = AttributesType::S_ROTATION_X;
            static constexpr const char* S_ROTATION_Y = AttributesType::S_ROTATION_Y;
            static constexpr const char* S_ROTATION_Z = AttributesType::S_ROTATION_Z;
            static constexpr const char* S_SCALE_X = AttributesType::S_SCALE_X;
            static constexpr const char* S_SCALE_Y = AttributesType::S_SCALE_Y;
            static constexpr const char* S_PIVOT_X = AttributesType::S_PIVOT_X;
            static constexpr const char* S_PIVOT_Y = AttributesType::S_PIVOT_Y;
            static constexpr const char* S_PROGRESS = AttributesType::S_PROGRESS;
            static constexpr const char* S_PATH_ROTATE = "pathRotate";
            static constexpr const char* S_EASING = "easing";
            static constexpr const char* S_WAVE_SHAPE = "waveShape";
            static constexpr const char* S_CUSTOM_WAVE_SHAPE = "customWave";
            static constexpr const char* S_WAVE_PERIOD = "period";
            static constexpr const char* S_WAVE_OFFSET = "offset";
            static constexpr const char* S_WAVE_PHASE = "phase";
            static constexpr const char* KEY_WORDS[] = {
                S_CURVE_FIT,         S_VISIBILITY,  S_ALPHA,       S_TRANSLATION_X, S_TRANSLATION_Y, S_TRANSLATION_Z,
                S_ELEVATION,         S_ROTATION_X,  S_ROTATION_Y,  S_ROTATION_Z,    S_SCALE_X,       S_SCALE_Y,
                S_PIVOT_X,           S_PIVOT_Y,     S_PROGRESS,    S_PATH_ROTATE,   S_EASING,        S_WAVE_SHAPE,
                S_CUSTOM_WAVE_SHAPE, S_WAVE_PERIOD, S_WAVE_OFFSET, S_WAVE_PHASE,
            };
            static int getId(const std::string& name) {
                if (name == S_CURVE_FIT) return TYPE_CURVE_FIT;
                if (name == S_VISIBILITY) return TYPE_VISIBILITY;
                if (name == S_ALPHA) return TYPE_ALPHA;
                if (name == S_TRANSLATION_X) return TYPE_TRANSLATION_X;
                if (name == S_TRANSLATION_Y) return TYPE_TRANSLATION_Y;
                if (name == S_TRANSLATION_Z) return TYPE_TRANSLATION_Z;
                if (name == S_ROTATION_X) return TYPE_ROTATION_X;
                if (name == S_ROTATION_Y) return TYPE_ROTATION_Y;
                if (name == S_ROTATION_Z) return TYPE_ROTATION_Z;
                if (name == S_SCALE_X) return TYPE_SCALE_X;
                if (name == S_SCALE_Y) return TYPE_SCALE_Y;
                if (name == S_PIVOT_X) return TYPE_PIVOT_X;
                if (name == S_PIVOT_Y) return TYPE_PIVOT_Y;
                if (name == S_PROGRESS) return TYPE_PROGRESS;
                if (name == S_PATH_ROTATE) return TYPE_PATH_ROTATE;
                if (name == S_EASING) return TYPE_EASING;
                return -1;
            }
            static int getType(int name) {
                switch (name) {
                case TYPE_CURVE_FIT:
                case TYPE_VISIBILITY:
                case TYPE_FRAME_POSITION:
                    return INT_MASK;
                case TYPE_ALPHA:
                case TYPE_TRANSLATION_X:
                case TYPE_TRANSLATION_Y:
                case TYPE_TRANSLATION_Z:
                case TYPE_ELEVATION:
                case TYPE_ROTATION_X:
                case TYPE_ROTATION_Y:
                case TYPE_ROTATION_Z:
                case TYPE_SCALE_X:
                case TYPE_SCALE_Y:
                case TYPE_PIVOT_X:
                case TYPE_PIVOT_Y:
                case TYPE_PROGRESS:
                case TYPE_PATH_ROTATE:
                case TYPE_WAVE_PERIOD:
                case TYPE_WAVE_OFFSET:
                case TYPE_WAVE_PHASE:
                    return FLOAT_MASK;
                case TYPE_EASING:
                case TYPE_TARGET:
                case TYPE_WAVE_SHAPE:
                    return STRING_MASK;
                }
                return -1;
            }
        };
        struct TriggerType {
            static constexpr const char* NAME = "KeyTrigger";
            static constexpr const char* VIEW_TRANSITION_ON_CROSS = "viewTransitionOnCross";
            static constexpr const char* VIEW_TRANSITION_ON_POSITIVE_CROSS = "viewTransitionOnPositiveCross";
            static constexpr const char* VIEW_TRANSITION_ON_NEGATIVE_CROSS = "viewTransitionOnNegativeCross";
            static constexpr const char* POST_LAYOUT = "postLayout";
            static constexpr const char* TRIGGER_SLACK = "triggerSlack";
            static constexpr const char* TRIGGER_COLLISION_VIEW = "triggerCollisionView";
            static constexpr const char* TRIGGER_COLLISION_ID = "triggerCollisionId";
            static constexpr const char* TRIGGER_ID = "triggerID";
            static constexpr const char* POSITIVE_CROSS = "positiveCross";
            static constexpr const char* NEGATIVE_CROSS = "negativeCross";
            static constexpr const char* TRIGGER_RECEIVER = "triggerReceiver";
            static constexpr const char* CROSS = "CROSS";
            static constexpr const char* KEY_WORDS[] = {
                VIEW_TRANSITION_ON_CROSS,
                VIEW_TRANSITION_ON_POSITIVE_CROSS,
                VIEW_TRANSITION_ON_NEGATIVE_CROSS,
                POST_LAYOUT,
                TRIGGER_SLACK,
                TRIGGER_COLLISION_VIEW,
                TRIGGER_COLLISION_ID,
                TRIGGER_ID,
                POSITIVE_CROSS,
                NEGATIVE_CROSS,
                TRIGGER_RECEIVER,
                CROSS,
            };
            static constexpr int TYPE_VIEW_TRANSITION_ON_CROSS = 301;
            static constexpr int TYPE_VIEW_TRANSITION_ON_POSITIVE_CROSS = 302;
            static constexpr int TYPE_VIEW_TRANSITION_ON_NEGATIVE_CROSS = 303;
            static constexpr int TYPE_POST_LAYOUT = 304;
            static constexpr int TYPE_TRIGGER_SLACK = 305;
            static constexpr int TYPE_TRIGGER_COLLISION_VIEW = 306;
            static constexpr int TYPE_TRIGGER_COLLISION_ID = 307;
            static constexpr int TYPE_TRIGGER_ID = 308;
            static constexpr int TYPE_POSITIVE_CROSS = 309;
            static constexpr int TYPE_NEGATIVE_CROSS = 310;
            static constexpr int TYPE_TRIGGER_RECEIVER = 311;
            static constexpr int TYPE_CROSS = 312;
            static int getId(const std::string& name) {
                if (name == VIEW_TRANSITION_ON_CROSS) return TYPE_VIEW_TRANSITION_ON_CROSS;
                if (name == VIEW_TRANSITION_ON_POSITIVE_CROSS) return TYPE_VIEW_TRANSITION_ON_POSITIVE_CROSS;
                if (name == VIEW_TRANSITION_ON_NEGATIVE_CROSS) return TYPE_VIEW_TRANSITION_ON_NEGATIVE_CROSS;
                if (name == POST_LAYOUT) return TYPE_POST_LAYOUT;
                if (name == TRIGGER_SLACK) return TYPE_TRIGGER_SLACK;
                if (name == TRIGGER_COLLISION_VIEW) return TYPE_TRIGGER_COLLISION_VIEW;
                if (name == TRIGGER_COLLISION_ID) return TYPE_TRIGGER_COLLISION_ID;
                if (name == TRIGGER_ID) return TYPE_TRIGGER_ID;
                if (name == POSITIVE_CROSS) return TYPE_POSITIVE_CROSS;
                if (name == NEGATIVE_CROSS) return TYPE_NEGATIVE_CROSS;
                if (name == TRIGGER_RECEIVER) return TYPE_TRIGGER_RECEIVER;
                if (name == CROSS) return TYPE_CROSS;
                return -1;
            }
        };
        struct PositionType {
            static constexpr const char* NAME = "KeyPosition";
            static constexpr const char* S_TRANSITION_EASING = "transitionEasing";
            static constexpr const char* S_DRAWPATH = "drawPath";
            static constexpr const char* S_PERCENT_WIDTH = "percentWidth";
            static constexpr const char* S_PERCENT_HEIGHT = "percentHeight";
            static constexpr const char* S_SIZE_PERCENT = "sizePercent";
            static constexpr const char* S_PERCENT_X = "percentX";
            static constexpr const char* S_PERCENT_Y = "percentY";
            static constexpr int TYPE_TRANSITION_EASING = 501;
            static constexpr int TYPE_DRAWPATH = 502;
            static constexpr int TYPE_PERCENT_WIDTH = 503;
            static constexpr int TYPE_PERCENT_HEIGHT = 504;
            static constexpr int TYPE_SIZE_PERCENT = 505;
            static constexpr int TYPE_PERCENT_X = 506;
            static constexpr int TYPE_PERCENT_Y = 507;
            static constexpr int TYPE_CURVE_FIT = 508;
            static constexpr int TYPE_PATH_MOTION_ARC = 509;
            static constexpr int TYPE_POSITION_TYPE = 510;
            static constexpr const char* KEY_WORDS[] = {
                S_TRANSITION_EASING, S_DRAWPATH, S_PERCENT_WIDTH, S_PERCENT_HEIGHT, S_SIZE_PERCENT, S_PERCENT_X, S_PERCENT_Y,
            };
            static int getId(const std::string& name) {
                if (name == S_TRANSITION_EASING) return PositionType::TYPE_TRANSITION_EASING;
                if (name == S_DRAWPATH) return PositionType::TYPE_DRAWPATH;
                if (name == S_PERCENT_WIDTH) return PositionType::TYPE_PERCENT_WIDTH;
                if (name == S_PERCENT_HEIGHT) return PositionType::TYPE_PERCENT_HEIGHT;
                if (name == S_SIZE_PERCENT) return PositionType::TYPE_SIZE_PERCENT;
                if (name == S_PERCENT_X) return PositionType::TYPE_PERCENT_X;
                if (name == S_PERCENT_Y) return PositionType::TYPE_PERCENT_Y;
                return -1;
            }
            static int getType(int name) {
                switch (name) {
                case TYPE_CURVE_FIT:
                case TYPE_FRAME_POSITION:
                    return INT_MASK;
                case TYPE_PERCENT_WIDTH:
                case TYPE_PERCENT_HEIGHT:
                case TYPE_SIZE_PERCENT:
                case TYPE_PERCENT_X:
                case TYPE_PERCENT_Y:
                    return FLOAT_MASK;
                case TYPE_TRANSITION_EASING:
                case TYPE_TARGET:
                case TYPE_DRAWPATH:
                    return STRING_MASK;
                }
                return -1;
            }
        };
        struct MotionType {
            static constexpr const char* NAME = "Motion";
            static constexpr const char* S_STAGGER = "Stagger";
            static constexpr const char* S_PATH_ROTATE = "PathRotate";
            static constexpr const char* S_QUANTIZE_MOTION_PHASE = "QuantizeMotionPhase";
            static constexpr const char* S_EASING = "TransitionEasing";
            static constexpr const char* S_QUANTIZE_INTERPOLATOR = "QuantizeInterpolator";
            static constexpr const char* S_ANIMATE_RELATIVE_TO = "AnimateRelativeTo";
            static constexpr const char* S_ANIMATE_CIRCLEANGLE_TO = "AnimateCircleAngleTo";
            static constexpr const char* S_PATHMOTION_ARC = "PathMotionArc";
            static constexpr const char* S_DRAW_PATH = "DrawPath";
            static constexpr const char* S_POLAR_RELATIVETO = "PolarRelativeTo";
            static constexpr const char* S_QUANTIZE_MOTIONSTEPS = "QuantizeMotionSteps";
            static constexpr const char* S_QUANTIZE_INTERPOLATOR_TYPE = "QuantizeInterpolatorType";
            static constexpr const char* S_QUANTIZE_INTERPOLATOR_ID = "QuantizeInterpolatorID";
            static constexpr const char* KEY_WORDS[] = {
                S_STAGGER,
                S_PATH_ROTATE,
                S_QUANTIZE_MOTION_PHASE,
                S_EASING,
                S_QUANTIZE_INTERPOLATOR,
                S_ANIMATE_RELATIVE_TO,
                S_ANIMATE_CIRCLEANGLE_TO,
                S_PATHMOTION_ARC,
                S_DRAW_PATH,
                S_POLAR_RELATIVETO,
                S_QUANTIZE_MOTIONSTEPS,
                S_QUANTIZE_INTERPOLATOR_TYPE,
                S_QUANTIZE_INTERPOLATOR_ID,
            };
            static constexpr int TYPE_STAGGER = 600;
            static constexpr int TYPE_PATH_ROTATE = 601;
            static constexpr int TYPE_QUANTIZE_MOTION_PHASE = 602;
            static constexpr int TYPE_EASING = 603;
            static constexpr int TYPE_QUANTIZE_INTERPOLATOR = 604;
            static constexpr int TYPE_ANIMATE_RELATIVE_TO = 605;
            static constexpr int TYPE_ANIMATE_CIRCLEANGLE_TO = 606;
            static constexpr int TYPE_PATHMOTION_ARC = 607;
            static constexpr int TYPE_DRAW_PATH = 608;
            static constexpr int TYPE_POLAR_RELATIVETO = 609;
            static constexpr int TYPE_QUANTIZE_MOTIONSTEPS = 610;
            static constexpr int TYPE_QUANTIZE_INTERPOLATOR_TYPE = 611;
            static constexpr int TYPE_QUANTIZE_INTERPOLATOR_ID = 612;
            static int getId(const std::string& name) {
                if (name == S_STAGGER) return TYPE_STAGGER;
                if (name == S_PATH_ROTATE) return TYPE_PATH_ROTATE;
                if (name == S_QUANTIZE_MOTION_PHASE) return TYPE_QUANTIZE_MOTION_PHASE;
                if (name == S_EASING) return TYPE_EASING;
                if (name == S_QUANTIZE_INTERPOLATOR) return TYPE_QUANTIZE_INTERPOLATOR;
                if (name == S_ANIMATE_RELATIVE_TO) return TYPE_ANIMATE_RELATIVE_TO;
                if (name == S_ANIMATE_CIRCLEANGLE_TO) return TYPE_ANIMATE_CIRCLEANGLE_TO;
                if (name == S_PATHMOTION_ARC) return TYPE_PATHMOTION_ARC;
                if (name == S_DRAW_PATH) return TYPE_DRAW_PATH;
                if (name == S_POLAR_RELATIVETO) return TYPE_POLAR_RELATIVETO;
                if (name == S_QUANTIZE_MOTIONSTEPS) return TYPE_QUANTIZE_MOTIONSTEPS;
                if (name == S_QUANTIZE_INTERPOLATOR_TYPE) return TYPE_QUANTIZE_INTERPOLATOR_TYPE;
                if (name == S_QUANTIZE_INTERPOLATOR_ID) return TYPE_QUANTIZE_INTERPOLATOR_ID;
                return -1;
            }
        };
        struct Custom {
            static constexpr const char* NAME = "Custom";
            static constexpr const char* S_INT = "integer";
            static constexpr const char* S_FLOAT = "float";
            static constexpr const char* S_COLOR = "color";
            static constexpr const char* S_STRING = "string";
            static constexpr const char* S_BOOLEAN = "boolean";
            static constexpr const char* S_DIMENSION = "dimension";
            static constexpr const char* S_REFERENCE = "reference";
            static constexpr const char* KEY_WORDS[] = {
                S_FLOAT, S_COLOR, S_STRING, S_BOOLEAN, S_DIMENSION, S_REFERENCE,
            };
            static constexpr int TYPE_INT = 900;
            static constexpr int TYPE_FLOAT = 901;
            static constexpr int TYPE_COLOR = 902;
            static constexpr int TYPE_STRING = 903;
            static constexpr int TYPE_BOOLEAN = 904;
            static constexpr int TYPE_DIMENSION = 905;
            static constexpr int TYPE_REFERENCE = 906;
            static int getId(const std::string& name) {
                if (name == S_INT) return TYPE_INT;
                if (name == S_FLOAT) return TYPE_FLOAT;
                if (name == S_COLOR) return TYPE_COLOR;
                if (name == S_STRING) return TYPE_STRING;
                if (name == S_BOOLEAN) return TYPE_BOOLEAN;
                if (name == S_DIMENSION) return TYPE_DIMENSION;
                if (name == S_REFERENCE) return TYPE_REFERENCE;
                return -1;
            }
        };
        struct MotionScene {
            static constexpr const char* NAME = "MotionScene";
            static constexpr const char* S_DEFAULT_DURATION = "defaultDuration";
            static constexpr const char* S_LAYOUT_DURING_TRANSITION = "layoutDuringTransition";
            static constexpr int TYPE_DEFAULT_DURATION = 600;
            static constexpr int TYPE_LAYOUT_DURING_TRANSITION = 601;
            static constexpr const char* KEY_WORDS[] = {
                S_DEFAULT_DURATION,
                S_LAYOUT_DURING_TRANSITION,
            };
            static int getType(int name) {
                switch (name) {
                case TYPE_DEFAULT_DURATION:
                    return INT_MASK;
                case TYPE_LAYOUT_DURING_TRANSITION:
                    return BOOLEAN_MASK;
                }
                return -1;
            }
            static int getId(const std::string& name) {
                if (name == S_DEFAULT_DURATION) return TYPE_DEFAULT_DURATION;
                if (name == S_LAYOUT_DURING_TRANSITION) return TYPE_LAYOUT_DURING_TRANSITION;
                return -1;
            }
        };
        struct TransitionType {
            static constexpr const char* NAME = "Transitions";
            static constexpr const char* S_DURATION = "duration";
            static constexpr const char* S_FROM = "from";
            static constexpr const char* S_TO = "to";
            static constexpr const char* S_PATH_MOTION_ARC = "pathMotionArc";
            static constexpr const char* S_AUTO_TRANSITION = "autoTransition";
            static constexpr const char* S_INTERPOLATOR = "motionInterpolator";
            static constexpr const char* S_STAGGERED = "staggered";
            static constexpr const char* S_TRANSITION_FLAGS = "transitionFlags";
            static constexpr int TYPE_DURATION = 700;
            static constexpr int TYPE_FROM = 701;
            static constexpr int TYPE_TO = 702;
            static constexpr int TYPE_PATH_MOTION_ARC = PositionType::TYPE_PATH_MOTION_ARC;
            static constexpr int TYPE_AUTO_TRANSITION = 704;
            static constexpr int TYPE_INTERPOLATOR = 705;
            static constexpr int TYPE_STAGGERED = 706;
            static constexpr int TYPE_TRANSITION_FLAGS = 707;
            static constexpr const char* KEY_WORDS[] = {
                S_DURATION, S_FROM, S_TO, S_PATH_MOTION_ARC, S_AUTO_TRANSITION, S_INTERPOLATOR, S_STAGGERED, S_FROM, S_TRANSITION_FLAGS,
            };
            static int getType(int name) {
                switch (name) {
                case TYPE_DURATION:
                case TYPE_PATH_MOTION_ARC:
                    return INT_MASK;
                case TYPE_FROM:
                case TYPE_TO:
                case TYPE_INTERPOLATOR:
                case TYPE_TRANSITION_FLAGS:
                    return STRING_MASK;
                case TYPE_STAGGERED:
                    return FLOAT_MASK;
                }
                return -1;
            }
            static int getId(const std::string& name) {
                if (name == S_DURATION) return TYPE_DURATION;
                if (name == S_FROM) return TYPE_FROM;
                if (name == S_TO) return TYPE_TO;
                if (name == S_PATH_MOTION_ARC) return TYPE_PATH_MOTION_ARC;
                if (name == S_AUTO_TRANSITION) return TYPE_AUTO_TRANSITION;
                if (name == S_INTERPOLATOR) return TYPE_INTERPOLATOR;
                if (name == S_STAGGERED) return TYPE_STAGGERED;
                if (name == S_TRANSITION_FLAGS) return TYPE_TRANSITION_FLAGS;
                return -1;
            }
        };
        struct OnSwipe {
            static constexpr const char* DRAG_SCALE = "dragscale";
            static constexpr const char* DRAG_THRESHOLD = "dragthreshold";
            static constexpr const char* MAX_VELOCITY = "maxvelocity";
            static constexpr const char* MAX_ACCELERATION = "maxacceleration";
            static constexpr const char* SPRING_MASS = "springmass";
            static constexpr const char* SPRING_STIFFNESS = "springstiffness";
            static constexpr const char* SPRING_DAMPING = "springdamping";
            static constexpr const char* SPRINGS_TOP_THRESHOLD = "springstopthreshold";
            static constexpr const char* DRAG_DIRECTION = "dragdirection";
            static constexpr const char* TOUCH_ANCHOR_ID = "touchanchorid";
            static constexpr const char* TOUCH_ANCHOR_SIDE = "touchanchorside";
            static constexpr const char* ROTATION_CENTER_ID = "rotationcenterid";
            static constexpr const char* TOUCH_REGION_ID = "touchregionid";
            static constexpr const char* LIMIT_BOUNDS_TO = "limitboundsto";
            static constexpr const char* MOVE_WHEN_SCROLLAT_TOP = "movewhenscrollattop";
            static constexpr const char* ON_TOUCH_UP = "ontouchup";
            static constexpr const char* ON_TOUCH_UP_ENUM[] = {
                "autoComplete", "autoCompleteToStart",   "autoCompleteToEnd",    "stop",
                "decelerate",   "decelerateAndComplete", "neverCompleteToStart", "neverCompleteToEnd"};
            static constexpr const char* SPRING_BOUNDARY = "springboundary";
            static constexpr const char* SPRING_BOUNDARY_ENUM[] = {"overshoot", "bounceStart", "bounceEnd", "bounceBoth"};
            static constexpr const char* AUTOCOMPLETE_MODE = "autocompletemode";
            static constexpr const char* AUTOCOMPLETE_MODE_ENUM[] = {"continuousVelocity", "spring"};
            static constexpr const char* NESTED_SCROLL_FLAGS = "nestedscrollflags";
            static constexpr const char* NESTED_SCROLL_FLAGS_ENUM[] = {"none", "disablePostScroll", "disableScroll", "supportScrollUp"};
        };
    };

} // namespace kiln::constraintlayout::core::motion
