// constraintlayout-compose's MotionScene and Transition: Transition.kt (TransitionImpl),
// TransitionScope.kt (the Transition DSL), MotionScene.kt (JSONMotionScene) and
// MotionSceneScope.kt (the MotionScene DSL).

#include "kiln/constraintlayout/MotionLayout.h"
#include "kiln/constraintlayout/MotionInternal.h"

#include "kiln/constraintlayout/core/parser/CLParser.h"
#include "kiln/constraintlayout/core/state/ConstraintSetParser.h"
#include "kiln/constraintlayout/core/state/Transition.h"
#include "kiln/constraintlayout/core/state/TransitionParser.h"
#include "kiln/runtime/Composition.h"

#include <charconv>
#include <cstdio>

namespace kiln::constraintlayout {

    using core::CLArray;
    using core::CLNumber;
    using core::CLObject;
    using core::CLString;

    namespace {
        constexpr const char* UNDEFINED_NAME_PREFIX = "androidx.constraintlayout";

        // Kotlin's Float.toString.
        std::string FloatString(float v) {
            char buf[32];
            auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
            (void)ec;
            std::string s(buf, end);
            if (s.find_first_of(".eE") == std::string::npos && s.find("inf") == std::string::npos && s.find("nan") == std::string::npos) s += ".0";
            return s;
        }

        // Color.toJsonHexString: "#AARRGGBB".
        std::string ToJsonHexString(Color c) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "#%08X", unsigned(c.argb));
            return buf;
        }

        std::shared_ptr<CLObject> ParseOrNull(const std::string& content) {
            try {
                return core::CLParser::parse(content);
            } catch (const core::CLParsingException& e) {
                std::fprintf(stderr, "CML: Error parsing JSON %s\n", e.reason().c_str());
                return nullptr;
            }
        }
    }

    // --- TransitionImpl ----------------------------------------------------------------------

    namespace detail {

        TransitionImpl::TransitionImpl(std::shared_ptr<CLObject> parsedTransition) : m_Parsed(std::move(parsedTransition)) {}

        void TransitionImpl::ApplyAllTo(core::state::Transition& transition) const {
            try {
                core::state::TransitionParser::parse(*m_Parsed, transition);
            } catch (const core::CLParsingException& e) {
                std::fprintf(stderr, "CML: Error parsing JSON %s\n", e.reason().c_str());
            }
        }

        void TransitionImpl::ApplyKeyFramesTo(core::state::Transition& transition) const {
            try {
                core::state::TransitionParser::parseKeyFrames(*m_Parsed, transition);
            } catch (const core::CLParsingException& e) {
                std::fprintf(stderr, "CML: Error parsing JSON %s\n", e.reason().c_str());
            }
        }

        std::string TransitionImpl::GetStartConstraintSetId() const { return m_Parsed->getStringOrNull("from").value_or("start"); }
        std::string TransitionImpl::GetEndConstraintSetId() const { return m_Parsed->getStringOrNull("to").value_or("end"); }

        bool TransitionImpl::Equals(const Transition& other) const {
            if (this == &other) return true;
            const auto* o = dynamic_cast<const TransitionImpl*>(&other);
            return o != nullptr && m_Parsed->equals(*o->m_Parsed);
        }

        const std::shared_ptr<TransitionImpl>& TransitionImpl::Empty() {
            static const std::shared_ptr<TransitionImpl> empty = std::make_shared<TransitionImpl>(CLObject::empty());
            return empty;
        }

    }

    std::shared_ptr<Transition> MakeTransition(const std::string& content) {
        std::shared_ptr<CLObject> parsed = ParseOrNull(content);
        if (parsed == nullptr) return detail::TransitionImpl::Empty();
        return std::make_shared<detail::TransitionImpl>(std::move(parsed));
    }

    // --- The Transition DSL --------------------------------------------------------------------

    Easing Easing::Cubic(float x1, float y1, float x2, float y2) {
        return { "cubic(" + FloatString(x1) + ", " + FloatString(y1) + ", " + FloatString(x2) + ", " + FloatString(y2) + ")" };
    }

    SwipeMode SwipeMode::Velocity(float maxVelocity, float maxAcceleration) {
        SwipeMode mode;
        mode.name = "velocity";
        mode.maxVelocity = maxVelocity;
        mode.maxAcceleration = maxAcceleration;
        return mode;
    }

    SwipeMode SwipeMode::Spring(float mass, float stiffness, float damping, float threshold, SpringBoundary boundary) {
        SwipeMode mode;
        mode.name = "spring";
        mode.springMass = mass;
        mode.springStiffness = stiffness;
        mode.springDamping = damping;
        mode.springThreshold = threshold;
        mode.springBoundary = std::move(boundary);
        return mode;
    }

    void BaseKeyFrameScope::Put(std::vector<std::pair<std::string, Value>>& map, const std::string& name, Value value) {
        for (auto& [k, v] : map) {
            if (k == name) {
                v = std::move(value);
                return;
            }
        }
        map.emplace_back(name, std::move(value));
    }

    void BaseKeyFrameScope::Record(const char* name, float value) { Put(m_Properties, name, value); }

    void BaseKeyFrameScope::Record(const char* name, const std::optional<CurveFit>& value) {
        // addNameOnPropertyChange: a null value leaves the last one.
        if (value) Put(m_Properties, name, value->name);
    }

    void BaseKeyFrameScope::CustomFloat(const std::string& name, float value) { Put(m_Custom, name, value); }
    void BaseKeyFrameScope::CustomColor(const std::string& name, Color value) { Put(m_Custom, name, ToJsonHexString(value)); }
    void BaseKeyFrameScope::CustomInt(const std::string& name, int value) { Put(m_Custom, name, float(value)); }
    void BaseKeyFrameScope::CustomDistance(const std::string& name, Dp value) { Put(m_Custom, name, value.value); }
    void BaseKeyFrameScope::CustomFontSize(const std::string& name, TextUnit value) { Put(m_Custom, name, value.value); }

    void BaseKeyFrameScope::AddToContainer(core::CLContainer& container) const {
        auto putValuesAsArrayElements = [](core::CLContainer& target, const std::vector<std::pair<std::string, Value>>& source) {
            for (const auto& [name, value] : source) {
                CLArray* array = target.getArrayOrCreate(name);
                if (const std::string* s = std::get_if<std::string>(&value)) array->add(CLString::from(*s));
                else array->add(std::make_shared<CLNumber>(std::get<float>(value)));
            }
        };
        putValuesAsArrayElements(container, m_Properties);
        CLObject* customPropsObject = container.getObjectOrNull("custom");
        if (customPropsObject == nullptr) {
            auto custom = CLObject::empty();
            customPropsObject = custom.get();
            container.put("custom", std::move(custom));
        }
        putValuesAsArrayElements(*customPropsObject, m_Custom);
    }

    BaseKeyFramesScope::BaseKeyFramesScope(const std::vector<ConstrainedLayoutReference>& targets)
        : keyFramePropsObject(CLObject::empty()), m_Frames(CLArray::empty()) {
        auto targetsContainer = CLArray::empty();
        keyFramePropsObject->put("target", targetsContainer);
        keyFramePropsObject->put("frames", m_Frames);
        for (const ConstrainedLayoutReference& target : targets) targetsContainer->add(CLString::from(target.id));
    }

    void BaseKeyFramesScope::PutName(const char* name, const std::string& value) { keyFramePropsObject->putString(name, value); }

    void BaseKeyFramesScope::AddFrame(int frame, const BaseKeyFrameScope& scope) {
        m_Frames->add(std::make_shared<CLNumber>(float(frame)));
        scope.AddToContainer(*keyFramePropsObject);
    }

    void KeyAttributesScope::Frame(int frame, const kiln::Function<void(KeyAttributeScope&)>& keyFrameContent) {
        KeyAttributeScope scope;
        keyFrameContent(scope);
        AddFrame(frame, scope);
    }

    void KeyPositionsScope::Frame(int frame, const kiln::Function<void(KeyPositionScope&)>& keyFrameContent) {
        KeyPositionScope scope;
        keyFrameContent(scope);
        AddFrame(frame, scope);
    }

    void KeyCyclesScope::Frame(int frame, const kiln::Function<void(KeyCycleScope&)>& keyFrameContent) {
        KeyCycleScope scope;
        keyFrameContent(scope);
        AddFrame(frame, scope);
    }

    TransitionScope::TransitionScope(std::string from, std::string to)
        : m_From(std::move(from)), m_To(std::move(to)), m_Container(CLObject::empty()), m_KeyFrames(CLObject::empty()), m_OnSwipe(CLObject::empty()),
          m_KeyAttributes(CLArray::empty()), m_KeyPositions(CLArray::empty()), m_KeyCycles(CLArray::empty()) {}

    void TransitionScope::KeyAttributes(const std::vector<ConstrainedLayoutReference>& targets, const kiln::Function<void(KeyAttributesScope&)>& content) {
        KeyAttributesScope scope(targets);
        content(scope);
        m_Container->put("KeyFrames", m_KeyFrames);
        m_KeyFrames->put("KeyAttributes", m_KeyAttributes);
        m_KeyAttributes->add(scope.keyFramePropsObject);
    }

    void TransitionScope::KeyPositions(const std::vector<ConstrainedLayoutReference>& targets, const kiln::Function<void(KeyPositionsScope&)>& content) {
        KeyPositionsScope scope(targets);
        content(scope);
        m_Container->put("KeyFrames", m_KeyFrames);
        m_KeyFrames->put("KeyPositions", m_KeyPositions);
        m_KeyPositions->add(scope.keyFramePropsObject);
    }

    void TransitionScope::KeyCycles(const std::vector<ConstrainedLayoutReference>& targets, const kiln::Function<void(KeyCyclesScope&)>& content) {
        KeyCyclesScope scope(targets);
        content(scope);
        m_Container->put("KeyFrames", m_KeyFrames);
        m_KeyFrames->put("KeyCycles", m_KeyCycles);
        m_KeyCycles->add(scope.keyFramePropsObject);
    }

    std::shared_ptr<CLObject> TransitionScope::GetObject() {
        m_Container->putString("pathMotionArc", motionArc.name);
        m_Container->putString("from", m_From);
        m_Container->putString("to", m_To);
        m_Container->putNumber("staggered", maxStaggerDelay);
        if (onSwipe) {
            const OnSwipe& it = *onSwipe;
            m_Container->put("onSwipe", m_OnSwipe);
            m_OnSwipe->putString("direction", it.direction.name);
            m_OnSwipe->putNumber("scale", it.dragScale);
            if (it.dragAround) m_OnSwipe->putString("around", it.dragAround->id);
            if (it.limitBoundsTo) m_OnSwipe->putString("limitBounds", it.limitBoundsTo->id);
            m_OnSwipe->putNumber("threshold", it.dragThreshold);
            m_OnSwipe->putString("anchor", it.anchor.id);
            m_OnSwipe->putString("side", it.side.name);
            m_OnSwipe->putString("touchUp", it.onTouchUp.name);
            m_OnSwipe->putString("mode", it.mode.name);
            m_OnSwipe->putNumber("maxVelocity", it.mode.maxVelocity);
            m_OnSwipe->putNumber("maxAccel", it.mode.maxAcceleration);
            m_OnSwipe->putNumber("springMass", it.mode.springMass);
            m_OnSwipe->putNumber("springStiffness", it.mode.springStiffness);
            m_OnSwipe->putNumber("springDamping", it.mode.springDamping);
            m_OnSwipe->putNumber("stopThreshold", it.mode.springThreshold);
            m_OnSwipe->putString("springBoundary", it.mode.springBoundary.name);
        }
        return m_Container;
    }

    std::shared_ptr<Transition> MakeTransition(const std::string& from, const std::string& to, const kiln::Function<void(TransitionScope&)>& content) {
        TransitionScope transitionScope(from, to);
        content(transitionScope);
        return std::make_shared<detail::TransitionImpl>(transitionScope.GetObject());
    }

    // --- MotionScene ---------------------------------------------------------------------------

    namespace {

        // JSONMotionScene: the scene's constraint sets and transitions as JSON5, by name.
        class JsonMotionScene final : public MotionScene {
        public:
            explicit JsonMotionScene(const std::string& content) : m_Content(content) {
                try {
                    core::state::ConstraintSetParser::parseMotionSceneJSON(*this, content);
                } catch (const std::exception&) {
                }
            }
            void setConstraintSetContent(const std::string& name, const std::string& content) override { Put(m_ConstraintSets, name, content); }
            void setTransitionContent(const std::string& name, const std::string& content) override { Put(m_Transitions, name, content); }
            std::optional<std::string> getConstraintSet(const std::string& name) override { return Get(m_ConstraintSets, name); }
            // HashMap.values.elementAtOrNull(index): in the map's iteration order.
            std::optional<std::string> getConstraintSet(int index) override {
                std::vector<std::string> keys = m_ConstraintSets.keys();
                if (index < 0 || index >= int(keys.size())) return std::nullopt;
                return Get(m_ConstraintSets, keys[std::size_t(index)]);
            }
            std::optional<std::string> getTransition(const std::string& name) override { return Get(m_Transitions, name); }
            void setDebugName(const std::string& name) override { m_DebugName = name; }
            std::shared_ptr<ConstraintSet> GetConstraintSetInstance(const std::string& name) override {
                std::optional<std::string> content = getConstraintSet(name);
                if (!content) return nullptr;
                return MakeConstraintSet(*content);
            }
            std::shared_ptr<Transition> GetTransitionInstance(const std::string& name) override {
                std::optional<std::string> content = getTransition(name);
                if (!content) return nullptr;
                std::shared_ptr<CLObject> parsed = ParseOrNull(*content);
                if (parsed == nullptr) return nullptr;
                return std::make_shared<detail::TransitionImpl>(std::move(parsed));
            }
        private:
            static void Put(core::JavaHashMap<std::string>& map, const std::string& name, const std::string& content) { map.put(name, content); }
            static std::optional<std::string> Get(const core::JavaHashMap<std::string>& map, const std::string& name) {
                if (const std::string* v = map.find(name)) return *v;
                return std::nullopt;
            }
            std::string m_Content;
            std::string m_DebugName;
            core::JavaHashMap<std::string> m_ConstraintSets;
            core::JavaHashMap<std::string> m_Transitions;
        };

        // MotionSceneDslImpl: the DSL's sets and transitions, by name.
        class MotionSceneDslImpl final : public MotionScene {
        public:
            MotionSceneDslImpl(std::vector<std::pair<std::string, std::shared_ptr<ConstraintSet>>> constraintSets,
                               std::vector<std::pair<std::string, std::shared_ptr<Transition>>> transitions)
                : m_ConstraintSets(std::move(constraintSets)), m_Transitions(std::move(transitions)) {}
            void setTransitionContent(const std::string&, const std::string&) override {}
            std::optional<std::string> getConstraintSet(const std::string&) override { return std::string(); }
            void setConstraintSetContent(const std::string&, const std::string&) override {}
            void setDebugName(const std::string&) override {}
            std::optional<std::string> getTransition(const std::string&) override { return std::string(); }
            std::optional<std::string> getConstraintSet(int) override { return std::string(); }
            std::shared_ptr<ConstraintSet> GetConstraintSetInstance(const std::string& name) override {
                for (const auto& [k, v] : m_ConstraintSets)
                    if (k == name) return v;
                return nullptr;
            }
            std::shared_ptr<Transition> GetTransitionInstance(const std::string& name) override {
                for (const auto& [k, v] : m_Transitions)
                    if (k == name) return v;
                return nullptr;
            }

            // HashMap equality: the same names, with equal sets and transitions.
            bool Equals(const MotionScene& other) const override {
                if (this == &other) return true;
                const auto* o = dynamic_cast<const MotionSceneDslImpl*>(&other);
                return o != nullptr && SameEntries(m_ConstraintSets, o->m_ConstraintSets) && SameEntries(m_Transitions, o->m_Transitions);
            }

        private:
            template<typename V>
            static bool SameEntries(const std::vector<std::pair<std::string, std::shared_ptr<V>>>& a, const std::vector<std::pair<std::string, std::shared_ptr<V>>>& b) {
                if (a.size() != b.size()) return false;
                for (const auto& [name, value] : a) {
                    bool found = false;
                    for (const auto& [otherName, otherValue] : b) {
                        if (otherName != name) continue;
                        found = value == otherValue || (value && otherValue && value->Equals(*otherValue));
                        break;
                    }
                    if (!found) return false;
                }
                return true;
            }
            std::vector<std::pair<std::string, std::shared_ptr<ConstraintSet>>> m_ConstraintSets;
            std::vector<std::pair<std::string, std::shared_ptr<Transition>>> m_Transitions;
        };

    }

    std::shared_ptr<MotionScene> MakeMotionScene(const std::string& content) { return std::make_shared<JsonMotionScene>(content); }

    std::shared_ptr<MotionScene> RememberMotionScene(const std::string& content) {
        auto* c = rt::Composition::Current();
        return c->Remember<std::shared_ptr<MotionScene>>([&] { return MakeMotionScene(content); }, rt::Keys(content));
    }

    std::string MotionSceneScope::NextName() { return UNDEFINED_NAME_PREFIX + std::to_string(m_GeneratedCount++); }
    std::string MotionSceneScope::NextId() { return std::string(UNDEFINED_NAME_PREFIX) + "id" + std::to_string(m_GeneratedIdCount++); }

    template<typename V>
    void MotionSceneScope::Put(std::vector<std::pair<std::string, V>>& map, const std::string& name, V value) {
        for (auto& [k, v] : map) {
            if (k == name) {
                v = std::move(value);
                return;
            }
        }
        map.emplace_back(name, std::move(value));
    }

    void MotionSceneScope::DefaultTransition(const ConstraintSetRef& from, const ConstraintSetRef& to, const kiln::Function<void(TransitionScope&)>& content) {
        Transition(from, to, std::string("default"), content);
    }

    ConstraintSetRef MotionSceneScope::ConstraintSet(const kiln::Function<void(ConstraintSetScope&)>& content, std::optional<std::string> name,
                                                     std::optional<ConstraintSetRef> extendConstraintSet) {
        std::shared_ptr<constraintlayout::ConstraintSet> extendFrom;
        if (extendConstraintSet) {
            for (const auto& [k, v] : constraintSetsByName)
                if (k == extendConstraintSet->name) extendFrom = v;
        }
        return AddConstraintSet(MakeConstraintSet(content, std::move(extendFrom)), std::move(name));
    }

    void MotionSceneScope::Transition(const ConstraintSetRef& from, const ConstraintSetRef& to, std::optional<std::string> name,
                                      const kiln::Function<void(TransitionScope&)>& content) {
        const std::string transitionName = name ? *name : NextName();
        TransitionScope scope(from.name, to.name);
        if (content) content(scope);
        Put<std::shared_ptr<constraintlayout::Transition>>(transitionsByName, transitionName, std::make_shared<detail::TransitionImpl>(scope.GetObject()));
    }

    ConstraintSetRef MotionSceneScope::AddConstraintSet(std::shared_ptr<constraintlayout::ConstraintSet> constraintSet, std::optional<std::string> name) {
        const std::string cSetName = name ? *name : NextName();
        Put(constraintSetsByName, cSetName, std::move(constraintSet));
        return ConstraintSetRef{ cSetName };
    }

    void MotionSceneScope::AddTransition(std::shared_ptr<constraintlayout::Transition> transition, std::optional<std::string> name) {
        const std::string transitionName = name ? *name : NextName();
        Put(transitionsByName, transitionName, std::move(transition));
    }

    std::vector<ConstrainedLayoutReference> MotionSceneScope::CreateRefsFor(std::initializer_list<std::string> ids, int count) {
        std::vector<ConstrainedLayoutReference> refs;
        for (const std::string& id : ids) refs.emplace_back(id);
        while (count >= 0 && int(refs.size()) < count) refs.emplace_back(NextId());
        return refs;
    }

    void MotionSceneScope::CustomFloat(ConstrainScope& scope, const std::string& name, float value) {
        CLObject* container = scope.ContainerObject();
        if (!container->has("custom")) container->put("custom", CLObject::empty());
        if (CLObject* customPropsObject = container->getObjectOrNull("custom")) customPropsObject->putNumber(name, value);
    }

    void MotionSceneScope::CustomColor(ConstrainScope& scope, const std::string& name, Color value) {
        CLObject* container = scope.ContainerObject();
        if (!container->has("custom")) container->put("custom", CLObject::empty());
        if (CLObject* customPropsObject = container->getObjectOrNull("custom")) customPropsObject->putString(name, ToJsonHexString(value));
    }

    float MotionSceneScope::StaggeredWeight(const ConstrainScope& scope) {
        CLObject* container = scope.ContainerObject();
        if (!container->has("motion")) return NAN;
        return container->getObject("motion")->getFloatOrNaN("stagger");
    }

    void MotionSceneScope::SetStaggeredWeight(ConstrainScope& scope, float value) {
        CLObject* container = scope.ContainerObject();
        if (!container->has("motion")) container->put("motion", CLObject::empty());
        if (CLObject* motionPropsObject = container->getObjectOrNull("motion")) motionPropsObject->putNumber("stagger", value);
    }

    std::shared_ptr<MotionScene> MakeMotionScene(const kiln::Function<void(MotionSceneScope&)>& content) {
        MotionSceneScope scope;
        content(scope);
        return std::make_shared<MotionSceneDslImpl>(std::move(scope.constraintSetsByName), std::move(scope.transitionsByName));
    }

}
