#include "kiln/foundation/Compose.h"

#include "kiln/ui/LayoutNode.h"

#include <any>
#include <string>

namespace kiln::foundation {

    namespace {
        // The contents' nodes are this layout's children, with an unplaced, empty boundary node
        // between two contents (Compose groups them under virtual nodes instead). Boundaries are
        // found by their parent data, so intrinsic measurements split the same way.
        struct ContentBoundary {};
        struct BoundaryParams {
            bool operator==(const BoundaryParams&) const { return true; }
        };
        struct BoundaryNode final : ModifierNode, ParentDataModifier {
            BoundaryParams params;
            explicit BoundaryNode(BoundaryParams p) : params(p) {}
            void OnParamsChanged() {}
            ParentDataModifier* AsParentData() override { return this; }
            void ModifyParentData(ParentData& d) const override { d.extra = ContentBoundary{}; }
        };
        struct EmptyPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints&) override { return s.Layout(0, 0); }
        };
        struct MultiContentAdapter final : MeasurePolicy {
            std::shared_ptr<MultiContentMeasurePolicy> policy;
            std::size_t count = 0;
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) override {
                std::vector<std::vector<Measurable*>> contents(1);
                for (Measurable* m : children) {
                    if (std::any_cast<ContentBoundary>(&m->GetParentData().extra)) contents.emplace_back();
                    else contents.back().push_back(m);
                }
                contents.resize(std::max(contents.size(), count));
                return policy->Measure(s, contents, c);
            }
        };
    }

    LayoutNode& Layout(const Modifier& modifier, std::vector<std::function<void()>> contents, std::shared_ptr<MultiContentMeasurePolicy> policy, Key key) {
        auto adapter = std::make_shared<MultiContentAdapter>();
        adapter->policy = std::move(policy);
        adapter->count = contents.size();
        return Layout(modifier, adapter, [contents = std::move(contents)] {
            static const auto empty = std::make_shared<EmptyPolicy>();
            for (std::size_t i = 0; i < contents.size(); ++i) {
                if (i > 0) Layout(Modifier{}.With<BoundaryNode>(BoundaryParams{}), empty, {}, Key("kiln.content-boundary." + std::to_string(i)));
                if (contents[i]) contents[i]();
            }
        }, key);
    }

}
