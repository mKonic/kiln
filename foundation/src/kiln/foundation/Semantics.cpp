#include <algorithm>
#include "kiln/foundation/Semantics.h"

#include "kiln/runtime/Composition.h"

namespace kiln::foundation {

    namespace {
        struct SemParams {
            std::shared_ptr<std::function<void(SemanticsConfiguration&)>> fn;
            bool merge, clear;
            // The lambda is refreshed every composition; compare by what it produces instead.
            std::shared_ptr<SemanticsConfiguration> snapshot;
            bool operator==(const SemParams& o) const {
                return merge == o.merge && clear == o.clear && snapshot->Equivalent(*o.snapshot);
            }
        };
        struct SemNode final : ui::ModifierNode, ui::SemanticsModifier {
            SemParams params;
            explicit SemNode(SemParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateSemantics(); }
            ui::SemanticsModifier* AsSemantics() override { return this; }
            void ApplySemantics(SemanticsConfiguration& c) override {
                (*params.fn)(c);
                c.mergeDescendants = c.mergeDescendants || params.merge;
                c.clearAndSet = c.clearAndSet || params.clear;
            }
        };

        Modifier Make(Modifier m, std::function<void(SemanticsConfiguration&)> fn, bool merge, bool clear) {
            auto box = std::make_shared<std::function<void(SemanticsConfiguration&)>>();
            if (auto* c = rt::Composition::Current()) box = c->Remember<decltype(box)>([&] { return box; });
            *box = std::move(fn);
            auto snap = std::make_shared<SemanticsConfiguration>();
            (*box)(*snap);
            return m.With<SemNode>(SemParams{ box, merge, clear, snap });
        }
    }

    Modifier Semantics(Modifier m, std::function<void(SemanticsConfiguration&)> p, bool merge) { return Make(std::move(m), std::move(p), merge, false); }
    Modifier ClearAndSetSemantics(Modifier m, std::function<void(SemanticsConfiguration&)> p) { return Make(std::move(m), std::move(p), false, true); }

    Modifier ProgressSemantics(Modifier m, float value, float start, float end, int steps) {
        return Semantics(std::move(m), [value, start, end, steps](SemanticsConfiguration& c) {
            c.progressBarRangeInfo = ui::ProgressBarRangeInfo{ std::clamp(value, start, end), start, end, steps };
        }, true);
    }
    Modifier ProgressSemantics(Modifier m) {
        return Semantics(std::move(m), [](SemanticsConfiguration& c) { c.progressBarRangeInfo = ui::ProgressBarRangeInfo::Indeterminate(); }, true);
    }

}
