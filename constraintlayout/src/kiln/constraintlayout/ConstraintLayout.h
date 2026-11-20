#pragma once

// ConstraintLayout (constraintlayout-compose): a layout that positions its children by
// constraints between them and to the parent, guidelines, barriers, chains, flows and grids.
//
// Two ways to describe the constraints, as in Compose:
// - inline: ConstraintLayout(modifier, [](ConstraintLayoutScope& s) { ... }) with refs from
//   s.CreateRef() and children tagged by s.ConstrainAs(modifier, ref, block);
// - a ConstraintSet (DSL or JSON5) applied to children tagged with LayoutId.
//
// The DSL writes its constraints into a JSON object that ConstraintSetParser reads into the
// solver's State, as upstream does. Not ported: animateChanges (it runs on MotionLayout),
// MotionLayout itself, and the Studio live-edit registry.

#include "kiln/constraintlayout/core/parser/CLParser.h"
#include "kiln/constraintlayout/core/widgets/Optimizer.h"
#include "kiln/foundation/Compose.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace kiln::constraintlayout::core::state {
    class State;
}

namespace kiln::constraintlayout {

    using ui::Dp;
    using ui::Modifier;

    class ConstrainedLayoutReference;
    class LayoutReference;

    // Dimension: how a child is sized. The Coercible / MinCoercible / MaxCoercible split of
    // Compose's types is not enforced: AtLeast / AtMost apply to any dimension.
    class Dimension {
    public:
        static Dimension PreferredValue(Dp dp);   // spread, at most dp
        static Dimension Value(Dp dp);
        static Dimension Ratio(const std::string& ratio);
        static Dimension PreferredWrapContent();
        static Dimension WrapContent();
        static Dimension MatchParent();
        static Dimension FillToConstraints();
        static Dimension Percent(float percent);

        Dimension AtLeast(Dp dp) const;
        Dimension AtMost(Dp dp) const;
        Dimension AtLeastWrapContent() const;
        Dimension AtMostWrapContent() const;

        // The element the constraint set stores (DimensionDescription.asCLElement).
        core::CLElementPtr AsCLElement() const;
        bool operator==(const Dimension&) const = default;

    private:
        struct Symbol {
            std::optional<float> value;   // dp
            std::string symbol;
            bool IsUndefined() const { return !value && symbol.empty(); }
            core::CLElementPtr AsCLElement() const;
            bool operator==(const Symbol&) const = default;
        };
        Symbol m_Value, m_Min, m_Max;
    };

    // ChainParams (withChainParams): margins and weight a reference takes in a chain.
    struct ChainParams {
        Dp startMargin{ 0 }, topMargin{ 0 }, endMargin{ 0 }, bottomMargin{ 0 };
        Dp startGoneMargin{ 0 }, topGoneMargin{ 0 }, endGoneMargin{ 0 }, bottomGoneMargin{ 0 };
        float weight = NAN;
    };

    class LayoutReference {
    public:
        explicit LayoutReference(std::string id) : id(std::move(id)) {}
        const std::string id;
        bool operator==(const LayoutReference& o) const { return id == o.id; }
        std::optional<ChainParams> chainParams;
    };

    // Anchors: an edge of a reference that another can link to. index: -2 start, -1 end,
    // 0 left (top), 1 right (bottom).
    struct VerticalAnchor {
        std::string id;
        int index = 0;
        bool operator==(const VerticalAnchor&) const = default;
        // The reference the anchor belongs to (a guideline's or barrier's own, for one).
        LayoutReference reference() const;
    };
    struct HorizontalAnchor {
        std::string id;
        int index = 0;
        bool operator==(const HorizontalAnchor&) const = default;
        // The reference the anchor belongs to (a guideline's or barrier's own, for one).
        LayoutReference reference() const;
    };
    struct BaselineAnchor {
        std::string id;
        bool operator==(const BaselineAnchor&) const = default;
        LayoutReference reference() const;
    };

    class ConstrainedLayoutReference : public LayoutReference {
    public:
        explicit ConstrainedLayoutReference(std::string id);
        VerticalAnchor start, absoluteLeft, end, absoluteRight;
        HorizontalAnchor top, bottom;
        BaselineAnchor baseline;
    };

    class HorizontalChainReference : public LayoutReference {
    public:
        explicit HorizontalChainReference(std::string id);
        VerticalAnchor start, absoluteLeft, end, absoluteRight;
    };

    class VerticalChainReference : public LayoutReference {
    public:
        explicit VerticalChainReference(std::string id);
        HorizontalAnchor top, bottom;
    };

    struct ChainStyle {
        std::string name;
        std::optional<float> bias;
        static ChainStyle Spread() { return { "spread", std::nullopt }; }
        static ChainStyle SpreadInside() { return { "spread_inside", std::nullopt }; }
        static ChainStyle Packed(float bias = 0.5f) { return { "packed", bias }; }
        bool operator==(const ChainStyle&) const = default;
    };

    struct Visibility {
        std::string name;
        static Visibility Visible() { return { "visible" }; }
        static Visibility Invisible() { return { "invisible" }; }
        static Visibility Gone() { return { "gone" }; }
        bool operator==(const Visibility&) const = default;
    };

    struct Wrap {
        std::string name;
        static Wrap None() { return { "none" }; }
        static Wrap Chain() { return { "chain" }; }
        static Wrap Aligned() { return { "aligned" }; }
    };
    struct VerticalAlign {
        std::string name;
        static VerticalAlign Top() { return { "top" }; }
        static VerticalAlign Bottom() { return { "bottom" }; }
        static VerticalAlign Center() { return { "center" }; }
        static VerticalAlign Baseline() { return { "baseline" }; }
    };
    struct HorizontalAlign {
        std::string name;
        static HorizontalAlign Start() { return { "start" }; }
        static HorizontalAlign End() { return { "end" }; }
        static HorizontalAlign Center() { return { "center" }; }
    };
    struct FlowStyle {
        std::string name;
        static FlowStyle Spread() { return { "spread" }; }
        static FlowStyle SpreadInside() { return { "spread_inside" }; }
        static FlowStyle Packed() { return { "packed" }; }
    };

    // GridFlag: how a grid places spans; combine with |.
    class GridFlag {
    public:
        static GridFlag None() { return GridFlag(false, false); }
        static GridFlag PlaceLayoutsOnSpansFirst() { return GridFlag(true, false); }
        static GridFlag SubGridByColRow() { return GridFlag(false, true); }
        GridFlag operator|(const GridFlag& o) const {
            return GridFlag(IsPlaceLayoutsOnSpansFirst() || o.IsPlaceLayoutsOnSpansFirst(), IsSubGridByColRow() || o.IsSubGridByColRow());
        }
        bool IsPlaceLayoutsOnSpansFirst() const;
        bool IsSubGridByColRow() const;
        int value = 0;

    private:
        GridFlag(bool placeLayoutsOnSpansFirst, bool subGridByColRow);
    };

    // Skip and Span: cells a grid leaves empty, or a widget covers ("position:rowsxcolumns").
    struct Skip {
        std::string description;
        Skip(int position, int rows, int columns);
        Skip(int position, int size);
    };
    struct Span {
        std::string description;
        explicit Span(std::string d) : description(std::move(d)) {}
        Span(int position, int rows, int columns);
        Span(int position, int size);
    };

    // The anchors of the reference being constrained, linking to others' anchors.
    class VerticalAnchorable {
    public:
        VerticalAnchorable(core::CLObject* container, int index);
        void LinkTo(const VerticalAnchor& anchor, Dp margin = Dp(0), Dp goneMargin = Dp(0));

    private:
        core::CLObject* m_Container;
        std::string m_AnchorName;
    };
    class HorizontalAnchorable {
    public:
        HorizontalAnchorable(core::CLObject* container, int index);
        void LinkTo(const HorizontalAnchor& anchor, Dp margin = Dp(0), Dp goneMargin = Dp(0));
        void LinkTo(const BaselineAnchor& anchor, Dp margin = Dp(0), Dp goneMargin = Dp(0));

    private:
        core::CLObject* m_Container;
        std::string m_AnchorName;
    };
    class BaselineAnchorable {
    public:
        explicit BaselineAnchorable(core::CLObject* container) : m_Container(container) {}
        void LinkTo(const BaselineAnchor& anchor, Dp margin = Dp(0), Dp goneMargin = Dp(0));
        void LinkTo(const HorizontalAnchor& anchor, Dp margin = Dp(0), Dp goneMargin = Dp(0));

    private:
        core::CLObject* m_Container;
    };

    // ConstrainScope: the constraints of one reference (the block of constrain / constrainAs).
    // The properties write into the constraint set when assigned, as Compose's delegates do.
    class ConstrainScope {
    public:
        ConstrainScope(std::string id, core::CLObject* container);

        template<typename T>
        class Property {
        public:
            Property(ConstrainScope* scope, const char* name, T initial) : m_Scope(scope), m_Name(name), m_Value(initial) {}
            Property& operator=(T value) {
                m_Value = value;
                m_Scope->Write(m_Name, m_Value);
                return *this;
            }
            Property(const Property&) = delete;
            const T& Get() const { return m_Value; }
            operator const T&() const { return m_Value; }

        private:
            ConstrainScope* m_Scope;
            const char* m_Name;
            T m_Value;
        };

        const std::string id;
        ConstrainedLayoutReference parent{ "parent" };
        VerticalAnchorable start, absoluteLeft;
        HorizontalAnchorable top;
        VerticalAnchorable end, absoluteRight;
        HorizontalAnchorable bottom;
        BaselineAnchorable baseline;
        Property<Dimension> width{ this, "width", Dimension::WrapContent() };
        Property<Dimension> height{ this, "height", Dimension::WrapContent() };
        Property<Visibility> visibility{ this, "visibility", Visibility::Visible() };
        Property<float> alpha{ this, "alpha", 1.0f };
        Property<float> scaleX{ this, "scaleX", 1.0f };
        Property<float> scaleY{ this, "scaleY", 1.0f };
        Property<float> rotationX{ this, "rotationX", 0.0f };
        Property<float> rotationY{ this, "rotationY", 0.0f };
        Property<float> rotationZ{ this, "rotationZ", 0.0f };
        Property<Dp> translationX{ this, "translationX", Dp(0) };
        Property<Dp> translationY{ this, "translationY", Dp(0) };
        Property<Dp> translationZ{ this, "translationZ", Dp(0) };
        Property<float> pivotX{ this, "pivotX", 0.5f };
        Property<float> pivotY{ this, "pivotY", 0.5f };
        Property<float> horizontalChainWeight{ this, "hWeight", NAN };
        Property<float> verticalChainWeight{ this, "vWeight", NAN };
        Property<float> horizontalBias{ this, "hBias", 0.5f };
        Property<float> verticalBias{ this, "vBias", 0.5f };

        void LinkTo(const VerticalAnchor& start, const VerticalAnchor& end, Dp startMargin = Dp(0), Dp endMargin = Dp(0),
                    Dp startGoneMargin = Dp(0), Dp endGoneMargin = Dp(0), float bias = 0.5f);
        void LinkTo(const HorizontalAnchor& top, const HorizontalAnchor& bottom, Dp topMargin = Dp(0), Dp bottomMargin = Dp(0),
                    Dp topGoneMargin = Dp(0), Dp bottomGoneMargin = Dp(0), float bias = 0.5f);
        void LinkTo(const VerticalAnchor& start, const HorizontalAnchor& top, const VerticalAnchor& end, const HorizontalAnchor& bottom,
                    Dp startMargin = Dp(0), Dp topMargin = Dp(0), Dp endMargin = Dp(0), Dp bottomMargin = Dp(0), Dp startGoneMargin = Dp(0),
                    Dp topGoneMargin = Dp(0), Dp endGoneMargin = Dp(0), Dp bottomGoneMargin = Dp(0), float horizontalBias = 0.5f,
                    float verticalBias = 0.5f);
        void CenterTo(const ConstrainedLayoutReference& other);
        void CenterHorizontallyTo(const ConstrainedLayoutReference& other, float bias = 0.5f);
        void CenterVerticallyTo(const ConstrainedLayoutReference& other, float bias = 0.5f);
        void CenterAround(const VerticalAnchor& anchor);
        void CenterAround(const HorizontalAnchor& anchor);
        void Circular(const ConstrainedLayoutReference& other, float angle, Dp distance);
        void ClearHorizontal();
        void ClearVertical();
        void ClearConstraints();
        void ResetDimensions();
        void ResetTransforms();

    private:
        void Write(const char* name, const Dimension& d);
        void Write(const char* name, const Visibility& v);
        void Write(const char* name, float f);
        void Write(const char* name, Dp dp);
        core::CLObject* m_Container;
    };

    class HorizontalChainScope {
    public:
        HorizontalChainScope(std::string id, core::CLObject* container);
        const std::string id;
        ConstrainedLayoutReference parent{ "parent" };
        VerticalAnchorable start, absoluteLeft, end, absoluteRight;
    };

    class VerticalChainScope {
    public:
        VerticalChainScope(std::string id, core::CLObject* container);
        const std::string id;
        ConstrainedLayoutReference parent{ "parent" };
        HorizontalAnchorable top, bottom;
    };

    // createFlow; null elements are skipped. One padding, or left/top/right/bottom.
    struct FlowParams {
        bool flowVertically = false;
        Dp verticalGap{ 0 }, horizontalGap{ 0 };
        int maxElement = 0;
        Dp paddingLeft{ 0 }, paddingTop{ 0 }, paddingRight{ 0 }, paddingBottom{ 0 };
        Wrap wrapMode = Wrap::None();
        VerticalAlign verticalAlign = VerticalAlign::Center();
        HorizontalAlign horizontalAlign = HorizontalAlign::Center();
        float horizontalFlowBias = 0.0f, verticalFlowBias = 0.0f;
        FlowStyle verticalStyle = FlowStyle::Packed(), horizontalStyle = FlowStyle::Packed();
        FlowParams& Padding(Dp all) {
            paddingLeft = paddingTop = paddingRight = paddingBottom = all;
            return *this;
        }
        FlowParams& Padding(Dp horizontal, Dp vertical) {
            paddingLeft = paddingRight = horizontal;
            paddingTop = paddingBottom = vertical;
            return *this;
        }
    };
    // createGrid / createRow / createColumn options.
    struct GridParams {
        bool isHorizontalArrangement = true;
        Dp verticalSpacing{ 0 }, horizontalSpacing{ 0 };
        std::vector<float> rowWeights, columnWeights;
        std::vector<Skip> skips;
        std::vector<Span> spans;
        GridFlag flags = GridFlag::None();
    };
    // What ConstraintLayoutScope and ConstraintSetScope share: helpers and constrain blocks,
    // written into one JSON object.
    class ConstraintLayoutBaseScope {
    public:
        explicit ConstraintLayoutBaseScope(std::shared_ptr<core::CLObject> extendFrom = nullptr);
        virtual ~ConstraintLayoutBaseScope() = default;

        void ApplyTo(core::state::State& state) const;
        virtual void Reset();

        HorizontalChainScope Constrain(const HorizontalChainReference& ref, const kiln::Function<void(HorizontalChainScope&)>& block);
        VerticalChainScope Constrain(const VerticalChainReference& ref, const kiln::Function<void(VerticalChainScope&)>& block);
        void Constrain(const ConstrainedLayoutReference& ref, const kiln::Function<void(ConstrainScope&)>& block);
        void Constrain(std::initializer_list<const ConstrainedLayoutReference*> refs, const kiln::Function<void(ConstrainScope&)>& block);

        VerticalAnchor CreateGuidelineFromStart(Dp offset);
        VerticalAnchor CreateGuidelineFromAbsoluteLeft(Dp offset);
        VerticalAnchor CreateGuidelineFromStart(float fraction);
        VerticalAnchor CreateGuidelineFromAbsoluteLeft(float fraction);
        VerticalAnchor CreateGuidelineFromEnd(Dp offset);
        VerticalAnchor CreateGuidelineFromAbsoluteRight(Dp offset);
        VerticalAnchor CreateGuidelineFromEnd(float fraction);
        VerticalAnchor CreateGuidelineFromAbsoluteRight(float fraction);
        HorizontalAnchor CreateGuidelineFromTop(Dp offset);
        HorizontalAnchor CreateGuidelineFromTop(float fraction);
        HorizontalAnchor CreateGuidelineFromBottom(Dp offset);
        HorizontalAnchor CreateGuidelineFromBottom(float fraction);

        // A helper's elements: a braced list or a vector (Kotlin's vararg, or a spread array).
        class Refs {
        public:
            Refs(std::initializer_list<const LayoutReference*> refs) : m_Refs(refs) {}
            Refs(std::vector<const LayoutReference*> refs) : m_Refs(std::move(refs)) {}
            auto begin() const { return m_Refs.begin(); }
            auto end() const { return m_Refs.end(); }
            std::size_t size() const { return m_Refs.size(); }

        private:
            std::vector<const LayoutReference*> m_Refs;
        };
        using FlowParams = kiln::constraintlayout::FlowParams;
        using GridParams = kiln::constraintlayout::GridParams;
        VerticalAnchor CreateStartBarrier(Refs elements, Dp margin = Dp(0));
        VerticalAnchor CreateAbsoluteLeftBarrier(Refs elements, Dp margin = Dp(0));
        HorizontalAnchor CreateTopBarrier(Refs elements, Dp margin = Dp(0));
        VerticalAnchor CreateEndBarrier(Refs elements, Dp margin = Dp(0));
        VerticalAnchor CreateAbsoluteRightBarrier(Refs elements, Dp margin = Dp(0));
        HorizontalAnchor CreateBottomBarrier(Refs elements, Dp margin = Dp(0));

        ConstrainedLayoutReference CreateFlow(Refs elements, const FlowParams& params = {});

        ConstrainedLayoutReference CreateRow(Refs elements, Dp spacing = Dp(0), std::vector<float> weights = {});
        ConstrainedLayoutReference CreateColumn(Refs elements, Dp spacing = Dp(0), std::vector<float> weights = {});
        // Throws std::invalid_argument when weights don't match the rows or columns.
        ConstrainedLayoutReference CreateGrid(Refs elements, int rows, int columns, const GridParams& params = {});

        HorizontalChainReference CreateHorizontalChain(Refs elements, const ChainStyle& chainStyle = ChainStyle::Spread());
        VerticalChainReference CreateVerticalChain(Refs elements, const ChainStyle& chainStyle = ChainStyle::Spread());

        // withChainParams and its horizontal / vertical forms: the reference, with params set.
        LayoutReference& WithChainParams(LayoutReference& ref, const ChainParams& params) {
            ref.chainParams = params;
            return ref;
        }
        LayoutReference& WithHorizontalChainParams(LayoutReference& ref, Dp startMargin = Dp(0), Dp endMargin = Dp(0),
                                                   Dp startGoneMargin = Dp(0), Dp endGoneMargin = Dp(0), float weight = NAN);
        LayoutReference& WithVerticalChainParams(LayoutReference& ref, Dp topMargin = Dp(0), Dp bottomMargin = Dp(0), Dp topGoneMargin = Dp(0),
                                                 Dp bottomGoneMargin = Dp(0), float weight = NAN);

        int HelpersHashCode() const { return m_HelpersHashCode; }
        const std::shared_ptr<core::CLObject>& ContainerObject() const { return m_ContainerObject; }
        core::CLObject* AsCLContainer(const LayoutReference& ref);
        bool operator==(const ConstraintLayoutBaseScope& o) const;

    protected:
        std::shared_ptr<core::CLObject> m_ContainerObject;

    private:
        void UpdateHelpersHashCode(int value) { m_HelpersHashCode = int((int64_t(m_HelpersHashCode) * 1009 + value) % 1000000007); }
        std::string CreateHelperId() { return std::to_string(m_HelperId++); }
        VerticalAnchor CreateVerticalGuideline(const char* key, float value, int hashKind, int hash);
        VerticalAnchor CreateVerticalPercentGuideline(const char* origin, float fraction, int hashKind);
        HorizontalAnchor CreateHorizontalGuideline(const char* key, float value, int hashKind, int hash);
        std::string CreateBarrier(const char* direction, Refs elements, Dp margin, int hashKind);
        static constexpr int kHelpersStartId = 1000;
        int m_HelpersHashCode = 0;
        int m_HelperId = kHelpersStartId;
    };

    class ConstraintLayoutScope : public ConstraintLayoutBaseScope {
    public:
        ConstraintLayoutScope() = default;
        // A reference for a child; the same ones come back in the same order each composition.
        ConstrainedLayoutReference CreateRef();
        // createRefs: count references at once.
        std::vector<ConstrainedLayoutReference> CreateRefs(int count);
        // Modifier.constrainAs: ties the child to `ref`, constrained by `block` (run at measure).
        // A new block each composition re-runs the constraints after each recomposition of the
        // content; Compose's compiler keeps a lambda with stable captures, which the overload
        // below gives: pass the same remembered block and nothing is rebuilt.
        Modifier ConstrainAs(Modifier modifier, const ConstrainedLayoutReference& ref, kiln::Function<void(ConstrainScope&)> block);
        using ConstrainBlock = std::shared_ptr<const kiln::Function<void(ConstrainScope&)>>;
        Modifier ConstrainAs(Modifier modifier, const ConstrainedLayoutReference& ref, ConstrainBlock block);
        void Reset() override;

    private:
        int m_ChildId = 0;
    };

    class ConstraintSetScope : public ConstraintLayoutBaseScope {
    public:
        explicit ConstraintSetScope(std::shared_ptr<core::CLObject> extendFrom = nullptr) : ConstraintLayoutBaseScope(std::move(extendFrom)) {}
        ConstrainedLayoutReference CreateRefFor(const std::string& id) const { return ConstrainedLayoutReference(id); }
        std::vector<ConstrainedLayoutReference> CreateRefsFor(std::initializer_list<std::string> ids) const;
        // createRefsFor destructured into `count` references: ids past those given are generated
        // ("androidx.constraintlayout.id0", ...).
        std::vector<ConstrainedLayoutReference> CreateRefsFor(std::initializer_list<std::string> ids, int count);

    private:
        int m_GeneratedCount = 0;
    };

    // A ConstraintSet: constraints for the children tagged with LayoutId.
    class ConstraintSet {
    public:
        virtual ~ConstraintSet() = default;
        virtual void ApplyTo(core::state::State& state, std::span<ui::Measurable* const> measurables) = 0;
        virtual bool IsDirty(std::span<ui::Measurable* const>) { return true; }
        // Replaces a variable of a JSON constraint set.
        virtual void Override(const std::string& /*name*/, float /*value*/) {}
        virtual bool Equals(const ConstraintSet& other) const { return this == &other; }
    };

    // ConstraintSet { }: from the DSL, optionally extending another made the same way.
    std::shared_ptr<ConstraintSet> MakeConstraintSet(const kiln::Function<void(ConstraintSetScope&)>& description,
                                                     std::shared_ptr<ConstraintSet> extendConstraintSet = nullptr);
    // ConstraintSet(json): from JSON5, optionally extending another; overrideVariables is a
    // JSON5 object of variable values.
    std::shared_ptr<ConstraintSet> MakeConstraintSet(const std::string& jsonContent, std::shared_ptr<ConstraintSet> extendConstraintSet = nullptr,
                                                     const std::string& overrideVariables = {});
    // The composable ConstraintSet(content, overrideVariables): remembered for its arguments.
    std::shared_ptr<ConstraintSet> RememberConstraintSet(const std::string& content, const std::string& overrideVariables = {});

    // ConstraintLayout with the inline DSL.
    void ConstraintLayout(const Modifier& modifier, kiln::Function<void(ConstraintLayoutScope&)> content,
                          int optimizationLevel = core::Optimizer::OPTIMIZATION_STANDARD, rt::Key key = {});
    // ConstraintLayout with a ConstraintSet; children are matched by LayoutId.
    void ConstraintLayout(std::shared_ptr<ConstraintSet> constraintSet, const Modifier& modifier, kiln::Function<void()> content,
                          int optimizationLevel = core::Optimizer::OPTIMIZATION_STANDARD, rt::Key key = {});

    // A child's ConstraintLayout tag (Modifier.layoutId with a tag): State.setTag, for a
    // constraint set's tag-based variables.
    Modifier LayoutId(Modifier modifier, std::string layoutId, std::string tag);

}
