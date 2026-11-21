#pragma once

// MotionLayout (constraintlayout-compose): a ConstraintLayout that animates its children
// between two ConstraintSets at a progress, along MotionLayout's Transition: paths, arcs,
// keyframes (attributes, positions, cycles), easing, stagger and custom values (colours and
// numbers the content reads), with OnSwipe to drive the progress by dragging.
//
// The scene comes from JSON5 (MotionScene(content)) or the DSL (MotionSceneScope); or the two
// sets and a Transition are given directly. Children are matched by LayoutId.
//
// Not ported: Android Studio's live editing (Registry, LayoutInformationReceiver,
// DesignInfoProvider) and the developer option that shows layout bounds.

#include "kiln/constraintlayout/ConstraintLayout.h"
#include "kiln/constraintlayout/core/state/CoreMotionScene.h"

#include "kiln/animation/Spec.h"
#include "kiln/text/TextStyle.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kiln::constraintlayout {

    using text::TextUnit;
    using ui::Color;

    // A Transition: the ids of its start and end sets, and how the children move between them.
    class Transition {
    public:
        virtual ~Transition() = default;
        virtual std::string GetStartConstraintSetId() const = 0;
        virtual std::string GetEndConstraintSetId() const = 0;
        virtual bool Equals(const Transition& other) const { return this == &other; }
    };

    // Transition(content): from JSON5 (an empty transition when it does not parse).
    std::shared_ptr<Transition> MakeTransition(const std::string& content);

    // --- The Transition DSL (TransitionScope.kt) ---------------------------------------------

    struct Easing {
        std::string name;
        static Easing Standard() { return { "standard" }; }
        static Easing Accelerate() { return { "accelerate" }; }
        static Easing Decelerate() { return { "decelerate" }; }
        static Easing Linear() { return { "linear" }; }
        static Easing Anticipate() { return { "anticipate" }; }
        static Easing Overshoot() { return { "overshoot" }; }
        static Easing Cubic(float x1, float y1, float x2, float y2);
        bool operator==(const Easing&) const = default;
    };

    struct Arc {
        std::string name;
        static Arc None() { return { "none" }; }
        static Arc StartVertical() { return { "startVertical" }; }
        static Arc StartHorizontal() { return { "startHorizontal" }; }
        static Arc Flip() { return { "flip" }; }
        static Arc Below() { return { "below" }; }
        static Arc Above() { return { "above" }; }
    };

    struct SpringBoundary {
        std::string name;
        static SpringBoundary Overshoot() { return { "overshoot" }; }
        static SpringBoundary BounceStart() { return { "bounceStart" }; }
        static SpringBoundary BounceEnd() { return { "bounceEnd" }; }
        static SpringBoundary BounceBoth() { return { "bounceBoth" }; }
    };

    // How a released swipe completes: by velocity (stop logic) or by a spring.
    struct SwipeMode {
        std::string name;
        float springMass = 1;
        float springStiffness = 400;
        float springDamping = 10;
        float springThreshold = 0.01f;
        SpringBoundary springBoundary = SpringBoundary::Overshoot();
        float maxVelocity = 4;
        float maxAcceleration = 1.2f;
        static SwipeMode Velocity(float maxVelocity = 4, float maxAcceleration = 1.2f);
        static SwipeMode Spring(float mass = 1, float stiffness = 400, float damping = 10, float threshold = 0.01f,
                                SpringBoundary boundary = SpringBoundary::Overshoot());
    };

    struct SwipeTouchUp {
        std::string name;
        static SwipeTouchUp AutoComplete() { return { "autocomplete" }; }
        static SwipeTouchUp ToStart() { return { "toStart" }; }
        static SwipeTouchUp ToEnd() { return { "toEnd" }; }
        static SwipeTouchUp Stop() { return { "stop" }; }
        static SwipeTouchUp Decelerate() { return { "decelerate" }; }
        static SwipeTouchUp NeverCompleteStart() { return { "neverCompleteStart" }; }
        static SwipeTouchUp NeverCompleteEnd() { return { "neverCompleteEnd" }; }
    };

    struct SwipeDirection {
        std::string name;
        static SwipeDirection Up() { return { "up" }; }
        static SwipeDirection Down() { return { "down" }; }
        static SwipeDirection Left() { return { "left" }; }
        static SwipeDirection Right() { return { "right" }; }
        static SwipeDirection Start() { return { "start" }; }
        static SwipeDirection End() { return { "end" }; }
        static SwipeDirection Clockwise() { return { "clockwise" }; }
        static SwipeDirection Counterclockwise() { return { "anticlockwise" }; }
    };

    struct SwipeSide {
        std::string name;
        static SwipeSide Top() { return { "top" }; }
        static SwipeSide Left() { return { "left" }; }
        static SwipeSide Right() { return { "right" }; }
        static SwipeSide Bottom() { return { "bottom" }; }
        static SwipeSide Middle() { return { "middle" }; }
        static SwipeSide Start() { return { "start" }; }
        static SwipeSide End() { return { "end" }; }
    };

    struct CurveFit {
        std::string name;
        static CurveFit Spline() { return { "spline" }; }
        static CurveFit Linear() { return { "linear" }; }
        bool operator==(const CurveFit&) const = default;
    };

    struct RelativePosition {
        std::string name;
        static RelativePosition Delta() { return { "deltaRelative" }; }
        static RelativePosition Path() { return { "pathRelative" }; }
        static RelativePosition Parent() { return { "parentRelative" }; }
        bool operator==(const RelativePosition&) const = default;
    };

    // OnSwipe: dragging `anchor` (its `side`) in `direction` drives the progress.
    struct OnSwipe {
        ConstrainedLayoutReference anchor;
        SwipeSide side;
        SwipeDirection direction;
        float dragScale = 1;
        float dragThreshold = 10;
        std::optional<ConstrainedLayoutReference> dragAround;
        std::optional<ConstrainedLayoutReference> limitBoundsTo;
        SwipeTouchUp onTouchUp = SwipeTouchUp::AutoComplete();
        SwipeMode mode = SwipeMode::Velocity();
    };

    // One frame of keyframes: the values set (in the order set) and custom values.
    class BaseKeyFrameScope {
    public:
        template<typename T>
        class Property {
        public:
            Property(BaseKeyFrameScope* scope, const char* name, T initial) : m_Scope(scope), m_Name(name), m_Value(initial) {}
            Property& operator=(T value) {
                m_Value = value;
                m_Scope->Record(m_Name, m_Value);
                return *this;
            }
            Property(const Property&) = delete;
            const T& Get() const { return m_Value; }
            operator const T&() const { return m_Value; }

        private:
            BaseKeyFrameScope* m_Scope;
            const char* m_Name;
            T m_Value;
        };

        // MotionSceneScope's KeyAttributeScope.customFloat and friends, on any frame scope.
        void CustomFloat(const std::string& name, float value);
        void CustomColor(const std::string& name, Color value);
        void CustomInt(const std::string& name, int value);
        void CustomDistance(const std::string& name, Dp value);
        void CustomFontSize(const std::string& name, TextUnit value);

        // internal
        using Value = std::variant<float, std::string>;
        void AddToContainer(core::CLContainer& container) const;

    protected:
        void Record(const char* name, float value);
        void Record(const char* name, Dp value) { Record(name, value.value); }
        void Record(const char* name, const std::optional<CurveFit>& value);

    private:
        std::vector<std::pair<std::string, Value>> m_Properties;   // LinkedHashMap
        std::vector<std::pair<std::string, Value>> m_Custom;
        static void Put(std::vector<std::pair<std::string, Value>>& map, const std::string& name, Value value);
    };

    class KeyAttributeScope : public BaseKeyFrameScope {
    public:
        Property<float> alpha{ this, "alpha", 1.0f };
        Property<float> scaleX{ this, "scaleX", 1.0f };
        Property<float> scaleY{ this, "scaleY", 1.0f };
        Property<float> rotationX{ this, "rotationX", 0.0f };
        Property<float> rotationY{ this, "rotationY", 0.0f };
        Property<float> rotationZ{ this, "rotationZ", 0.0f };
        Property<Dp> translationX{ this, "translationX", Dp(0) };
        Property<Dp> translationY{ this, "translationY", Dp(0) };
        Property<Dp> translationZ{ this, "translationZ", Dp(0) };
    };

    class KeyPositionScope : public BaseKeyFrameScope {
    public:
        Property<float> percentX{ this, "percentX", 1.0f };
        Property<float> percentY{ this, "percentY", 1.0f };
        Property<float> percentWidth{ this, "percentWidth", 1.0f };
        Property<float> percentHeight{ this, "percentHeight", 0.0f };
        Property<std::optional<CurveFit>> curveFit{ this, "curveFit", std::nullopt };
    };

    class KeyCycleScope : public BaseKeyFrameScope {
    public:
        Property<float> alpha{ this, "alpha", 1.0f };
        Property<float> scaleX{ this, "scaleX", 1.0f };
        Property<float> scaleY{ this, "scaleY", 1.0f };
        Property<float> rotationX{ this, "rotationX", 0.0f };
        Property<float> rotationY{ this, "rotationY", 0.0f };
        Property<float> rotationZ{ this, "rotationZ", 0.0f };
        Property<Dp> translationX{ this, "translationX", Dp(0) };
        Property<Dp> translationY{ this, "translationY", Dp(0) };
        Property<Dp> translationZ{ this, "translationZ", Dp(0) };
        Property<float> period{ this, "period", 0.0f };
        Property<float> offset{ this, "offset", 0.0f };
        Property<float> phase{ this, "phase", 0.0f };
    };

    // Keyframes for some targets: their frames, and an easing for them all.
    class BaseKeyFramesScope {
    public:
        template<typename T>
        class NamedProperty {
        public:
            NamedProperty(BaseKeyFramesScope* scope, const char* name, T initial) : m_Scope(scope), m_Name(name), m_Value(initial) {}
            NamedProperty& operator=(T value) {
                m_Value = value;
                m_Scope->PutName(m_Name, m_Value.name);
                return *this;
            }
            NamedProperty(const NamedProperty&) = delete;
            const T& Get() const { return m_Value; }
            operator const T&() const { return m_Value; }

        private:
            BaseKeyFramesScope* m_Scope;
            const char* m_Name;
            T m_Value;
        };

        explicit BaseKeyFramesScope(const std::vector<ConstrainedLayoutReference>& targets);
        NamedProperty<Easing> easing{ this, "transitionEasing", Easing::Standard() };

        // internal
        std::shared_ptr<core::CLObject> keyFramePropsObject;

    protected:
        void PutName(const char* name, const std::string& value);
        void AddFrame(int frame, const BaseKeyFrameScope& scope);
        std::shared_ptr<core::CLArray> m_Frames;
    };

    class KeyAttributesScope : public BaseKeyFramesScope {
    public:
        using BaseKeyFramesScope::BaseKeyFramesScope;
        void Frame(int frame, const kiln::Function<void(KeyAttributeScope&)>& keyFrameContent);
    };

    class KeyPositionsScope : public BaseKeyFramesScope {
    public:
        using BaseKeyFramesScope::BaseKeyFramesScope;
        NamedProperty<RelativePosition> type{ this, "type", RelativePosition::Delta() };
        void Frame(int frame, const kiln::Function<void(KeyPositionScope&)>& keyFrameContent);
    };

    class KeyCyclesScope : public BaseKeyFramesScope {
    public:
        using BaseKeyFramesScope::BaseKeyFramesScope;
        void Frame(int frame, const kiln::Function<void(KeyCycleScope&)>& keyFrameContent);
    };

    class TransitionScope {
    public:
        TransitionScope(std::string from, std::string to);

        Arc motionArc = Arc::None();
        // onSwipe = OnSwipe(...): onSwipe.emplace(OnSwipe{ ... }) (references are not assignable).
        std::optional<OnSwipe> onSwipe;
        // At most this much of the progress is spent staggering the children (-1..1, exclusive).
        float maxStaggerDelay = 0.0f;

        void KeyAttributes(const std::vector<ConstrainedLayoutReference>& targets, const kiln::Function<void(KeyAttributesScope&)>& content);
        void KeyPositions(const std::vector<ConstrainedLayoutReference>& targets, const kiln::Function<void(KeyPositionsScope&)>& content);
        void KeyCycles(const std::vector<ConstrainedLayoutReference>& targets, const kiln::Function<void(KeyCyclesScope&)>& content);
        ConstrainedLayoutReference CreateRefFor(const std::string& id) const { return ConstrainedLayoutReference(id); }

        // internal
        std::shared_ptr<core::CLObject> GetObject();

    private:
        std::string m_From, m_To;
        std::shared_ptr<core::CLObject> m_Container, m_KeyFrames, m_OnSwipe;
        std::shared_ptr<core::CLArray> m_KeyAttributes, m_KeyPositions, m_KeyCycles;
    };

    // Transition(from, to) { }: from the DSL.
    std::shared_ptr<Transition> MakeTransition(const std::string& from, const std::string& to, const kiln::Function<void(TransitionScope&)>& content);
    inline std::shared_ptr<Transition> MakeTransition(const kiln::Function<void(TransitionScope&)>& content) { return MakeTransition("start", "end", content); }

    // --- MotionScene ---------------------------------------------------------------------------

    class MotionScene : public core::state::CoreMotionScene {
    public:
        virtual std::shared_ptr<ConstraintSet> GetConstraintSetInstance(const std::string& name) = 0;
        virtual std::shared_ptr<Transition> GetTransitionInstance(const std::string& name) = 0;
        // A DSL scene equals one with equal sets and transitions; a JSON scene only itself.
        virtual bool Equals(const MotionScene& other) const { return this == &other; }
    };

    // MotionScene(content): from JSON5 (ConstraintSets, Transitions, Header).
    std::shared_ptr<MotionScene> MakeMotionScene(const std::string& content);
    // The composable MotionScene(content): remembered for its content.
    std::shared_ptr<MotionScene> RememberMotionScene(const std::string& content);

    struct ConstraintSetRef {
        std::string name;
        bool operator==(const ConstraintSetRef&) const = default;
    };

    class MotionSceneScope {
    public:
        void DefaultTransition(const ConstraintSetRef& from, const ConstraintSetRef& to, const kiln::Function<void(TransitionScope&)>& content = {});
        // constraintSet(name, extendConstraintSet) { }
        ConstraintSetRef ConstraintSet(const kiln::Function<void(ConstraintSetScope&)>& content, std::optional<std::string> name = std::nullopt,
                                       std::optional<ConstraintSetRef> extendConstraintSet = std::nullopt);
        // transition(from, to, name) { }
        void Transition(const ConstraintSetRef& from, const ConstraintSetRef& to, std::optional<std::string> name,
                        const kiln::Function<void(TransitionScope&)>& content);
        ConstraintSetRef AddConstraintSet(std::shared_ptr<constraintlayout::ConstraintSet> constraintSet, std::optional<std::string> name = std::nullopt);
        void AddTransition(std::shared_ptr<constraintlayout::Transition> transition, std::optional<std::string> name = std::nullopt);
        ConstrainedLayoutReference CreateRefFor(const std::string& id) const { return ConstrainedLayoutReference(id); }
        // createRefsFor destructured into `count` references; ids past those given are generated.
        std::vector<ConstrainedLayoutReference> CreateRefsFor(std::initializer_list<std::string> ids, int count = -1);

        // ConstrainScope.customFloat and friends: values MotionLayoutScope reads back.
        static void CustomFloat(ConstrainScope& scope, const std::string& name, float value);
        static void CustomColor(ConstrainScope& scope, const std::string& name, Color value);
        static void CustomInt(ConstrainScope& scope, const std::string& name, int value) { CustomFloat(scope, name, float(value)); }
        static void CustomDistance(ConstrainScope& scope, const std::string& name, Dp value) { CustomFloat(scope, name, value.value); }
        static void CustomFontSize(ConstrainScope& scope, const std::string& name, TextUnit value) { CustomFloat(scope, name, value.value); }
        // ConstrainScope.staggeredWeight: the child's share of maxStaggerDelay.
        static float StaggeredWeight(const ConstrainScope& scope);
        static void SetStaggeredWeight(ConstrainScope& scope, float value);

        // internal
        std::vector<std::pair<std::string, std::shared_ptr<constraintlayout::ConstraintSet>>> constraintSetsByName;
        std::vector<std::pair<std::string, std::shared_ptr<constraintlayout::Transition>>> transitionsByName;

    private:
        std::string NextName();
        std::string NextId();
        template<typename V>
        static void Put(std::vector<std::pair<std::string, V>>& map, const std::string& name, V value);
        int m_GeneratedCount = 0;
        int m_GeneratedIdCount = 0;
    };

    // MotionScene { }: from the DSL.
    std::shared_ptr<MotionScene> MakeMotionScene(const kiln::Function<void(MotionSceneScope&)>& content);

    // --- MotionLayout --------------------------------------------------------------------------

    // Which debug overlays to draw: the start and end bounds, the paths, the key positions.
    class DebugFlags {
    public:
        constexpr explicit DebugFlags(bool showBounds = false, bool showPaths = false, bool showKeyPositions = false)
            : m_Flags((showBounds ? 1 : 0) | (showPaths ? 2 : 0) | (showKeyPositions ? 4 : 0)) {}
        bool ShowBounds() const { return (m_Flags & 1) > 0; }
        bool ShowPaths() const { return (m_Flags & 2) > 0; }
        bool ShowKeyPositions() const { return (m_Flags & 4) > 0; }
        std::string ToString() const;
        static constexpr DebugFlags None() { return DebugFlags(); }
        // Every flag bit set: shows the same as DebugFlags(true, true, true), but is not equal to it.
        static constexpr DebugFlags All() { return DebugFlags(-1, 0); }
        bool operator==(const DebugFlags&) const = default;

    private:
        constexpr DebugFlags(int flags, int) : m_Flags(flags) {}
        int m_Flags;
    };

    // Rate-limiting helpers for InvalidationStrategy::onIncomingConstraints.
    class InvalidationStrategySpecification {
    public:
        bool ShouldInvalidateOnFixedWidth(const ui::Constraints& oldConstraints, const ui::Constraints& newConstraints, int skipCount, int threshold);
        bool ShouldInvalidateOnFixedHeight(const ui::Constraints& oldConstraints, const ui::Constraints& newConstraints, int skipCount, int threshold);

    private:
        int m_WidthRateCount = 0;
        int m_HeightRateCount = 0;
    };

    // When MotionLayout measures its children again (from both sets): when incoming constraints
    // change as onIncomingConstraints decides (by default: a different fixed size), and when
    // state read by onObservedStateChange changes (by default: whenever the content
    // recomposes). Otherwise a progress change only interpolates.
    class InvalidationStrategy {
    public:
        using IncomingConstraints = kiln::Function<bool(InvalidationStrategySpecification&, const ui::Constraints& old, const ui::Constraints& now)>;
        InvalidationStrategy(IncomingConstraints onIncomingConstraints, kiln::Function<void()> onObservedStateChange)
            : onIncomingConstraints(std::move(onIncomingConstraints)), onObservedStateChange(std::move(onObservedStateChange)) {}
        static std::shared_ptr<InvalidationStrategy> Default();

        IncomingConstraints onIncomingConstraints;
        kiln::Function<void()> onObservedStateChange;
        // internal
        InvalidationStrategySpecification scope;
    };

    namespace detail {
        class MotionMeasurer;
    }

    // What MotionLayout's content can read: custom values at the current progress, and the
    // start and end bounds of a child.
    class MotionLayoutScope {
    public:
        class CustomProperties {
        public:
            Color GetColor(const std::string& name) const;
            float GetFloat(const std::string& name) const;
            int GetInt(const std::string& name) const;
            Dp Distance(const std::string& name) const;
            TextUnit FontSize(const std::string& name) const;

        private:
            friend class MotionLayoutScope;
            CustomProperties(const MotionLayoutScope* scope, std::string id) : m_Scope(scope), m_Id(std::move(id)) {}
            const MotionLayoutScope* m_Scope;
            std::string m_Id;
        };

        // Deprecated upstream for customProperties(id); kept with its id and tag.
        class MotionProperties {
        public:
            const std::string& Id() const { return m_Id; }
            const std::optional<std::string>& Tag() const { return m_Tag; }
            Color GetColor(const std::string& name) const;
            float GetFloat(const std::string& name) const;
            int GetInt(const std::string& name) const;
            Dp Distance(const std::string& name) const;
            TextUnit FontSize(const std::string& name) const;

        private:
            friend class MotionLayoutScope;
            MotionProperties(const MotionLayoutScope* scope, std::string id, std::optional<std::string> tag)
                : m_Scope(scope), m_Id(std::move(id)), m_Tag(std::move(tag)) {}
            const MotionLayoutScope* m_Scope;
            std::string m_Id;
            std::optional<std::string> m_Tag;
        };

        // Modifier.onStartEndBoundsChanged: called on placement when the child's start or end
        // bounds changed.
        Modifier OnStartEndBoundsChanged(Modifier modifier, const std::string& layoutId,
                                         kiln::Function<void(const ui::Rect& startBounds, const ui::Rect& endBounds)> onBoundsChanged) const;

        CustomProperties GetCustomProperties(const std::string& id) const { return CustomProperties(this, id); }
        Color CustomColor(const std::string& id, const std::string& name) const;
        float CustomFloat(const std::string& id, const std::string& name) const;
        int CustomInt(const std::string& id, const std::string& name) const;
        Dp CustomDistance(const std::string& id, const std::string& name) const;
        TextUnit CustomFontSize(const std::string& id, const std::string& name) const;

        // Deprecated upstream (naming): motionProperties(id[, tag]) and motionColor .. motionFontSize.
        MotionProperties GetMotionProperties(const std::string& id, std::optional<std::string> tag = std::nullopt) const {
            return MotionProperties(this, id, std::move(tag));
        }
        Color MotionColor(const std::string& id, const std::string& name) const { return CustomColor(id, name); }
        float MotionFloat(const std::string& id, const std::string& name) const { return CustomFloat(id, name); }
        int MotionInt(const std::string& id, const std::string& name) const { return CustomInt(id, name); }
        Dp MotionDistance(const std::string& id, const std::string& name) const { return CustomDistance(id, name); }
        TextUnit MotionFontSize(const std::string& id, const std::string& name) const { return CustomFontSize(id, name); }

        // internal
        MotionLayoutScope(detail::MotionMeasurer* measurer, rt::State<float>* motionProgress) : m_Measurer(measurer), m_Progress(motionProgress) {}

    private:
        detail::MotionMeasurer* m_Measurer;
        rt::State<float>* m_Progress;
    };

    struct MotionLayoutOptions {
        DebugFlags debugFlags = DebugFlags::None();
        int optimizationLevel = core::Optimizer::OPTIMIZATION_STANDARD;
        std::shared_ptr<InvalidationStrategy> invalidationStrategy = InvalidationStrategy::Default();
    };

    // MotionLayout(start, end, progress, transition): between two sets.
    void MotionLayout(std::shared_ptr<ConstraintSet> start, std::shared_ptr<ConstraintSet> end, float progress, const Modifier& modifier,
                      kiln::Function<void(MotionLayoutScope&)> content, std::shared_ptr<Transition> transition = nullptr,
                      const MotionLayoutOptions& options = {}, rt::Key key = {});
    // MotionLayout(motionScene, progress, transitionName): a scene's transition.
    void MotionLayout(std::shared_ptr<MotionScene> motionScene, float progress, const Modifier& modifier, kiln::Function<void(MotionLayoutScope&)> content,
                      const std::string& transitionName = "default", const MotionLayoutOptions& options = {}, rt::Key key = {});
    // MotionLayout(motionScene, constraintSetName, animationSpec): animates to the named set
    // whenever it changes (from the scene's default transition).
    void MotionLayout(std::shared_ptr<MotionScene> motionScene, std::optional<std::string> constraintSetName, anim::SpecPtr animationSpec,
                      const Modifier& modifier, kiln::Function<void(MotionLayoutScope&)> content, kiln::Function<void()> finishedAnimationListener = {},
                      const MotionLayoutOptions& options = {}, rt::Key key = {});

    // --- MotionCarousel (MotionCarousel.kt) ------------------------------------------------------

    // The items a carousel shows: count and how to compose one, with or without the slot's
    // motion properties.
    class MotionCarouselScope {
    public:
        virtual ~MotionCarouselScope() = default;
        virtual void Items(int count, kiln::Function<void(int index)> itemContent) = 0;
        virtual void ItemsWithProperties(int count, kiln::Function<void(int index, const MotionLayoutScope::MotionProperties& properties)> itemContent) = 0;
    };
    template<typename T>
    void Items(MotionCarouselScope& scope, std::vector<T> items, kiln::Function<void(const T&)> itemContent) {
        auto shared = std::make_shared<std::vector<T>>(std::move(items));
        const int count = int(shared->size());
        scope.Items(count, [shared, itemContent = std::move(itemContent)](int index) { itemContent((*shared)[std::size_t(index)]); });
    }
    template<typename T>
    void ItemsWithProperties(MotionCarouselScope& scope, std::vector<T> items,
                             kiln::Function<void(const T&, const MotionLayoutScope::MotionProperties&)> itemContent) {
        auto shared = std::make_shared<std::vector<T>>(std::move(items));
        const int count = int(shared->size());
        scope.ItemsWithProperties(count, [shared, itemContent = std::move(itemContent)](int index, const MotionLayoutScope::MotionProperties& p) {
            itemContent((*shared)[std::size_t(index)], p);
        });
    }

    struct MotionCarouselOptions {
        std::string backwardTransition = "backward";
        std::string forwardTransition = "forward";
        std::string slotPrefix = "slot";
        bool showSlots = false;
    };

    // MotionCarousel: numSlots slots ("slot0".."slotN" in the scene) show consecutive items;
    // swiping horizontally runs the scene's forward or backward transition and moves the items
    // along by one. initialSlotIndex is the slot the first item starts in.
    void MotionCarousel(std::shared_ptr<MotionScene> motionScene, int initialSlotIndex, int numSlots, kiln::Function<void(MotionCarouselScope&)> content,
                        const MotionCarouselOptions& options = {}, rt::Key key = {});
    // ItemHolder: one slot (LayoutId slotPrefix + i), outlined when showSlot.
    void ItemHolder(int i, const std::string& slotPrefix, bool showSlot, kiln::Function<void()> function);

}
