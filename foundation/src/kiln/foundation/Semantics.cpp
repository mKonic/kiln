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
                if (merge != o.merge || clear != o.clear) return false;
                const auto& a = *snapshot; const auto& b = *o.snapshot;
                return a.contentDescription == b.contentDescription && a.text == b.text && a.editableText == b.editableText &&
                       a.stateDescription == b.stateDescription && a.role == b.role && a.heading == b.heading &&
                       a.selected == b.selected && a.toggleableState == b.toggleableState && a.focused == b.focused &&
                       a.disabled == b.disabled && a.paneTitle == b.paneTitle && a.textSelection == b.textSelection &&
                       bool(a.onClick) == bool(b.onClick) && bool(a.setText) == bool(b.setText) &&
                       a.onClickLabel == b.onClickLabel && a.selectableGroup == b.selectableGroup &&
                       a.onLongClickLabel == b.onLongClickLabel && bool(a.onLongClick) == bool(b.onLongClick) &&
                       bool(a.scrollBy) == bool(b.scrollBy) && a.collectionInfo == b.collectionInfo &&
                       bool(a.horizontalScrollAxisRange) == bool(b.horizontalScrollAxisRange) &&
                       bool(a.verticalScrollAxisRange) == bool(b.verticalScrollAxisRange) &&
                       bool(a.scrollToIndex) == bool(b.scrollToIndex) && bool(a.pageUp) == bool(b.pageUp) &&
                       bool(a.pageDown) == bool(b.pageDown) && bool(a.pageLeft) == bool(b.pageLeft) && bool(a.pageRight) == bool(b.pageRight);
            }
        };
        struct SemNode final : ui::ModifierNode, ui::SemanticsModifier {
            SemParams params;
            explicit SemNode(SemParams p) : params(std::move(p)) {}
            void OnParamsChanged() { InvalidateSemantics(); }
            ui::SemanticsModifier* AsSemantics() override { return this; }
            void ApplySemantics(SemanticsConfiguration& c) override {
                // Leftmost wins: only fill what is still unset, except list values which append.
                SemanticsConfiguration mine;
                (*params.fn)(mine);
                c.mergeDescendants = c.mergeDescendants || params.merge;
                c.clearAndSet = c.clearAndSet || params.clear;
                c.isTraversalGroup = c.isTraversalGroup || mine.isTraversalGroup;
                c.contentDescription.insert(c.contentDescription.end(), mine.contentDescription.begin(), mine.contentDescription.end());
                c.text.insert(c.text.end(), mine.text.begin(), mine.text.end());
                if (!c.editableText) c.editableText = mine.editableText;
                if (!c.stateDescription) c.stateDescription = mine.stateDescription;
                if (!c.paneTitle) c.paneTitle = mine.paneTitle;
                if (!c.testTag) c.testTag = mine.testTag;
                if (c.role == ui::Role::None) c.role = mine.role;
                c.heading = c.heading || mine.heading;
                c.disabled = c.disabled || mine.disabled;
                c.password = c.password || mine.password;
                if (!c.selected) c.selected = mine.selected;
                if (!c.toggleableState) c.toggleableState = mine.toggleableState;
                if (!c.focused) c.focused = mine.focused;
                if (!c.onClick) c.onClick = mine.onClick;
                if (!c.onClickLabel) c.onClickLabel = mine.onClickLabel;
                if (!c.onLongClickLabel) c.onLongClickLabel = mine.onLongClickLabel;
                c.selectableGroup = c.selectableGroup || mine.selectableGroup;
                if (!c.onLongClick) c.onLongClick = mine.onLongClick;
                if (!c.setText) c.setText = mine.setText;
                if (!c.requestFocus) c.requestFocus = mine.requestFocus;
                if (!c.textSelection) c.textSelection = mine.textSelection;
                if (!c.horizontalScrollAxisRange) c.horizontalScrollAxisRange = mine.horizontalScrollAxisRange;
                if (!c.verticalScrollAxisRange) c.verticalScrollAxisRange = mine.verticalScrollAxisRange;
                if (!c.scrollBy) c.scrollBy = mine.scrollBy;
                if (!c.scrollToIndex) c.scrollToIndex = mine.scrollToIndex;
                if (!c.scrollViewportLength) c.scrollViewportLength = mine.scrollViewportLength;
                if (!c.collectionInfo) c.collectionInfo = mine.collectionInfo;
                if (!c.pageUp) c.pageUp = mine.pageUp;
                if (!c.pageDown) c.pageDown = mine.pageDown;
                if (!c.pageLeft) c.pageLeft = mine.pageLeft;
                if (!c.pageRight) c.pageRight = mine.pageRight;
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

}
