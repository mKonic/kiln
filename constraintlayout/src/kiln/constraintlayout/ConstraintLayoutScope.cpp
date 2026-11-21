// constraintlayout-compose's DSL: ConstraintLayoutBaseScope, ConstrainScope, the chain
// scopes, references, Dimension and the grid types (ConstraintLayoutBaseScope.kt,
// ConstrainScope.kt, ChainConstrainScope.kt, ConstraintScopeCommon.kt, ConstraintLayout.kt).

#include "kiln/constraintlayout/ConstraintLayout.h"

#include "kiln/constraintlayout/core/state/ConstraintSetParser.h"
#include "kiln/constraintlayout/core/widgets/GridCore.h"

#include "kiln/constraintlayout/core/state/JavaHashMap.h"

#include <charconv>
#include <cstring>
#include <stdexcept>

namespace kiln::constraintlayout {

    using core::CLArray;
    using core::CLNumber;
    using core::CLObject;
    using core::CLString;

    namespace {

        // Kotlin's Float.toString, close enough for values the parser reads back.
        std::string FloatString(float v) {
            char buf[32];
            auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
            (void)ec;
            std::string s(buf, end);
            if (s.find_first_of(".eE") == std::string::npos && s.find("inf") == std::string::npos && s.find("nan") == std::string::npos) s += ".0";
            return s;
        }

        std::string JoinFloats(const std::vector<float>& values) {
            std::string out;
            for (std::size_t i = 0; i < values.size(); i++) out += (i ? "," : "") + FloatString(values[i]);
            return out;
        }

        // Java's Float.hashCode / Dp.hashCode: the float's bits.
        int FloatHash(float v) {
            if (v != v) return 0x7fc00000;
            int bits;
            std::memcpy(&bits, &v, sizeof bits);
            return bits;
        }

        int StringHash(const std::string& s) { return core::javaStringHash(s); }

        std::string HorizontalAnchorName(int index) {
            switch (index) {
                case 0: return "top";
                case 1: return "bottom";
                default: return "top";
            }
        }

        std::string VerticalAnchorName(int index) {
            switch (index) {
                case -2: return "start";
                case -1: return "end";
                case 0: return "left";
                case 1: return "right";
                default: return "start";
            }
        }

        std::shared_ptr<CLArray> ConstraintArray(const std::string& id, const std::string& anchor, Dp margin, Dp goneMargin) {
            auto array = CLArray::empty();
            array->add(CLString::from(id));
            array->add(CLString::from(anchor));
            array->add(std::make_shared<CLNumber>(margin.value));
            array->add(std::make_shared<CLNumber>(goneMargin.value));
            return array;
        }

    }

    // Dimension

    core::CLElementPtr Dimension::Symbol::AsCLElement() const {
        if (value) return std::make_shared<CLNumber>(*value);
        if (!symbol.empty()) return CLString::from(symbol);
        return CLString::from("wrap");   // upstream logs and falls back to wrap
    }

    Dimension Dimension::PreferredValue(Dp dp) {
        Dimension d;
        d.m_Value.symbol = "spread";
        d.m_Max.value = dp.value;
        return d;
    }
    Dimension Dimension::Value(Dp dp) {
        Dimension d;
        d.m_Value.value = dp.value;
        return d;
    }
    Dimension Dimension::Ratio(const std::string& ratio) {
        Dimension d;
        d.m_Value.symbol = ratio;
        return d;
    }
    Dimension Dimension::PreferredWrapContent() {
        Dimension d;
        d.m_Value.symbol = "preferWrap";
        return d;
    }
    Dimension Dimension::WrapContent() {
        Dimension d;
        d.m_Value.symbol = "wrap";
        return d;
    }
    Dimension Dimension::MatchParent() {
        Dimension d;
        d.m_Value.symbol = "parent";
        return d;
    }
    Dimension Dimension::FillToConstraints() {
        Dimension d;
        d.m_Value.symbol = "spread";
        return d;
    }
    Dimension Dimension::Percent(float percent) {
        Dimension d;
        d.m_Value.symbol = FloatString(percent * 100.0f) + "%";
        return d;
    }
    Dimension Dimension::AtLeast(Dp dp) const {
        Dimension d = *this;
        d.m_Min = { dp.value, {} };
        return d;
    }
    Dimension Dimension::AtMost(Dp dp) const {
        Dimension d = *this;
        d.m_Max = { dp.value, {} };
        return d;
    }
    Dimension Dimension::AtLeastWrapContent() const {
        Dimension d = *this;
        d.m_Min = { std::nullopt, "wrap" };
        return d;
    }
    Dimension Dimension::AtMostWrapContent() const {
        Dimension d = *this;
        d.m_Max = { std::nullopt, "wrap" };
        return d;
    }

    core::CLElementPtr Dimension::AsCLElement() const {
        if (m_Min.IsUndefined() && m_Max.IsUndefined()) return m_Value.AsCLElement();
        auto obj = CLObject::empty();
        if (!m_Min.IsUndefined()) obj->put("min", m_Min.AsCLElement());
        if (!m_Max.IsUndefined()) obj->put("max", m_Max.AsCLElement());
        obj->put("value", m_Value.AsCLElement());
        return obj;
    }

    // References

    LayoutReference VerticalAnchor::reference() const { return LayoutReference(id); }
    LayoutReference HorizontalAnchor::reference() const { return LayoutReference(id); }
    LayoutReference BaselineAnchor::reference() const { return LayoutReference(id); }

    ConstrainedLayoutReference::ConstrainedLayoutReference(std::string id)
        : LayoutReference(std::move(id)), start{ this->id, -2 }, absoluteLeft{ this->id, 0 }, end{ this->id, -1 }, absoluteRight{ this->id, 1 },
          top{ this->id, 0 }, bottom{ this->id, 1 }, baseline{ this->id } {}

    HorizontalChainReference::HorizontalChainReference(std::string id)
        : LayoutReference(std::move(id)), start{ this->id, -2 }, absoluteLeft{ this->id, 0 }, end{ this->id, -1 }, absoluteRight{ this->id, 1 } {}

    VerticalChainReference::VerticalChainReference(std::string id) : LayoutReference(std::move(id)), top{ this->id, 0 }, bottom{ this->id, 1 } {}

    // Grid types

    GridFlag::GridFlag(bool placeLayoutsOnSpansFirst, bool subGridByColRow)
        : value((placeLayoutsOnSpansFirst ? 0 : core::GridCore::SPANS_RESPECT_WIDGET_ORDER) |
                (subGridByColRow ? core::GridCore::SUB_GRID_BY_COL_ROW : 0)) {}
    bool GridFlag::IsPlaceLayoutsOnSpansFirst() const { return (value & core::GridCore::SPANS_RESPECT_WIDGET_ORDER) == 0; }
    bool GridFlag::IsSubGridByColRow() const { return (value & core::GridCore::SUB_GRID_BY_COL_ROW) > 0; }

    Skip::Skip(int position, int rows, int columns)
        : description(std::to_string(position) + ":" + std::to_string(rows) + "x" + std::to_string(columns)) {}
    Skip::Skip(int position, int size) : description(std::to_string(position) + ":" + std::to_string(size)) {}
    Span::Span(int position, int rows, int columns)
        : description(std::to_string(position) + ":" + std::to_string(rows) + "x" + std::to_string(columns)) {}
    Span::Span(int position, int size) : description(std::to_string(position) + ":" + std::to_string(size)) {}

    // Anchorables

    VerticalAnchorable::VerticalAnchorable(CLObject* container, int index) : m_Container(container), m_AnchorName(VerticalAnchorName(index)) {}

    void VerticalAnchorable::LinkTo(const VerticalAnchor& anchor, Dp margin, Dp goneMargin) {
        m_Container->put(m_AnchorName, ConstraintArray(anchor.id, VerticalAnchorName(anchor.index), margin, goneMargin));
    }

    HorizontalAnchorable::HorizontalAnchorable(CLObject* container, int index) : m_Container(container), m_AnchorName(HorizontalAnchorName(index)) {}

    void HorizontalAnchorable::LinkTo(const HorizontalAnchor& anchor, Dp margin, Dp goneMargin) {
        m_Container->put(m_AnchorName, ConstraintArray(anchor.id, HorizontalAnchorName(anchor.index), margin, goneMargin));
    }

    void HorizontalAnchorable::LinkTo(const BaselineAnchor& anchor, Dp margin, Dp goneMargin) {
        m_Container->put(m_AnchorName, ConstraintArray(anchor.id, "baseline", margin, goneMargin));
    }

    void BaselineAnchorable::LinkTo(const BaselineAnchor& anchor, Dp margin, Dp goneMargin) {
        m_Container->put("baseline", ConstraintArray(anchor.id, "baseline", margin, goneMargin));
    }

    void BaselineAnchorable::LinkTo(const HorizontalAnchor& anchor, Dp margin, Dp goneMargin) {
        m_Container->put("baseline", ConstraintArray(anchor.id, HorizontalAnchorName(anchor.index), margin, goneMargin));
    }

    // ConstrainScope

    ConstrainScope::ConstrainScope(std::string i, CLObject* container)
        : id(std::move(i)), start(container, -2), absoluteLeft(container, 0), top(container, 0), end(container, -1), absoluteRight(container, 1),
          bottom(container, 1), baseline(container), m_Container(container) {}

    void ConstrainScope::Write(const char* name, const Dimension& d) { m_Container->put(name, d.AsCLElement()); }
    void ConstrainScope::Write(const char* name, const Visibility& v) { m_Container->putString(name, v.name); }
    void ConstrainScope::Write(const char* name, float f) {
        if (f == f) m_Container->putNumber(name, f);
    }
    void ConstrainScope::Write(const char* name, Dp dp) {
        if (dp.value == dp.value) m_Container->putNumber(name, dp.value);
    }

    void ConstrainScope::LinkTo(const VerticalAnchor& s, const VerticalAnchor& e, Dp startMargin, Dp endMargin, Dp startGoneMargin, Dp endGoneMargin,
                                float bias) {
        start.LinkTo(s, startMargin, startGoneMargin);
        end.LinkTo(e, endMargin, endGoneMargin);
        m_Container->putNumber("hRtlBias", bias);
    }

    void ConstrainScope::LinkTo(const HorizontalAnchor& t, const HorizontalAnchor& b, Dp topMargin, Dp bottomMargin, Dp topGoneMargin,
                                Dp bottomGoneMargin, float bias) {
        top.LinkTo(t, topMargin, topGoneMargin);
        bottom.LinkTo(b, bottomMargin, bottomGoneMargin);
        m_Container->putNumber("vBias", bias);
    }

    void ConstrainScope::LinkTo(const VerticalAnchor& s, const HorizontalAnchor& t, const VerticalAnchor& e, const HorizontalAnchor& b,
                                Dp startMargin, Dp topMargin, Dp endMargin, Dp bottomMargin, Dp startGoneMargin, Dp topGoneMargin,
                                Dp endGoneMargin, Dp bottomGoneMargin, float hBias, float vBias) {
        LinkTo(s, e, startMargin, endMargin, startGoneMargin, endGoneMargin, hBias);
        LinkTo(t, b, topMargin, bottomMargin, topGoneMargin, bottomGoneMargin, vBias);
    }

    void ConstrainScope::CenterTo(const ConstrainedLayoutReference& other) { LinkTo(other.start, other.top, other.end, other.bottom); }
    void ConstrainScope::CenterHorizontallyTo(const ConstrainedLayoutReference& other, float bias) {
        LinkTo(other.start, other.end, Dp(0), Dp(0), Dp(0), Dp(0), bias);
    }
    void ConstrainScope::CenterVerticallyTo(const ConstrainedLayoutReference& other, float bias) {
        LinkTo(other.top, other.bottom, Dp(0), Dp(0), Dp(0), Dp(0), bias);
    }
    void ConstrainScope::CenterAround(const VerticalAnchor& anchor) { LinkTo(anchor, anchor); }
    void ConstrainScope::CenterAround(const HorizontalAnchor& anchor) { LinkTo(anchor, anchor); }

    void ConstrainScope::Circular(const ConstrainedLayoutReference& other, float angle, Dp distance) {
        auto params = CLArray::empty();
        params->add(CLString::from(other.id));
        params->add(std::make_shared<CLNumber>(angle));
        params->add(std::make_shared<CLNumber>(distance.value));
        m_Container->put("circular", params);
    }

    void ConstrainScope::ClearHorizontal() {
        for (const char* n : { "left", "right", "start", "end" }) m_Container->remove(n);
    }
    void ConstrainScope::ClearVertical() {
        for (const char* n : { "top", "bottom", "baseline" }) m_Container->remove(n);
    }
    void ConstrainScope::ClearConstraints() {
        ClearHorizontal();
        ClearVertical();
        m_Container->remove("circular");
    }
    void ConstrainScope::ResetDimensions() {
        width = Dimension::WrapContent();
        height = Dimension::WrapContent();
    }
    void ConstrainScope::ResetTransforms() {
        for (const char* n : { "alpha", "scaleX", "scaleY", "rotationX", "rotationY", "rotationZ", "translationX", "translationY", "translationZ",
                               "pivotX", "pivotY" })
            m_Container->remove(n);
    }

    HorizontalChainScope::HorizontalChainScope(std::string i, CLObject* container)
        : id(std::move(i)), start(container, -2), absoluteLeft(container, 0), end(container, -1), absoluteRight(container, 1) {}

    VerticalChainScope::VerticalChainScope(std::string i, CLObject* container) : id(std::move(i)), top(container, 0), bottom(container, 1) {}

    // ConstraintLayoutBaseScope

    ConstraintLayoutBaseScope::ConstraintLayoutBaseScope(std::shared_ptr<CLObject> extendFrom)
        : m_ContainerObject(extendFrom ? extendFrom->cloneObject() : CLObject::empty()) {}

    void ConstraintLayoutBaseScope::ApplyTo(core::state::State& state) const {
        core::state::ConstraintSetParser::LayoutVariables variables;
        core::state::ConstraintSetParser::populateState(*m_ContainerObject, state, variables);
    }

    void ConstraintLayoutBaseScope::Reset() {
        m_ContainerObject->clear();
        m_HelperId = kHelpersStartId;
        m_HelpersHashCode = 0;
    }

    CLObject* ConstraintLayoutBaseScope::AsCLContainer(const LayoutReference& ref) {
        if (m_ContainerObject->getObjectOrNull(ref.id) == nullptr) m_ContainerObject->put(ref.id, CLObject::empty());
        return m_ContainerObject->getObject(ref.id);
    }

    bool ConstraintLayoutBaseScope::operator==(const ConstraintLayoutBaseScope& o) const {
        return this == &o || m_ContainerObject->equals(*o.m_ContainerObject);
    }

    HorizontalChainScope ConstraintLayoutBaseScope::Constrain(const HorizontalChainReference& ref, const kiln::Function<void(HorizontalChainScope&)>& block) {
        HorizontalChainScope scope(ref.id, AsCLContainer(ref));
        block(scope);
        return scope;
    }

    VerticalChainScope ConstraintLayoutBaseScope::Constrain(const VerticalChainReference& ref, const kiln::Function<void(VerticalChainScope&)>& block) {
        VerticalChainScope scope(ref.id, AsCLContainer(ref));
        block(scope);
        return scope;
    }

    void ConstraintLayoutBaseScope::Constrain(const ConstrainedLayoutReference& ref, const kiln::Function<void(ConstrainScope&)>& block) {
        ConstrainScope scope(ref.id, AsCLContainer(ref));
        block(scope);
    }

    void ConstraintLayoutBaseScope::Constrain(std::initializer_list<const ConstrainedLayoutReference*> refs,
                                              const kiln::Function<void(ConstrainScope&)>& block) {
        for (const auto* ref : refs) Constrain(*ref, block);
    }

    VerticalAnchor ConstraintLayoutBaseScope::CreateVerticalGuideline(const char* key, float value, int hashKind, int hash) {
        LayoutReference ref(CreateHelperId());
        CLObject* c = AsCLContainer(ref);
        c->putString("type", "vGuideline");
        c->putNumber(key, value);
        UpdateHelpersHashCode(hashKind);
        UpdateHelpersHashCode(hash);
        return { ref.id, 0 };
    }

    VerticalAnchor ConstraintLayoutBaseScope::CreateVerticalPercentGuideline(const char* origin, float fraction, int hashKind) {
        LayoutReference ref(CreateHelperId());
        auto percentParams = CLArray::empty();
        percentParams->add(CLString::from(origin));
        percentParams->add(std::make_shared<CLNumber>(fraction));
        CLObject* c = AsCLContainer(ref);
        c->putString("type", "vGuideline");
        c->put("percent", percentParams);
        UpdateHelpersHashCode(hashKind);
        UpdateHelpersHashCode(FloatHash(fraction));
        return { ref.id, 0 };
    }

    HorizontalAnchor ConstraintLayoutBaseScope::CreateHorizontalGuideline(const char* key, float value, int hashKind, int hash) {
        LayoutReference ref(CreateHelperId());
        CLObject* c = AsCLContainer(ref);
        c->putString("type", "hGuideline");
        c->putNumber(key, value);
        UpdateHelpersHashCode(hashKind);
        UpdateHelpersHashCode(hash);
        return { ref.id, 0 };
    }

    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromStart(Dp offset) { return CreateVerticalGuideline("start", offset.value, 1, FloatHash(offset.value)); }
    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromAbsoluteLeft(Dp offset) {
        return CreateVerticalGuideline("left", offset.value, 2, FloatHash(offset.value));
    }
    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromStart(float fraction) { return CreateVerticalPercentGuideline("start", fraction, 3); }
    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromAbsoluteLeft(float fraction) {
        return CreateVerticalGuideline("percent", fraction, 4, FloatHash(fraction));
    }
    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromEnd(Dp offset) { return CreateVerticalGuideline("end", offset.value, 5, FloatHash(offset.value)); }
    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromAbsoluteRight(Dp offset) {
        return CreateVerticalGuideline("right", offset.value, 6, FloatHash(offset.value));
    }
    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromEnd(float fraction) { return CreateVerticalPercentGuideline("end", fraction, 3); }
    VerticalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromAbsoluteRight(float fraction) { return CreateGuidelineFromAbsoluteLeft(1.0f - fraction); }
    HorizontalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromTop(Dp offset) {
        return CreateHorizontalGuideline("start", offset.value, 7, FloatHash(offset.value));
    }
    HorizontalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromTop(float fraction) {
        return CreateHorizontalGuideline("percent", fraction, 8, FloatHash(fraction));
    }
    HorizontalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromBottom(Dp offset) {
        return CreateHorizontalGuideline("end", offset.value, 9, FloatHash(offset.value));
    }
    HorizontalAnchor ConstraintLayoutBaseScope::CreateGuidelineFromBottom(float fraction) { return CreateGuidelineFromTop(1.0f - fraction); }

    std::string ConstraintLayoutBaseScope::CreateBarrier(const char* direction, Refs elements, Dp margin, int hashKind) {
        LayoutReference ref(CreateHelperId());
        auto elementArray = CLArray::empty();
        for (const LayoutReference* e : elements) elementArray->add(CLString::from(e->id));
        CLObject* c = AsCLContainer(ref);
        c->putString("type", "barrier");
        c->putString("direction", direction);
        c->putNumber("margin", margin.value);
        c->put("contains", elementArray);
        UpdateHelpersHashCode(hashKind);
        for (const LayoutReference* e : elements) UpdateHelpersHashCode(StringHash(e->id));
        UpdateHelpersHashCode(FloatHash(margin.value));
        return ref.id;
    }

    VerticalAnchor ConstraintLayoutBaseScope::CreateStartBarrier(Refs elements, Dp margin) { return { CreateBarrier("start", elements, margin, 10), 0 }; }
    VerticalAnchor ConstraintLayoutBaseScope::CreateAbsoluteLeftBarrier(Refs elements, Dp margin) {
        return { CreateBarrier("left", elements, margin, 11), 0 };
    }
    HorizontalAnchor ConstraintLayoutBaseScope::CreateTopBarrier(Refs elements, Dp margin) { return { CreateBarrier("top", elements, margin, 12), 0 }; }
    VerticalAnchor ConstraintLayoutBaseScope::CreateEndBarrier(Refs elements, Dp margin) { return { CreateBarrier("end", elements, margin, 13), 0 }; }
    VerticalAnchor ConstraintLayoutBaseScope::CreateAbsoluteRightBarrier(Refs elements, Dp margin) {
        return { CreateBarrier("right", elements, margin, 14), 0 };
    }
    HorizontalAnchor ConstraintLayoutBaseScope::CreateBottomBarrier(Refs elements, Dp margin) {
        return { CreateBarrier("bottom", elements, margin, 15), 0 };
    }

    ConstrainedLayoutReference ConstraintLayoutBaseScope::CreateFlow(Refs elements, const FlowParams& p) {
        ConstrainedLayoutReference ref(CreateHelperId());
        auto elementArray = CLArray::empty();
        for (const LayoutReference* e : elements)
            if (e != nullptr) elementArray->add(CLString::from(e->id));
        auto paddingArray = CLArray::empty();
        for (Dp d : { p.paddingLeft, p.paddingTop, p.paddingRight, p.paddingBottom }) paddingArray->add(std::make_shared<CLNumber>(d.value));
        CLObject* c = AsCLContainer(ref);
        c->put("contains", elementArray);
        c->putString("type", p.flowVertically ? "vFlow" : "hFlow");
        c->putNumber("vGap", p.verticalGap.value);
        c->putNumber("hGap", p.horizontalGap.value);
        c->putNumber("maxElement", float(p.maxElement));
        c->put("padding", paddingArray);
        c->putString("wrap", p.wrapMode.name);
        c->putString("vAlign", p.verticalAlign.name);
        c->putString("hAlign", p.horizontalAlign.name);
        c->putNumber("hFlowBias", p.horizontalFlowBias);
        c->putNumber("vFlowBias", p.verticalFlowBias);
        c->putString("vStyle", p.verticalStyle.name);
        c->putString("hStyle", p.horizontalStyle.name);
        UpdateHelpersHashCode(16);
        for (const LayoutReference* e : elements) UpdateHelpersHashCode(e ? StringHash(e->id) : 0);
        return ref;
    }

    ConstrainedLayoutReference ConstraintLayoutBaseScope::CreateRow(Refs elements, Dp spacing, std::vector<float> weights) {
        if (!weights.empty() && elements.size() != weights.size())
            throw std::invalid_argument("Number of weights (" + std::to_string(weights.size()) + ") should match number of elements (" +
                                        std::to_string(elements.size()) + ").");
        GridParams p;
        p.horizontalSpacing = spacing;
        p.columnWeights = std::move(weights);
        return CreateGrid(elements, 1, 0, p);
    }

    ConstrainedLayoutReference ConstraintLayoutBaseScope::CreateColumn(Refs elements, Dp spacing, std::vector<float> weights) {
        if (!weights.empty() && elements.size() != weights.size())
            throw std::invalid_argument("Number of weights (" + std::to_string(weights.size()) + ") should match number of elements (" +
                                        std::to_string(elements.size()) + ").");
        GridParams p;
        p.verticalSpacing = spacing;
        p.rowWeights = std::move(weights);
        return CreateGrid(elements, 0, 1, p);
    }

    ConstrainedLayoutReference ConstraintLayoutBaseScope::CreateGrid(Refs elements, int rows, int columns, const GridParams& p) {
        if (!p.rowWeights.empty() && rows > 0 && rows != int(p.rowWeights.size()))
            throw std::invalid_argument("Number of weights (" + std::to_string(p.rowWeights.size()) + ") should match number of rows (" +
                                        std::to_string(rows) + ").");
        if (!p.columnWeights.empty() && columns > 0 && columns != int(p.columnWeights.size()))
            throw std::invalid_argument("Number of weights (" + std::to_string(p.columnWeights.size()) + ") should match number of columns (" +
                                        std::to_string(columns) + ").");
        ConstrainedLayoutReference ref(CreateHelperId());
        auto elementArray = CLArray::empty();
        for (const LayoutReference* e : elements) elementArray->add(CLString::from(e->id));
        std::string strRowWeights = p.rowWeights.size() > 1 ? JoinFloats(p.rowWeights) : "";
        std::string strColumnWeights = p.columnWeights.size() > 1 ? JoinFloats(p.columnWeights) : "";
        std::string strSkips, strSpans;
        for (std::size_t i = 0; i < p.skips.size(); i++) strSkips += (i ? "," : "") + p.skips[i].description;
        for (std::size_t i = 0; i < p.spans.size(); i++) strSpans += (i ? "," : "") + p.spans[i].description;
        CLObject* c = AsCLContainer(ref);
        c->put("contains", elementArray);
        c->putString("type", "grid");
        c->putNumber("orientation", p.isHorizontalArrangement ? 0.0f : 1.0f);
        c->putNumber("rows", float(rows));
        c->putNumber("columns", float(columns));
        c->putNumber("vGap", p.verticalSpacing.value);
        c->putNumber("hGap", p.horizontalSpacing.value);
        c->putString("rowWeights", strRowWeights);
        c->putString("columnWeights", strColumnWeights);
        c->putString("skips", strSkips);
        c->putString("spans", strSpans);
        c->putNumber("flags", float(p.flags.value));
        return ref;
    }

    HorizontalChainReference ConstraintLayoutBaseScope::CreateHorizontalChain(Refs elements, const ChainStyle& chainStyle) {
        HorizontalChainReference ref(CreateHelperId());
        auto elementArray = CLArray::empty();
        for (const LayoutReference* e : elements) {
            if (e->chainParams) {
                const ChainParams& cp = *e->chainParams;
                auto element = CLArray::empty();
                element->add(CLString::from(e->id));
                for (float v : { cp.weight, cp.startMargin.value, cp.endMargin.value, cp.startGoneMargin.value, cp.endGoneMargin.value })
                    element->add(std::make_shared<CLNumber>(v));
                elementArray->add(element);
            } else {
                elementArray->add(CLString::from(e->id));
            }
        }
        auto styleArray = CLArray::empty();
        styleArray->add(CLString::from(chainStyle.name));
        styleArray->add(std::make_shared<CLNumber>(chainStyle.bias.value_or(0.5f)));
        CLObject* c = AsCLContainer(ref);
        c->putString("type", "hChain");
        c->put("contains", elementArray);
        c->put("style", styleArray);
        UpdateHelpersHashCode(16);
        for (const LayoutReference* e : elements) UpdateHelpersHashCode(StringHash(e->id));
        UpdateHelpersHashCode(int(std::uint32_t(StringHash(chainStyle.name)) * 31u + std::uint32_t(chainStyle.bias ? FloatHash(*chainStyle.bias) : 0)));   // Java int arithmetic wraps
        return ref;
    }

    VerticalChainReference ConstraintLayoutBaseScope::CreateVerticalChain(Refs elements, const ChainStyle& chainStyle) {
        VerticalChainReference ref(CreateHelperId());
        auto elementArray = CLArray::empty();
        for (const LayoutReference* e : elements) {
            if (e->chainParams) {
                const ChainParams& cp = *e->chainParams;
                auto element = CLArray::empty();
                element->add(CLString::from(e->id));
                for (float v : { cp.weight, cp.topMargin.value, cp.bottomMargin.value, cp.topGoneMargin.value, cp.bottomGoneMargin.value })
                    element->add(std::make_shared<CLNumber>(v));
                elementArray->add(element);
            } else {
                elementArray->add(CLString::from(e->id));
            }
        }
        auto styleArray = CLArray::empty();
        styleArray->add(CLString::from(chainStyle.name));
        styleArray->add(std::make_shared<CLNumber>(chainStyle.bias.value_or(0.5f)));
        CLObject* c = AsCLContainer(ref);
        c->putString("type", "vChain");
        c->put("contains", elementArray);
        c->put("style", styleArray);
        UpdateHelpersHashCode(17);
        for (const LayoutReference* e : elements) UpdateHelpersHashCode(StringHash(e->id));
        UpdateHelpersHashCode(int(std::uint32_t(StringHash(chainStyle.name)) * 31u + std::uint32_t(chainStyle.bias ? FloatHash(*chainStyle.bias) : 0)));   // Java int arithmetic wraps
        return ref;
    }

    LayoutReference& ConstraintLayoutBaseScope::WithHorizontalChainParams(LayoutReference& ref, Dp startMargin, Dp endMargin, Dp startGoneMargin,
                                                                         Dp endGoneMargin, float weight) {
        ChainParams p;
        p.startMargin = startMargin;
        p.endMargin = endMargin;
        p.startGoneMargin = startGoneMargin;
        p.endGoneMargin = endGoneMargin;
        p.weight = weight;
        return WithChainParams(ref, p);
    }

    LayoutReference& ConstraintLayoutBaseScope::WithVerticalChainParams(LayoutReference& ref, Dp topMargin, Dp bottomMargin, Dp topGoneMargin,
                                                                       Dp bottomGoneMargin, float weight) {
        ChainParams p;
        p.topMargin = topMargin;
        p.bottomMargin = bottomMargin;
        p.topGoneMargin = topGoneMargin;
        p.bottomGoneMargin = bottomGoneMargin;
        p.weight = weight;
        return WithChainParams(ref, p);
    }

    // ConstraintLayoutScope / ConstraintSetScope

    ConstrainedLayoutReference ConstraintLayoutScope::CreateRef() { return ConstrainedLayoutReference(std::to_string(++m_ChildId)); }

    std::vector<ConstrainedLayoutReference> ConstraintLayoutScope::CreateRefs(int count) {
        std::vector<ConstrainedLayoutReference> refs;
        refs.reserve(std::size_t(std::max(count, 0)));
        for (int i = 0; i < count; i++) refs.push_back(CreateRef());
        return refs;
    }

    void ConstraintLayoutScope::Reset() {
        ConstraintLayoutBaseScope::Reset();
        m_ChildId = 0;
    }

    std::vector<ConstrainedLayoutReference> ConstraintSetScope::CreateRefsFor(std::initializer_list<std::string> ids) const {
        std::vector<ConstrainedLayoutReference> refs;
        for (const std::string& id : ids) refs.emplace_back(id);
        return refs;
    }

    std::vector<ConstrainedLayoutReference> ConstraintSetScope::CreateRefsFor(std::initializer_list<std::string> ids, int count) {
        std::vector<ConstrainedLayoutReference> refs;
        auto it = ids.begin();
        for (int i = 0; i < count; i++) {
            if (it != ids.end()) refs.emplace_back(*it++);
            else refs.emplace_back("androidx.constraintlayout.id" + std::to_string(m_GeneratedCount++));
        }
        return refs;
    }

}
