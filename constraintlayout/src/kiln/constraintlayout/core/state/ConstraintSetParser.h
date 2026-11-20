#pragma once

// ConstraintSetParser (constraintlayout-core state): fills a State from a ConstraintSet in
// JSON5, as written by hand or by the Compose DSL (ConstraintLayoutBaseScope). The layout
// parts: variables, generators, helpers, guidelines, barriers, chains, flows, grids and widgets,
// with a widget's custom attributes and motion properties; and MotionLayout's MotionScene
// (ConstraintSets, Transitions, Header), a Transition's custom values and design elements.

#include "kiln/constraintlayout/core/parser/CLParser.h"
#include "kiln/constraintlayout/core/state/JavaHashMap.h"

#include <memory>
#include <string>
#include <vector>

namespace kiln::constraintlayout::core::state {

    class State;
    class ConstraintReference;
    class Dimension;
    class Transition;
    class CoreMotionScene;

    class ConstraintSetParser {
    public:
        class GeneratedValue {
        public:
            virtual ~GeneratedValue() = default;
            virtual float value() = 0;
            virtual bool isOverride() const { return false; }
        };

        class DesignElement {
        public:
            DesignElement(std::string id, std::string type, JavaHashMap<std::string> params)
                : mId(std::move(id)), mType(std::move(type)), mParams(std::move(params)) {}
            const std::string& getId() const { return mId; }
            const std::string& getType() const { return mType; }
            const JavaHashMap<std::string>& getParams() const { return mParams; }

        private:
            std::string mId;
            std::string mType;
            JavaHashMap<std::string> mParams;
        };

        class LayoutVariables {
        public:
            void put(const std::string& elementName, int element) { mMargins.put(elementName, element); }
            void put(const std::string& elementName, float start, float incrementBy);
            void put(const std::string& elementName, float from, float to, float step, const std::string& prefix, const std::string& postfix);
            void putOverride(const std::string& elementName, float value);
            float get(CLElement* elementName);
            const std::vector<std::string>* getList(const std::string& elementName) const { return mArrayIds.find(elementName); }
            void put(const std::string& elementName, std::vector<std::string> elements) { mArrayIds.put(elementName, std::move(elements)); }

        private:
            JavaHashMap<int> mMargins;
            JavaHashMap<std::shared_ptr<GeneratedValue>> mGenerators;
            JavaHashMap<std::vector<std::string>> mArrayIds;
        };

        // Logs and swallows a parse error, as upstream does.
        static void parseJSON(const std::string& content, State& state, LayoutVariables& layoutVariables);
        static void parseJSON(const std::string& content, Transition& transition, int state);
        static void parseMotionSceneJSON(CoreMotionScene& scene, const std::string& content);
        static void parseConstraintSets(CoreMotionScene& scene, const CLObject& json);
        static void override(CLObject& baseJson, const std::string& name, const CLObject& overrideValue);
        static void parseTransitions(CoreMotionScene& scene, const CLObject& json);
        static void parseHeader(CoreMotionScene& scene, const CLObject& json);
        static void parseDesignElementsJSON(const std::string& content, std::vector<DesignElement>& list);
        static void parseCustomProperties(const CLObject& element, ConstraintReference* reference, const std::string& constraintName);
        static void parseMotionProperties(CLElement* element, ConstraintReference* reference);
        static void populateState(const CLObject& parsedJson, State& state, LayoutVariables& layoutVariables);

        static void parseHelpers(State& state, LayoutVariables& layoutVariables, const CLArray& element);
        static void parseGenerate(State& state, LayoutVariables& layoutVariables, const CLObject& json);
        static void parseChain(int orientation, State& state, LayoutVariables& margins, const CLArray& helper);
        static void parseGuideline(int orientation, State& state, const CLArray& helper);
        static void parseGuidelineParams(int orientation, State& state, const std::string& guidelineId, const CLObject& params);
        static void parseBarrier(State& state, const std::string& elementName, const CLObject& element);
        static void parseWidget(State& state, LayoutVariables& layoutVariables, const std::string& elementName, const CLObject& element);
        static void parseWidget(State& state, LayoutVariables& layoutVariables, ConstraintReference* reference, const CLObject& element);
        static void applyAttribute(State& state, LayoutVariables& layoutVariables, ConstraintReference* reference, const CLObject& element,
                                   const std::string& attributeName);
        static void parseConstraint(State& state, LayoutVariables& layoutVariables, const CLObject& element, ConstraintReference* reference,
                                    const std::string& constraintName);
        static Dimension parseDimensionMode(const std::string& dimensionString);
        static Dimension parseDimension(const CLObject& element, const std::string& constraintName, State& state);
        static int64_t parseColorString(const std::string& value);
        // Empty when the element has no type (Java's null).
        static std::string lookForType(const CLObject& element);

    private:
        static void parseVariables(State& state, LayoutVariables& layoutVariables, const CLObject& json);
        static float toPix(State& state, float dp);
        static void parseChainType(const std::string& orientation, State& state, const std::string& chainName, LayoutVariables& margins,
                                   const CLObject& object);
        static void parseGridType(const std::string& gridType, State& state, const std::string& name, LayoutVariables& layoutVariables,
                                  const CLObject& element);
        static void parseFlowType(const std::string& flowType, State& state, const std::string& flowName, LayoutVariables& layoutVariables,
                                  const CLObject& element);
    };

}
