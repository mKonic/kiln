#include "kiln/foundation/BringIntoView.h"
#include "kiln/ui/Locals.h"

#include <cmath>

namespace kiln::foundation {

    void BringIntoViewRequester::BringIntoView(std::optional<Rect> rect) {
        for (ui::ModifierNode* n : m_Nodes)
            if (n->Owner()) ui::BringIntoView(*n->Owner(), rect);
    }

    namespace {
        struct RequesterParams { BringIntoViewRequester* r; bool operator==(const RequesterParams&) const = default; };
        struct RequesterNode final : ui::ModifierNode {
            RequesterParams params;
            explicit RequesterNode(RequesterParams p) : params(p) {}
            void OnParamsChanged() {}
            void OnAttach() override { params.r->Add(this); }
            void OnDetach() override { params.r->Remove(this); }
        };
    }

    Modifier BringIntoViewRequesterMod(Modifier m, BringIntoViewRequester& r) { return m.With<RequesterNode>(RequesterParams{ &r }); }

}

namespace kiln::foundation {

    float BringIntoViewSpec::DefaultCalculateScrollDistance(float offset, float size, float containerSize) {
        const float leading = offset, trailing = offset + size;
        if (leading >= 0 && trailing <= containerSize) return 0;      // already visible
        if (leading < 0 && trailing > containerSize) return 0;        // larger and covering
        return std::abs(leading) < std::abs(trailing - containerSize) ? leading : trailing - containerSize;
    }

    namespace {
        // PivotBringIntoViewSpec (BringIntoViewSpec.android.kt).
        class Pivot final : public BringIntoViewSpec {
        public:
            float CalculateScrollDistance(float offset, float size, float containerSize) const override {
                constexpr float parentFraction = 0.3f, childFraction = 0.0f;
                const float childSize = std::abs(size);
                const bool smaller = childSize <= containerSize;
                const float initial = parentFraction * containerSize - childFraction * childSize;
                const float space = containerSize - initial;
                const float target = smaller && space < childSize ? containerSize - childSize : initial;
                return offset - target;
            }
        };
    }

    std::shared_ptr<const BringIntoViewSpec> DefaultBringIntoViewSpec() {
        static const auto spec = std::make_shared<const BringIntoViewSpec>();
        return spec;
    }
    std::shared_ptr<const BringIntoViewSpec> PivotBringIntoViewSpec() {
        static const auto spec = std::make_shared<const Pivot>();
        return spec;
    }

    std::shared_ptr<const BringIntoViewSpec> CurrentBringIntoViewSpec() {
        auto* c = rt::Composition::Current();
        if (!c) return DefaultBringIntoViewSpec();
        if (auto spec = c->Read(LocalBringIntoViewSpec)) return spec;
        return c->Read(ui::LocalIsTelevision) ? PivotBringIntoViewSpec() : DefaultBringIntoViewSpec();
    }

}
