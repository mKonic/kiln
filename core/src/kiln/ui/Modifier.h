#pragma once

// Modifier elements and nodes (spec 03 req. 1). A Modifier is an immutable list of elements;
// a LayoutNode keeps one live node per element and diffs a new list against it: same element
// type at the same index updates the node in place (only when the element differs), anything
// else detaches the old node and creates a new one.

#include "kiln/ui/Geometry.h"

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <typeindex>
#include <vector>

namespace kiln::ui {

    class LayoutNode;
    class Measurable;
    class MeasureScope;
    struct MeasureResult;
    class ContentDrawScope;
    class PointerInputModifier;
    class FocusTargetNode;
    class FocusEventModifier;
    class KeyInputModifier;

    // Data a child hands its parent's measure policy (weights, alignments).
    struct ParentData {
        float weight = 0;                 // Row/Column
        bool fill = true;
        std::optional<AlignmentV> alignV; // Row child
        std::optional<AlignmentH> alignH; // Column child
        std::optional<Alignment> boxAlign;// Box child
        bool matchParentSize = false;     // Box child
        std::any extra;                   // other layouts' data
    };

    class LayoutModifier {
    public:
        virtual ~LayoutModifier() = default;
        virtual MeasureResult Measure(MeasureScope& scope, Measurable& wrapped, const Constraints& c) = 0;
        // Defaults measure through the modifier with intrinsic stand-ins (Compose's
        // NodeMeasuringIntrinsics); modifiers that change size should override.
        virtual int MinIntrinsicWidth(MeasureScope& s, Measurable& wrapped, int height);
        virtual int MaxIntrinsicWidth(MeasureScope& s, Measurable& wrapped, int height);
        virtual int MinIntrinsicHeight(MeasureScope& s, Measurable& wrapped, int width);
        virtual int MaxIntrinsicHeight(MeasureScope& s, Measurable& wrapped, int width);
    };

    class DrawModifier {
    public:
        virtual ~DrawModifier() = default;
        // Must call scope.DrawContent() to draw what it wraps (or skip it on purpose).
        virtual void Draw(ContentDrawScope& scope) = 0;
    };

    class ParentDataModifier {
    public:
        virtual ~ParentDataModifier() = default;
        virtual void ModifyParentData(ParentData& data) const = 0;
    };

    class ModifierNode {
    public:
        virtual ~ModifierNode() = default;
        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual LayoutModifier* AsLayout() { return nullptr; }
        virtual DrawModifier* AsDraw() { return nullptr; }
        virtual ParentDataModifier* AsParentData() { return nullptr; }
        virtual PointerInputModifier* AsPointerInput() { return nullptr; }
        virtual FocusTargetNode* AsFocusTarget() { return nullptr; }
        virtual FocusEventModifier* AsFocusEvent() { return nullptr; }
        virtual KeyInputModifier* AsKeyInput() { return nullptr; }

        LayoutNode* Owner() const { return m_Owner; }
        // Ask the owner to measure / place / draw again (call from Update when params change).
        void InvalidateMeasure();
        void InvalidateDraw();

    private:
        friend class LayoutNode;
        LayoutNode* m_Owner = nullptr;
    };

    class ModifierElement {
    public:
        virtual ~ModifierElement() = default;
        virtual std::type_index Type() const = 0;
        virtual std::unique_ptr<ModifierNode> Create() const = 0;
        virtual void Update(ModifierNode& node) const = 0;
        virtual bool Equals(const ModifierElement& other) const = 0;
    };

    // ElementOf<Node, Params>: the usual element whose node is built from a params struct.
    // Node must have `Params params;`, a constructor taking Params, and may override
    // OnParamsChanged() to invalidate what depends on them.
    template<typename NodeT, typename Params>
    class ElementOf final : public ModifierElement {
    public:
        explicit ElementOf(Params p) : m_Params(std::move(p)) {}
        std::type_index Type() const override { return typeid(NodeT); }
        std::unique_ptr<ModifierNode> Create() const override { return std::make_unique<NodeT>(m_Params); }
        void Update(ModifierNode& node) const override {
            auto& n = static_cast<NodeT&>(node);
            n.params = m_Params;
            n.OnParamsChanged();
        }
        bool Equals(const ModifierElement& o) const override {
            return o.Type() == Type() && static_cast<const ElementOf&>(o).m_Params == m_Params;
        }
    private:
        Params m_Params;
    };

    class Modifier {
    public:
        Modifier() = default;
        explicit Modifier(std::shared_ptr<const ModifierElement> e) { m_Elements.push_back(std::move(e)); }
        Modifier Then(const Modifier& other) const {
            Modifier m = *this;
            m.m_Elements.insert(m.m_Elements.end(), other.m_Elements.begin(), other.m_Elements.end());
            return m;
        }
        template<typename NodeT, typename Params>
        Modifier With(Params p) const { return Then(Modifier(std::make_shared<ElementOf<NodeT, Params>>(std::move(p)))); }
        const std::vector<std::shared_ptr<const ModifierElement>>& Elements() const { return m_Elements; }
        bool Empty() const { return m_Elements.empty(); }

    private:
        std::vector<std::shared_ptr<const ModifierElement>> m_Elements;
    };

}
