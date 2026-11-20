#include "kiln/constraintlayout/core/state/ConstraintSetParser.h"

#include "kiln/constraintlayout/core/motion/utils/TypedBundle.h"
#include "kiln/constraintlayout/core/motion/utils/TypedValues.h"
#include "kiln/constraintlayout/core/state/CoreMotionScene.h"
#include "kiln/constraintlayout/core/state/GridReference.h"
#include "kiln/constraintlayout/core/state/Transition.h"
#include "kiln/constraintlayout/core/state/StateHelpers.h"
#include "kiln/constraintlayout/core/widgets/Flow.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kiln::constraintlayout::core::state {

    namespace {

        class Generator : public ConstraintSetParser::GeneratedValue {
        public:
            Generator(float start, float incrementBy) : mIncrementBy(incrementBy), mCurrent(start) {}
            float value() override {
                if (!mStop) mCurrent += mIncrementBy;
                return mCurrent;
            }

        private:
            float mIncrementBy = 0;
            float mCurrent = 0;
            bool mStop = false;
        };

        class FiniteGenerator : public ConstraintSetParser::GeneratedValue {
        public:
            FiniteGenerator(float from, float to, float step, std::string prefix, std::string postfix)
                : mStep(step), mPrefix(std::move(prefix)), mPostfix(std::move(postfix)), mInitial(from), mMax(to) {}
            float value() override {
                if (mCurrent >= mMax) mStop = true;
                if (!mStop) mCurrent += mStep;
                return mCurrent;
            }
            std::vector<std::string> array() const {
                std::vector<std::string> array;
                int value = JavaInt(mInitial);
                int maxInt = JavaInt(mMax);
                for (int i = value; i <= maxInt; i++) {
                    array.push_back(mPrefix + std::to_string(value) + mPostfix);
                    value += JavaInt(mStep);
                }
                return array;
            }

        private:
            float mStep = 0;
            bool mStop = false;
            std::string mPrefix;
            std::string mPostfix;
            float mCurrent = 0;
            float mInitial;
            float mMax;
        };

        class OverrideValue : public ConstraintSetParser::GeneratedValue {
        public:
            explicit OverrideValue(float value) : mValue(value) {}
            float value() override { return mValue; }
            bool isOverride() const override { return true; }

        private:
            float mValue;
        };

        // Java's Math.round(float).
        int javaRound(float v) { return JavaInt(std::floor(v + 0.5f)); }

        float javaParseFloat(const std::string& s) {
            char* end = nullptr;
            float v = std::strtof(s.c_str(), &end);
            if (s.empty() || end != s.c_str() + s.size()) throw CLParsingException("For input string: \"" + s + "\"", nullptr);
            return v;
        }

        ConstraintReference* targetFor(State& state, const std::string& target) {
            return target == "parent" ? state.constraints(State::PARENT) : state.constraints(target);
        }

        void applyChainStyle(ChainReference* chain, CLElement* styleObject) {
            std::string styleValue;
            if (styleObject->is(CLElement::KIND_ARRAY) && static_cast<CLArray*>(styleObject)->size() > 1) {
                auto* array = static_cast<CLArray*>(styleObject);
                styleValue = array->getString(0);
                chain->bias(array->getFloat(1));
            } else {
                styleValue = styleObject->content();
            }
            if (styleValue == "packed") chain->style(State::Chain::PACKED);
            else if (styleValue == "spread_inside") chain->style(State::Chain::SPREAD_INSIDE);
            else chain->style(State::Chain::SPREAD);
        }

    }

    // LayoutVariables

    void ConstraintSetParser::LayoutVariables::put(const std::string& elementName, float start, float incrementBy) {
        if (auto* existing = mGenerators.find(elementName); existing && (*existing)->isOverride()) return;
        mGenerators.put(elementName, std::make_shared<Generator>(start, incrementBy));
    }

    void ConstraintSetParser::LayoutVariables::put(const std::string& elementName, float from, float to, float step, const std::string& prefix,
                                                   const std::string& postfix) {
        if (auto* existing = mGenerators.find(elementName); existing && (*existing)->isOverride()) return;
        auto generator = std::make_shared<FiniteGenerator>(from, to, step, prefix, postfix);
        mGenerators.put(elementName, generator);
        mArrayIds.put(elementName, generator->array());
    }

    void ConstraintSetParser::LayoutVariables::putOverride(const std::string& elementName, float value) {
        mGenerators.put(elementName, std::make_shared<OverrideValue>(value));
    }

    float ConstraintSetParser::LayoutVariables::get(CLElement* elementName) {
        if (elementName == nullptr) return 0.0f;
        if (elementName->is(CLElement::KIND_STRING)) {
            std::string stringValue = elementName->content();
            if (auto* generator = mGenerators.find(stringValue)) return (*generator)->value();
            if (auto* margin = mMargins.find(stringValue)) return float(*margin);
        } else if (elementName->is(CLElement::KIND_NUMBER)) {
            return elementName->getFloat();
        }
        return 0.0f;
    }

    // Parsing

    void ConstraintSetParser::parseJSON(const std::string& content, State& state, LayoutVariables& layoutVariables) {
        try {
            auto json = CLParser::parse(content);
            populateState(*json, state, layoutVariables);
        } catch (const CLParsingException& e) {
            std::fprintf(stderr, "Error parsing JSON %s\n", e.reason().c_str());
        }
    }

    void ConstraintSetParser::populateState(const CLObject& parsedJson, State& state, LayoutVariables& layoutVariables) {
        for (const std::string& elementName : parsedJson.names()) {
            CLElement* element = parsedJson.get(elementName);
            if (elementName == "Variables") {
                if (element->is(CLElement::KIND_OBJECT)) parseVariables(state, layoutVariables, *static_cast<CLObject*>(element));
            } else if (elementName == "Helpers") {
                if (element->is(CLElement::KIND_ARRAY)) parseHelpers(state, layoutVariables, *static_cast<CLArray*>(element));
            } else if (elementName == "Generate") {
                if (element->is(CLElement::KIND_OBJECT)) parseGenerate(state, layoutVariables, *static_cast<CLObject*>(element));
            } else if (element->is(CLElement::KIND_OBJECT)) {
                const auto& object = *static_cast<CLObject*>(element);
                std::string type = lookForType(object);
                if (!type.empty()) {
                    if (type == "hGuideline") parseGuidelineParams(ConstraintWidget::HORIZONTAL, state, elementName, object);
                    else if (type == "vGuideline") parseGuidelineParams(ConstraintWidget::VERTICAL, state, elementName, object);
                    else if (type == "barrier") parseBarrier(state, elementName, object);
                    else if (type == "vChain" || type == "hChain") parseChainType(type, state, elementName, layoutVariables, object);
                    else if (type == "vFlow" || type == "hFlow") parseFlowType(type, state, elementName, layoutVariables, object);
                    else if (type == "grid" || type == "row" || type == "column") parseGridType(type, state, elementName, layoutVariables, object);
                } else {
                    parseWidget(state, layoutVariables, elementName, object);
                }
            } else if (element->is(CLElement::KIND_NUMBER)) {
                layoutVariables.put(elementName, element->getInt());
            }
        }
    }

    void ConstraintSetParser::parseVariables(State& state, LayoutVariables& layoutVariables, const CLObject& json) {
        for (const std::string& elementName : json.names()) {
            CLElement* element = json.get(elementName);
            if (element->is(CLElement::KIND_NUMBER)) {
                layoutVariables.put(elementName, element->getInt());
            } else if (element->is(CLElement::KIND_OBJECT)) {
                const auto& obj = *static_cast<CLObject*>(element);
                if (obj.has("from") && obj.has("to")) {
                    float from = layoutVariables.get(obj.get("from"));
                    float to = layoutVariables.get(obj.get("to"));
                    std::string prefix = obj.getStringOrNull("prefix").value_or("");
                    std::string postfix = obj.getStringOrNull("postfix").value_or("");
                    layoutVariables.put(elementName, from, to, 1.0f, prefix, postfix);
                } else if (obj.has("from") && obj.has("step")) {
                    float start = layoutVariables.get(obj.get("from"));
                    float increment = layoutVariables.get(obj.get("step"));
                    layoutVariables.put(elementName, start, increment);
                } else if (obj.has("ids")) {
                    CLArray* ids = obj.getArray("ids");
                    std::vector<std::string> arrayIds;
                    for (int i = 0; i < ids->size(); i++) arrayIds.push_back(ids->getString(i));
                    layoutVariables.put(elementName, std::move(arrayIds));
                } else if (obj.has("tag")) {
                    const std::vector<std::string>* arrayIds = state.getIdsForTag(obj.getString("tag"));
                    layoutVariables.put(elementName, arrayIds ? *arrayIds : std::vector<std::string>{});
                }
            }
        }
    }

    void ConstraintSetParser::parseHelpers(State& state, LayoutVariables& layoutVariables, const CLArray& element) {
        for (int i = 0; i < element.size(); i++) {
            CLElement* helper = element.get(i);
            if (!helper->is(CLElement::KIND_ARRAY)) continue;
            const auto& array = *static_cast<CLArray*>(helper);
            if (array.size() <= 1) continue;
            std::string kind = array.getString(0);
            if (kind == "hChain") parseChain(ConstraintWidget::HORIZONTAL, state, layoutVariables, array);
            else if (kind == "vChain") parseChain(ConstraintWidget::VERTICAL, state, layoutVariables, array);
            else if (kind == "hGuideline") parseGuideline(ConstraintWidget::HORIZONTAL, state, array);
            else if (kind == "vGuideline") parseGuideline(ConstraintWidget::VERTICAL, state, array);
        }
    }

    void ConstraintSetParser::parseGenerate(State& state, LayoutVariables& layoutVariables, const CLObject& json) {
        for (const std::string& elementName : json.names()) {
            CLElement* element = json.get(elementName);
            const std::vector<std::string>* arrayIds = layoutVariables.getList(elementName);
            if (arrayIds != nullptr && element->is(CLElement::KIND_OBJECT)) {
                for (const std::string& id : std::vector<std::string>(*arrayIds))
                    parseWidget(state, layoutVariables, id, *static_cast<CLObject*>(element));
            }
        }
    }

    void ConstraintSetParser::parseChain(int orientation, State& state, LayoutVariables& margins, const CLArray& helper) {
        ChainReference* chain = orientation == ConstraintWidget::HORIZONTAL ? static_cast<ChainReference*>(state.horizontalChain())
                                                                            : static_cast<ChainReference*>(state.verticalChain());
        CLElement* refs = helper.get(1);
        if (!refs->is(CLElement::KIND_ARRAY) || static_cast<CLArray*>(refs)->size() < 1) return;
        auto* refArray = static_cast<CLArray*>(refs);
        for (int i = 0; i < refArray->size(); i++) chain->add(refArray->getString(i));

        if (helper.size() > 2) {
            CLElement* params = helper.get(2);
            if (!params->is(CLElement::KIND_OBJECT)) return;
            const auto& obj = *static_cast<CLObject*>(params);
            for (const std::string& constraintName : obj.names()) {
                if (constraintName == "style") applyChainStyle(chain, obj.get(constraintName));
                else parseConstraint(state, margins, obj, chain, constraintName);
            }
        }
    }

    float ConstraintSetParser::toPix(State& state, float dp) {
        auto dpToPixel = state.getDpToPixel();
        return dpToPixel ? dpToPixel(dp) : dp;
    }

    void ConstraintSetParser::parseChainType(const std::string& orientation, State& state, const std::string& chainName, LayoutVariables& margins,
                                             const CLObject& object) {
        ChainReference* chain = orientation[0] == 'h' ? static_cast<ChainReference*>(state.horizontalChain())
                                                      : static_cast<ChainReference*>(state.verticalChain());
        chain->setKey(chainName);
        for (const std::string& params : object.names()) {
            if (params == "contains") {
                CLElement* refs = object.get(params);
                if (!refs->is(CLElement::KIND_ARRAY) || static_cast<CLArray*>(refs)->size() < 1) {
                    std::fprintf(stderr, "%s contains should be an array \"%s\"\n", chainName.c_str(), refs->content().c_str());
                    return;
                }
                auto* refArray = static_cast<CLArray*>(refs);
                for (int i = 0; i < refArray->size(); i++) {
                    CLElement* chainElement = refArray->get(i);
                    if (chainElement->is(CLElement::KIND_ARRAY)) {
                        auto* array = static_cast<CLArray*>(chainElement);
                        if (array->size() > 0) {
                            std::string id = array->get(0)->content();
                            float weight = NAN, preMargin = NAN, postMargin = NAN, preGoneMargin = NAN, postGoneMargin = NAN;
                            switch (array->size()) {
                                case 2:
                                    weight = array->getFloat(1);
                                    break;
                                case 3:
                                    weight = array->getFloat(1);
                                    postMargin = preMargin = toPix(state, array->getFloat(2));
                                    break;
                                case 4:
                                    weight = array->getFloat(1);
                                    preMargin = toPix(state, array->getFloat(2));
                                    postMargin = toPix(state, array->getFloat(3));
                                    break;
                                case 6:
                                    weight = array->getFloat(1);
                                    preMargin = toPix(state, array->getFloat(2));
                                    postMargin = toPix(state, array->getFloat(3));
                                    preGoneMargin = toPix(state, array->getFloat(4));
                                    postGoneMargin = toPix(state, array->getFloat(5));
                                    break;
                            }
                            chain->addChainElement(id, weight, preMargin, postMargin, preGoneMargin, postGoneMargin);
                        }
                    } else {
                        chain->add(chainElement->content());
                    }
                }
            } else if (params == "start" || params == "end" || params == "top" || params == "bottom" || params == "left" || params == "right") {
                parseConstraint(state, margins, object, chain, params);
            } else if (params == "style") {
                applyChainStyle(chain, object.get(params));
            }
        }
    }

    namespace {
        // A flow's or grid's "padding": one value, or [start, top] or [start, top, end, bottom].
        void readPadding(CLElement* paddingObject, float& p0, float& p1, float& p2, float& p3) {
            if (paddingObject->is(CLElement::KIND_ARRAY) && static_cast<CLArray*>(paddingObject)->size() > 1) {
                auto* array = static_cast<CLArray*>(paddingObject);
                p0 = float(array->getInt(0));
                p2 = p0;
                p1 = float(array->getInt(1));
                p3 = p1;
                if (array->size() > 2) {
                    p2 = float(array->getInt(2));
                    // Upstream catches an out-of-bounds index here, but the container throws a
                    // parsing exception for a missing element, which it does not catch.
                    p3 = float(array->getInt(3));
                }
            } else {
                p0 = float(paddingObject->getInt());
                p1 = p0;
                p2 = p0;
                p3 = p0;
            }
        }
    }

    void ConstraintSetParser::parseGridType(const std::string& gridType, State& state, const std::string& name, LayoutVariables& layoutVariables,
                                            const CLObject& element) {
        GridReference* grid = state.getGrid(name, gridType);
        for (const std::string& param : element.names()) {
            if (param == "contains") {
                if (CLArray* list = element.getArrayOrNull(param)) {
                    for (int j = 0; j < list->size(); j++) {
                        std::string elementNameReference = list->get(j)->content();
                        // Upstream adds the reference itself; its key finds the same widget.
                        ConstraintReference* elementReference = state.constraints(elementNameReference);
                        grid->add(elementReference->getKey());
                    }
                }
            } else if (param == "orientation") {
                grid->setOrientation(element.get(param)->getInt());
            } else if (param == "rows") {
                int rows = element.get(param)->getInt();
                if (rows > 0) grid->setRowsSet(rows);
            } else if (param == "columns") {
                int columns = element.get(param)->getInt();
                if (columns > 0) grid->setColumnsSet(columns);
            } else if (param == "hGap") {
                grid->setHorizontalGaps(toPix(state, element.get(param)->getFloat()));
            } else if (param == "vGap") {
                grid->setVerticalGaps(toPix(state, element.get(param)->getFloat()));
            } else if (param == "spans") {
                std::string spans = element.get(param)->content();
                if (spans.find(':') != std::string::npos) grid->setSpans(spans);
            } else if (param == "skips") {
                std::string skips = element.get(param)->content();
                if (skips.find(':') != std::string::npos) grid->setSkips(skips);
            } else if (param == "rowWeights") {
                std::string rowWeights = element.get(param)->content();
                if (rowWeights.find(',') != std::string::npos) grid->setRowWeights(rowWeights);
            } else if (param == "columnWeights") {
                std::string columnWeights = element.get(param)->content();
                if (columnWeights.find(',') != std::string::npos) grid->setColumnWeights(columnWeights);
            } else if (param == "padding") {
                float paddingStart = 0, paddingTop = 0, paddingEnd = 0, paddingBottom = 0;
                readPadding(element.get(param), paddingStart, paddingTop, paddingEnd, paddingBottom);
                grid->setPaddingStart(javaRound(toPix(state, paddingStart)));
                grid->setPaddingTop(javaRound(toPix(state, paddingTop)));
                grid->setPaddingEnd(javaRound(toPix(state, paddingEnd)));
                grid->setPaddingBottom(javaRound(toPix(state, paddingBottom)));
            } else if (param == "flags") {
                int flagValue = 0;
                std::string flags;
                try {
                    CLElement* obj = element.get(param);
                    if (obj->is(CLElement::KIND_NUMBER)) flagValue = obj->getInt();
                    else flags = obj->content();
                } catch (const std::exception& ex) {
                    std::fprintf(stderr, "Error parsing grid flags %s\n", ex.what());
                }
                if (!flags.empty()) grid->setFlags(flags);
                else grid->setFlags(flagValue);
            } else {
                applyAttribute(state, layoutVariables, state.constraints(name), element, param);
            }
        }
    }

    void ConstraintSetParser::parseFlowType(const std::string& flowType, State& state, const std::string& flowName, LayoutVariables& layoutVariables,
                                            const CLObject& element) {
        bool isVertical = flowType[0] == 'v';
        FlowReference* flow = state.getFlow(flowName, isVertical);
        for (const std::string& param : element.names()) {
            if (param == "contains") {
                CLElement* refs = element.get(param);
                if (!refs->is(CLElement::KIND_ARRAY) || static_cast<CLArray*>(refs)->size() < 1) {
                    std::fprintf(stderr, "%s contains should be an array \"%s\"\n", flowName.c_str(), refs->content().c_str());
                    return;
                }
                auto* refArray = static_cast<CLArray*>(refs);
                for (int i = 0; i < refArray->size(); i++) {
                    CLElement* chainElement = refArray->get(i);
                    if (chainElement->is(CLElement::KIND_ARRAY)) {
                        auto* array = static_cast<CLArray*>(chainElement);
                        if (array->size() > 0) {
                            std::string id = array->get(0)->content();
                            float weight = NAN, preMargin = NAN, postMargin = NAN;
                            switch (array->size()) {
                                case 2:
                                    weight = array->getFloat(1);
                                    break;
                                case 3:
                                    weight = array->getFloat(1);
                                    postMargin = preMargin = toPix(state, array->getFloat(2));
                                    break;
                                case 4:
                                    weight = array->getFloat(1);
                                    preMargin = toPix(state, array->getFloat(2));
                                    postMargin = toPix(state, array->getFloat(3));
                                    break;
                            }
                            flow->addFlowElement(id, weight, preMargin, postMargin);
                        }
                    } else {
                        flow->add(chainElement->content());
                    }
                }
            } else if (param == "type") {
                flow->setOrientation(element.get(param)->content() == "hFlow" ? ConstraintWidget::HORIZONTAL : ConstraintWidget::VERTICAL);
            } else if (param == "wrap") {
                flow->setWrapMode(State::wrapValueByString(element.get(param)->content()));
            } else if (param == "vGap") {
                flow->setVerticalGap(element.get(param)->getInt());
            } else if (param == "hGap") {
                flow->setHorizontalGap(element.get(param)->getInt());
            } else if (param == "maxElement") {
                flow->setMaxElementsWrap(element.get(param)->getInt());
            } else if (param == "padding") {
                float paddingLeft = 0, paddingTop = 0, paddingRight = 0, paddingBottom = 0;
                readPadding(element.get(param), paddingLeft, paddingTop, paddingRight, paddingBottom);
                flow->setPaddingLeft(javaRound(toPix(state, paddingLeft)));
                flow->setPaddingTop(javaRound(toPix(state, paddingTop)));
                flow->setPaddingRight(javaRound(toPix(state, paddingRight)));
                flow->setPaddingBottom(javaRound(toPix(state, paddingBottom)));
            } else if (param == "vAlign") {
                std::string vAlignValue = element.get(param)->content();
                if (vAlignValue == "top") flow->setVerticalAlign(Flow::VERTICAL_ALIGN_TOP);
                else if (vAlignValue == "bottom") flow->setVerticalAlign(Flow::VERTICAL_ALIGN_BOTTOM);
                else if (vAlignValue == "baseline") flow->setVerticalAlign(Flow::VERTICAL_ALIGN_BASELINE);
                else flow->setVerticalAlign(Flow::VERTICAL_ALIGN_CENTER);
            } else if (param == "hAlign") {
                std::string hAlignValue = element.get(param)->content();
                if (hAlignValue == "start") flow->setHorizontalAlign(Flow::HORIZONTAL_ALIGN_START);
                else if (hAlignValue == "end") flow->setHorizontalAlign(Flow::HORIZONTAL_ALIGN_END);
                else flow->setHorizontalAlign(Flow::HORIZONTAL_ALIGN_CENTER);
            } else if (param == "vFlowBias" || param == "hFlowBias") {
                CLElement* biasObject = element.get(param);
                float biasValue = 0.5f, firstBiasValue = 0.5f, lastBiasValue = 0.5f;
                if (biasObject->is(CLElement::KIND_ARRAY) && static_cast<CLArray*>(biasObject)->size() > 1) {
                    auto* array = static_cast<CLArray*>(biasObject);
                    firstBiasValue = array->getFloat(0);
                    biasValue = array->getFloat(1);
                    if (array->size() > 2) lastBiasValue = array->getFloat(2);
                } else {
                    biasValue = biasObject->getFloat();
                }
                if (param == "vFlowBias") {
                    flow->verticalBias(biasValue);
                    if (firstBiasValue != 0.5f) flow->setFirstVerticalBias(firstBiasValue);
                    if (lastBiasValue != 0.5f) flow->setLastVerticalBias(lastBiasValue);
                } else {
                    flow->horizontalBias(biasValue);
                    if (firstBiasValue != 0.5f) flow->setFirstHorizontalBias(firstBiasValue);
                    if (lastBiasValue != 0.5f) flow->setLastHorizontalBias(lastBiasValue);
                }
            } else if (param == "vStyle" || param == "hStyle") {
                CLElement* styleObject = element.get(param);
                std::string styleValueStr, firstStyleValueStr, lastStyleValueStr;
                if (styleObject->is(CLElement::KIND_ARRAY) && static_cast<CLArray*>(styleObject)->size() > 1) {
                    auto* array = static_cast<CLArray*>(styleObject);
                    firstStyleValueStr = array->getString(0);
                    styleValueStr = array->getString(1);
                    if (array->size() > 2) lastStyleValueStr = array->getString(2);
                } else {
                    styleValueStr = styleObject->content();
                }
                if (param == "vStyle") {
                    if (!styleValueStr.empty()) flow->setVerticalStyle(State::chainValueByString(styleValueStr));
                    if (!firstStyleValueStr.empty()) flow->setFirstVerticalStyle(State::chainValueByString(firstStyleValueStr));
                    if (!lastStyleValueStr.empty()) flow->setLastVerticalStyle(State::chainValueByString(lastStyleValueStr));
                } else {
                    if (!styleValueStr.empty()) flow->setHorizontalStyle(State::chainValueByString(styleValueStr));
                    if (!firstStyleValueStr.empty()) flow->setFirstHorizontalStyle(State::chainValueByString(firstStyleValueStr));
                    if (!lastStyleValueStr.empty()) flow->setLastHorizontalStyle(State::chainValueByString(lastStyleValueStr));
                }
            } else {
                applyAttribute(state, layoutVariables, state.constraints(flowName), element, param);
            }
        }
    }

    void ConstraintSetParser::parseGuideline(int orientation, State& state, const CLArray& helper) {
        CLElement* params = helper.get(1);
        if (!params->is(CLElement::KIND_OBJECT)) return;
        const auto& obj = *static_cast<CLObject*>(params);
        std::optional<std::string> guidelineId = obj.getStringOrNull("id");
        if (!guidelineId) return;
        parseGuidelineParams(orientation, state, *guidelineId, obj);
    }

    void ConstraintSetParser::parseGuidelineParams(int orientation, State& state, const std::string& guidelineId, const CLObject& params) {
        std::vector<std::string> constraints = params.names();
        ConstraintReference* reference = state.constraints(guidelineId);
        if (orientation == ConstraintWidget::HORIZONTAL) state.horizontalGuideline(guidelineId);
        else state.verticalGuideline(guidelineId);
        bool isLtr = !state.isRtl() || orientation == ConstraintWidget::HORIZONTAL;
        GuidelineReference* guidelineReference = reference->getFacade()->asGuidelineReference();
        bool isPercent = false;
        float value = 0.0f;
        bool fromStart = true;
        for (const std::string& constraintName : constraints) {
            if (constraintName == "left") {
                value = toPix(state, params.getFloat(constraintName));
                fromStart = true;
            } else if (constraintName == "right") {
                value = toPix(state, params.getFloat(constraintName));
                fromStart = false;
            } else if (constraintName == "start") {
                value = toPix(state, params.getFloat(constraintName));
                fromStart = isLtr;
            } else if (constraintName == "end") {
                value = toPix(state, params.getFloat(constraintName));
                fromStart = !isLtr;
            } else if (constraintName == "percent") {
                isPercent = true;
                CLArray* percentParams = params.getArrayOrNull(constraintName);
                if (percentParams == nullptr) {
                    fromStart = true;
                    value = params.getFloat(constraintName);
                } else if (percentParams->size() > 1) {
                    std::string origin = percentParams->getString(0);
                    value = percentParams->getFloat(1);
                    if (origin == "left") fromStart = true;
                    else if (origin == "right") fromStart = false;
                    else if (origin == "start") fromStart = isLtr;
                    else if (origin == "end") fromStart = !isLtr;
                }
            }
        }
        if (isPercent) {
            guidelineReference->percent(fromStart ? value : 1.0f - value);
        } else {
            if (fromStart) guidelineReference->start(value);
            else guidelineReference->end(value);
        }
    }

    void ConstraintSetParser::parseBarrier(State& state, const std::string& elementName, const CLObject& element) {
        bool isLtr = !state.isRtl();
        BarrierReference* reference = state.barrier(elementName, State::Direction::END);
        for (const std::string& constraintName : element.names()) {
            if (constraintName == "direction") {
                std::string direction = element.getString(constraintName);
                if (direction == "start") reference->setBarrierDirection(isLtr ? State::Direction::LEFT : State::Direction::RIGHT);
                else if (direction == "end") reference->setBarrierDirection(isLtr ? State::Direction::RIGHT : State::Direction::LEFT);
                else if (direction == "left") reference->setBarrierDirection(State::Direction::LEFT);
                else if (direction == "right") reference->setBarrierDirection(State::Direction::RIGHT);
                else if (direction == "top") reference->setBarrierDirection(State::Direction::TOP);
                else if (direction == "bottom") reference->setBarrierDirection(State::Direction::BOTTOM);
            } else if (constraintName == "margin") {
                float margin = element.getFloatOrNaN(constraintName);
                if (!std::isnan(margin)) reference->margin(toPix(state, margin));
            } else if (constraintName == "contains") {
                if (CLArray* list = element.getArrayOrNull(constraintName)) {
                    for (int j = 0; j < list->size(); j++) {
                        std::string elementNameReference = list->get(j)->content();
                        // Upstream adds the reference itself; its key finds the same widget.
                        ConstraintReference* elementReference = state.constraints(elementNameReference);
                        reference->add(elementReference->getKey());
                    }
                }
            }
        }
    }

    void ConstraintSetParser::parseWidget(State& state, LayoutVariables& layoutVariables, const std::string& elementName, const CLObject& element) {
        parseWidget(state, layoutVariables, state.constraints(elementName), element);
    }

    void ConstraintSetParser::parseWidget(State& state, LayoutVariables& layoutVariables, ConstraintReference* reference, const CLObject& element) {
        for (const std::string& constraintName : element.names()) applyAttribute(state, layoutVariables, reference, element, constraintName);
    }

    void ConstraintSetParser::applyAttribute(State& state, LayoutVariables& layoutVariables, ConstraintReference* reference, const CLObject& element,
                                             const std::string& attributeName) {
        const std::string& a = attributeName;
        auto value = [&] { return layoutVariables.get(element.get(a)); };
        if (a == "width") {
            reference->setWidth(parseDimension(element, a, state));
        } else if (a == "height") {
            reference->setHeight(parseDimension(element, a, state));
        } else if (a == "center") {
            ConstraintReference* targetReference = targetFor(state, element.getString(a));
            reference->startToStart(targetReference);
            reference->endToEnd(targetReference);
            reference->topToTop(targetReference);
            reference->bottomToBottom(targetReference);
        } else if (a == "centerHorizontally") {
            ConstraintReference* targetReference = targetFor(state, element.getString(a));
            reference->startToStart(targetReference);
            reference->endToEnd(targetReference);
        } else if (a == "centerVertically") {
            ConstraintReference* targetReference = targetFor(state, element.getString(a));
            reference->topToTop(targetReference);
            reference->bottomToBottom(targetReference);
        } else if (a == "alpha") {
            reference->alpha(value());
        } else if (a == "scaleX") {
            reference->scaleX(value());
        } else if (a == "scaleY") {
            reference->scaleY(value());
        } else if (a == "translationX") {
            reference->translationX(toPix(state, value()));
        } else if (a == "translationY") {
            reference->translationY(toPix(state, value()));
        } else if (a == "translationZ") {
            reference->translationZ(toPix(state, value()));
        } else if (a == "pivotX") {
            reference->pivotX(value());
        } else if (a == "pivotY") {
            reference->pivotY(value());
        } else if (a == "rotationX") {
            reference->rotationX(value());
        } else if (a == "rotationY") {
            reference->rotationY(value());
        } else if (a == "rotationZ") {
            reference->rotationZ(value());
        } else if (a == "visibility") {
            std::string visibility = element.getString(a);
            if (visibility == "visible") {
                reference->visibility(ConstraintWidget::VISIBLE);
            } else if (visibility == "invisible") {
                reference->visibility(ConstraintWidget::INVISIBLE);
                reference->alpha(0.0f);
            } else if (visibility == "gone") {
                reference->visibility(ConstraintWidget::GONE);
            }
        } else if (a == "vBias") {
            reference->verticalBias(value());
        } else if (a == "hRtlBias") {
            float v = value();
            if (state.isRtl()) v = 1.0f - v;
            reference->horizontalBias(v);
        } else if (a == "hBias") {
            reference->horizontalBias(value());
        } else if (a == "vWeight") {
            reference->setVerticalChainWeight(value());
        } else if (a == "hWeight") {
            reference->setHorizontalChainWeight(value());
        } else if (a == "custom") {
            parseCustomProperties(element, reference, a);
        } else if (a == "motion") {
            parseMotionProperties(element.get(a), reference);
        } else {
            parseConstraint(state, layoutVariables, element, reference, a);
        }
    }

    void ConstraintSetParser::parseConstraint(State& state, LayoutVariables& layoutVariables, const CLObject& element, ConstraintReference* reference,
                                              const std::string& constraintName) {
        bool isLtr = !state.isRtl();
        CLArray* constraint = element.getArrayOrNull(constraintName);
        if (constraint != nullptr && constraint->size() > 1) {
            std::string target = constraint->getString(0);
            // Upstream switches on a null anchor (a number) and throws; nothing matches here.
            std::string anchor = constraint->getStringOrNull(1).value_or("");
            float margin = 0.0f;
            float marginGone = 0.0f;
            if (constraint->size() > 2) margin = toPix(state, layoutVariables.get(constraint->getOrNull(2)));
            if (constraint->size() > 3) marginGone = toPix(state, layoutVariables.get(constraint->getOrNull(3)));
            ConstraintReference* targetReference = targetFor(state, target);

            bool isHorizontalConstraint = false;
            bool isHorOriginLeft = true;
            bool isHorTargetLeft = true;
            if (constraintName == "circular") {
                float angle = layoutVariables.get(constraint->get(1));
                float distance = 0.0f;
                if (constraint->size() > 2) distance = toPix(state, layoutVariables.get(constraint->getOrNull(2)));
                reference->circularConstraint(targetReference, angle, distance);
            } else if (constraintName == "top") {
                if (anchor == "top") reference->topToTop(targetReference);
                else if (anchor == "bottom") reference->topToBottom(targetReference);
                else if (anchor == "baseline") {
                    state.baselineNeededFor(targetReference->getKey());
                    reference->topToBaseline(targetReference);
                }
            } else if (constraintName == "bottom") {
                if (anchor == "top") reference->bottomToTop(targetReference);
                else if (anchor == "bottom") reference->bottomToBottom(targetReference);
                else if (anchor == "baseline") {
                    state.baselineNeededFor(targetReference->getKey());
                    reference->bottomToBaseline(targetReference);
                }
            } else if (constraintName == "baseline") {
                if (anchor == "baseline") {
                    state.baselineNeededFor(reference->getKey());
                    state.baselineNeededFor(targetReference->getKey());
                    reference->baselineToBaseline(targetReference);
                } else if (anchor == "top") {
                    state.baselineNeededFor(reference->getKey());
                    reference->baselineToTop(targetReference);
                } else if (anchor == "bottom") {
                    state.baselineNeededFor(reference->getKey());
                    reference->baselineToBottom(targetReference);
                }
            } else if (constraintName == "left") {
                isHorizontalConstraint = true;
                isHorOriginLeft = true;
            } else if (constraintName == "right") {
                isHorizontalConstraint = true;
                isHorOriginLeft = false;
            } else if (constraintName == "start") {
                isHorizontalConstraint = true;
                isHorOriginLeft = isLtr;
            } else if (constraintName == "end") {
                isHorizontalConstraint = true;
                isHorOriginLeft = !isLtr;
            }

            if (isHorizontalConstraint) {
                if (anchor == "left") isHorTargetLeft = true;
                else if (anchor == "right") isHorTargetLeft = false;
                else if (anchor == "start") isHorTargetLeft = isLtr;
                else if (anchor == "end") isHorTargetLeft = !isLtr;
                if (isHorOriginLeft) {
                    if (isHorTargetLeft) reference->leftToLeft(targetReference);
                    else reference->leftToRight(targetReference);
                } else {
                    if (isHorTargetLeft) reference->rightToLeft(targetReference);
                    else reference->rightToRight(targetReference);
                }
            }
            reference->margin(margin).marginGone(marginGone);
        } else {
            std::optional<std::string> target = element.getStringOrNull(constraintName);
            if (target) {
                ConstraintReference* targetReference = targetFor(state, *target);
                if (constraintName == "start") {
                    if (isLtr) reference->leftToLeft(targetReference);
                    else reference->rightToRight(targetReference);
                } else if (constraintName == "end") {
                    if (isLtr) reference->rightToRight(targetReference);
                    else reference->leftToLeft(targetReference);
                } else if (constraintName == "top") {
                    reference->topToTop(targetReference);
                } else if (constraintName == "bottom") {
                    reference->bottomToBottom(targetReference);
                } else if (constraintName == "baseline") {
                    state.baselineNeededFor(reference->getKey());
                    state.baselineNeededFor(targetReference->getKey());
                    reference->baselineToBaseline(targetReference);
                }
            }
        }
    }

    Dimension ConstraintSetParser::parseDimensionMode(const std::string& dimensionString) {
        Dimension dimension = Dimension::createFixed(0);
        if (dimensionString == "wrap") {
            dimension = Dimension::createWrap();
        } else if (dimensionString == "preferWrap") {
            dimension = Dimension::createSuggested(Dimension::Value::WRAP_DIMENSION);
        } else if (dimensionString == "spread") {
            dimension = Dimension::createSuggested(Dimension::Value::SPREAD_DIMENSION);
        } else if (dimensionString == "parent") {
            dimension = Dimension::createParent();
        } else if (!dimensionString.empty() && dimensionString.back() == '%') {
            std::string percentString = dimensionString.substr(0, dimensionString.find('%'));
            float percentValue = javaParseFloat(percentString) / 100.0f;
            dimension = Dimension::createPercent(percentValue).suggested(0);
        } else if (dimensionString.find(':') != std::string::npos) {
            dimension = Dimension::createRatio(dimensionString).suggested(Dimension::Value::SPREAD_DIMENSION);
        }
        return dimension;
    }

    Dimension ConstraintSetParser::parseDimension(const CLObject& element, const std::string& constraintName, State& state) {
        CLElement* dimensionElement = element.get(constraintName);
        Dimension dimension = Dimension::createFixed(0);
        if (dimensionElement->is(CLElement::KIND_STRING)) {
            dimension = parseDimensionMode(dimensionElement->content());
        } else if (dimensionElement->is(CLElement::KIND_NUMBER)) {
            dimension = Dimension::createFixed(state.convertDimension(toPix(state, element.getFloat(constraintName))));
        } else if (dimensionElement->is(CLElement::KIND_OBJECT)) {
            const auto& obj = *static_cast<CLObject*>(dimensionElement);
            if (std::optional<std::string> mode = obj.getStringOrNull("value")) dimension = parseDimensionMode(*mode);
            if (CLElement* minEl = obj.getOrNull("min")) {
                if (minEl->is(CLElement::KIND_NUMBER)) dimension.min(state.convertDimension(toPix(state, minEl->getFloat())));
                else if (minEl->is(CLElement::KIND_STRING)) dimension.min(Dimension::Value::WRAP_DIMENSION);
            }
            if (CLElement* maxEl = obj.getOrNull("max")) {
                if (maxEl->is(CLElement::KIND_NUMBER)) dimension.max(state.convertDimension(toPix(state, maxEl->getFloat())));
                else if (maxEl->is(CLElement::KIND_STRING)) dimension.max(Dimension::Value::WRAP_DIMENSION);
            }
        }
        return dimension;
    }

    int64_t ConstraintSetParser::parseColorString(const std::string& value) {
        if (value.empty() || value[0] != '#') return -1;
        std::string str = value.substr(1);
        if (str.size() == 6) str = "FF" + str;
        char* end = nullptr;
        long long parsed = std::strtoll(str.c_str(), &end, 16);
        if (str.empty() || end != str.c_str() + str.size()) throw CLParsingException("For input string: \"" + str + "\"", nullptr);
        return parsed;
    }

    std::string ConstraintSetParser::lookForType(const CLObject& element) {
        for (const std::string& constraintName : element.names())
            if (constraintName == "type") return element.getString("type");
        return "";
    }


    // MotionLayout

    void ConstraintSetParser::parseJSON(const std::string& content, Transition& transition, int state) {
        try {
            std::shared_ptr<CLObject> json = CLParser::parse(content);
            for (const std::string& elementName : json->names()) {
                CLElement* base_element = json->get(elementName);
                if (!base_element->is(CLElement::KIND_OBJECT)) continue;
                auto* element = static_cast<CLObject*>(base_element);
                CLObject* customProperties = element->getObjectOrNull("custom");
                if (customProperties == nullptr) continue;
                for (const std::string& property : customProperties->names()) {
                    CLElement* value = customProperties->get(property);
                    if (value->is(CLElement::KIND_NUMBER)) {
                        transition.addCustomFloat(state, elementName, property, value->getFloat());
                    } else if (value->is(CLElement::KIND_STRING)) {
                        const int64_t color = parseColorString(value->content());
                        if (color != -1) transition.addCustomColor(state, elementName, property, int(color));
                    }
                }
            }
        } catch (const CLParsingException& e) {
            std::fprintf(stderr, "Error parsing JSON %s\n", e.what());
        }
    }

    void ConstraintSetParser::parseMotionSceneJSON(CoreMotionScene& scene, const std::string& content) {
        try {
            std::shared_ptr<CLObject> json = CLParser::parse(content);
            for (const std::string& elementName : json->names()) {
                CLElement* element = json->get(elementName);
                if (!element->is(CLElement::KIND_OBJECT)) continue;
                const auto& clObject = static_cast<const CLObject&>(*element);
                if (elementName == "ConstraintSets") parseConstraintSets(scene, clObject);
                else if (elementName == "Transitions") parseTransitions(scene, clObject);
                else if (elementName == "Header") parseHeader(scene, clObject);
            }
        } catch (const CLParsingException& e) {
            std::fprintf(stderr, "Error parsing JSON %s\n", e.what());
        }
    }

    void ConstraintSetParser::parseConstraintSets(CoreMotionScene& scene, const CLObject& json) {
        for (const std::string& csName : json.names()) {
            CLObject* constraintSet = json.getObject(csName);
            bool added = false;
            const std::optional<std::string> ext = constraintSet->getStringOrNull("Extends");
            if (ext && !ext->empty()) {
                const std::optional<std::string> base = scene.getConstraintSet(*ext);
                if (!base) continue;
                std::shared_ptr<CLObject> baseJson = CLParser::parse(*base);
                for (const std::string& widgetOverrideName : constraintSet->names()) {
                    CLElement* value = constraintSet->get(widgetOverrideName);
                    if (value->is(CLElement::KIND_OBJECT)) override(*baseJson, widgetOverrideName, static_cast<const CLObject&>(*value));
                }
                scene.setConstraintSetContent(csName, baseJson->toJSON());
                added = true;
            }
            if (!added) scene.setConstraintSetContent(csName, constraintSet->toJSON());
        }
    }

    void ConstraintSetParser::override(CLObject& baseJson, const std::string& name, const CLObject& overrideValue) {
        if (!baseJson.has(name)) {
            baseJson.put(name, overrideValue.clone());
            return;
        }
        CLObject* base = baseJson.getObject(name);
        for (const std::string& key : overrideValue.names()) {
            if (key != "clear") {
                base->put(key, overrideValue.get(key)->clone());
                continue;
            }
            CLArray* toClear = overrideValue.getArray("clear");
            for (int i = 0; i < toClear->size(); i++) {
                const std::optional<std::string> clearedKey = toClear->getStringOrNull(i);
                if (!clearedKey) continue;
                if (*clearedKey == "dimensions") {
                    base->remove("width");
                    base->remove("height");
                } else if (*clearedKey == "constraints") {
                    for (const char* k : { "start", "end", "top", "bottom", "baseline", "center", "centerHorizontally", "centerVertically" }) base->remove(k);
                } else if (*clearedKey == "transforms") {
                    for (const char* k : { "visibility", "alpha", "pivotX", "pivotY", "rotationX", "rotationY", "rotationZ", "scaleX", "scaleY",
                                           "translationX", "translationY" })
                        base->remove(k);
                } else {
                    base->remove(*clearedKey);
                }
            }
        }
    }

    void ConstraintSetParser::parseTransitions(CoreMotionScene& scene, const CLObject& json) {
        for (const std::string& elementName : json.names()) scene.setTransitionContent(elementName, json.getObject(elementName)->toJSON());
    }

    void ConstraintSetParser::parseHeader(CoreMotionScene& scene, const CLObject& json) {
        if (const std::optional<std::string> name = json.getStringOrNull("export")) scene.setDebugName(*name);
    }

    void ConstraintSetParser::parseDesignElementsJSON(const std::string& content, std::vector<DesignElement>& list) {
        std::shared_ptr<CLObject> json = CLParser::parse(content);
        std::vector<std::string> elements = json->names();
        for (std::size_t i = 0; i < elements.size(); i++) {
            const std::string elementName = elements[i];
            CLElement* element = json->get(elementName);
            if (elementName == "Design") {
                if (!element->is(CLElement::KIND_OBJECT)) return;
                auto* obj = static_cast<CLObject*>(element);
                elements = obj->names();
                for (std::size_t j = 0; j < elements.size(); j++) {
                    const std::string designElementName = elements[j];
                    auto* designElement = static_cast<CLObject*>(obj->get(designElementName));
                    std::printf("element found %s", designElementName.c_str());
                    const std::optional<std::string> type = designElement->getStringOrNull("type");
                    if (!type) continue;
                    JavaHashMap<std::string> parameters;
                    const int size = designElement->size();
                    for (int k = 0; k < size; k++) {
                        // Upstream reads entry j here, not k.
                        auto* key = static_cast<CLKey*>(designElement->get(int(j)));
                        parameters.put(key->content(), key->getValue()->content());
                    }
                    list.emplace_back(elementName, *type, std::move(parameters));
                }
            }
            break;
        }
    }

    void ConstraintSetParser::parseCustomProperties(const CLObject& element, ConstraintReference* reference, const std::string& constraintName) {
        CLObject* json = element.getObjectOrNull(constraintName);
        if (json == nullptr) return;
        for (const std::string& property : json->names()) {
            CLElement* value = json->get(property);
            if (value->is(CLElement::KIND_NUMBER)) {
                reference->addCustomFloat(property, value->getFloat());
            } else if (value->is(CLElement::KIND_STRING)) {
                const int64_t it = parseColorString(value->content());
                if (it != -1) reference->addCustomColor(property, int(it));
            }
        }
    }

    void ConstraintSetParser::parseMotionProperties(CLElement* element, ConstraintReference* reference) {
        using MT = motion::TypedValues::MotionType;
        if (element == nullptr || !element->is(CLElement::KIND_OBJECT)) return;
        auto* obj = static_cast<CLObject*>(element);
        auto bundle = std::make_shared<motion::TypedBundle>();
        for (const std::string& constraintName : obj->names()) {
            if (constraintName == "pathArc") {
                const std::string val = obj->getString(constraintName);
                static const char* const arcs[] = { "none", "startVertical", "startHorizontal", "flip", "below", "above" };
                int ord = -1;
                for (int i = 0; i < 6; i++)
                    if (val == arcs[i]) ord = i;
                if (ord == -1) {
                    std::fprintf(stderr, "%d pathArc = '%s'\n", obj->getLine(), val.c_str());
                    continue;
                }
                bundle->add(MT::TYPE_PATHMOTION_ARC, ord);
            } else if (constraintName == "relativeTo") {
                bundle->add(MT::TYPE_ANIMATE_RELATIVE_TO, obj->getString(constraintName));
            } else if (constraintName == "easing") {
                bundle->add(MT::TYPE_EASING, obj->getString(constraintName));
            } else if (constraintName == "stagger") {
                bundle->add(MT::TYPE_STAGGER, obj->getFloat(constraintName));
            } else if (constraintName == "quantize") {
                CLElement* quant = obj->get(constraintName);
                if (quant->is(CLElement::KIND_ARRAY)) {
                    auto* array = static_cast<CLArray*>(quant);
                    const int len = array->size();
                    if (len > 0) {
                        bundle->add(MT::TYPE_QUANTIZE_MOTIONSTEPS, array->getInt(0));
                        if (len > 1) {
                            bundle->add(MT::TYPE_QUANTIZE_INTERPOLATOR_TYPE, array->getString(1));
                            if (len > 2) bundle->add(MT::TYPE_QUANTIZE_MOTION_PHASE, array->getFloat(2));
                        }
                    }
                } else {
                    bundle->add(MT::TYPE_QUANTIZE_MOTIONSTEPS, obj->getInt(constraintName));
                }
            }
        }
        reference->mMotionProperties = bundle;
    }

}
