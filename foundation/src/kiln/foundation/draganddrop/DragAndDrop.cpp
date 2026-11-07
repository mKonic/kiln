#include "kiln/foundation/draganddrop/DragAndDrop.h"

#include "kiln/foundation/Gestures.h"
#include "kiln/ui/LayoutNode.h"

namespace kiln::foundation {

    namespace {
        // --- the target --------------------------------------------------------------------------

        struct TargetParams {
            std::shared_ptr<const std::function<bool(const DragAndDropEvent&)>> shouldStart;
            std::shared_ptr<ui::DragAndDropTarget> target;
            bool operator==(const TargetParams&) const = default;
        };
        struct DragAndDropTargetNode final : ui::DragAndDropNode {
            TargetParams params;
            explicit DragAndDropTargetNode(TargetParams p) : params(std::move(p)), m_Target(params.target) { Validate(); }
            void Validate() {
                onDropTargetValidate = [this](const DragAndDropEvent& e) -> ui::DragAndDropTarget* {
                    return (*params.shouldStart)(e) ? params.target.get() : nullptr;
                };
            }
            void OnParamsChanged() {
                // A new target starts over (the delegated node is replaced); a new predicate
                // just applies from the next session.
                if (params.target != m_Target) {
                    m_Target = params.target;
                    DragAndDropNode::OnDetach();
                }
            }
            std::shared_ptr<ui::DragAndDropTarget> m_Target;
        };

        // --- the source --------------------------------------------------------------------------

        // What the source's pointer input reaches: the node, once attached.
        struct SourceState {
            ui::DragAndDropNode* node = nullptr;
        };
        struct SourceParams {
            std::shared_ptr<SourceState> state;
            std::shared_ptr<const DragTransferData> transferData;
            std::shared_ptr<const std::function<void(DrawScope&)>> drawDragDecoration;   // null: the content
            bool operator==(const SourceParams&) const = default;
        };
        struct DragAndDropSourceNode final : ui::DragAndDropNode, DrawModifier {
            SourceParams params;
            explicit DragAndDropSourceNode(SourceParams p) : params(std::move(p)) {
                onStartTransfer = [this](ui::DragAndDropStartTransferScope& scope, Offset offset) {
                    const auto data = (*params.transferData)(offset);
                    if (!data || !ModifierNode::Owner()) return;
                    const ui::IntSize size = ModifierNode::Owner()->Size();
                    std::function<void(DrawScope&)> draw;
                    if (params.drawDragDecoration) {
                        draw = *params.drawDragDecoration;
                    } else {
                        // DragSourceNodeWithDefaultPainter: the content as this node draws it.
                        draw = [this](DrawScope& d) {
                            if (auto* n = ModifierNode::Owner()) n->DrawAfter(this, static_cast<ContentDrawScope&>(d));
                        };
                    }
                    scope.StartDragAndDropTransfer(*data, { float(size.width), float(size.height) }, std::move(draw));
                };
            }
            void OnParamsChanged() { params.state->node = this; }
            void OnAttach() override { params.state->node = this; }
            void OnDetach() override {
                if (params.state->node == this) params.state->node = nullptr;
                DragAndDropNode::OnDetach();
            }
            DrawModifier* AsDraw() override { return this; }
            void Draw(ContentDrawScope& s) override { s.DrawContent(); }
        };

        // DragAndDropSourceDefaults.DefaultStartDetector.
        rt::Task<> DefaultStartDetector(PointerInputScope& scope, std::shared_ptr<SourceState> state) {
            for (;;) {
                const auto down = co_await AwaitFirstDown(scope, false, PointerEventPass::Initial);
                if (!down) continue;
                auto request = [&](Offset at) { if (state->node) state->node->RequestDragAndDropTransfer(at); };
                if (down->type == PointerType::Mouse) {
                    // processDragGesture: past the slop, the drag starts at the down position.
                    const auto slop = co_await AwaitTouchSlopOrCancellation(scope, down->id, [](PointerInputChange& c, Offset) { c.Consume(); });
                    if (slop) request(down->position);
                } else {
                    // processTapGesture with onLongPress only.
                    const auto press = co_await AwaitLongPressOrCancellation(scope, down->id);
                    if (press) request(press->position);
                }
                co_await AwaitAllPointersUp(scope);
            }
        }

        Modifier Source(Modifier m, std::shared_ptr<const std::function<void(DrawScope&)>> draw, DragTransferData transferData) {
            auto* c = rt::Composition::Current();
            auto state = c->Remember<std::shared_ptr<SourceState>>([] { return std::make_shared<SourceState>(); });
            m = m.With<DragAndDropSourceNode>(SourceParams{ state, std::make_shared<const DragTransferData>(std::move(transferData)), std::move(draw) });
            return PointerInput(std::move(m), rt::Keys(state.get()), [state](PointerInputScope& s) { return DefaultStartDetector(s, state); });
        }
    }

    Modifier DragAndDropSource(Modifier m, DragTransferData transferData) { return Source(std::move(m), nullptr, std::move(transferData)); }

    Modifier DragAndDropSource(Modifier m, std::function<void(DrawScope&)> drawDragDecoration, DragTransferData transferData) {
        return Source(std::move(m), std::make_shared<const std::function<void(DrawScope&)>>(std::move(drawDragDecoration)), std::move(transferData));
    }

    Modifier DragAndDropTarget(Modifier m, std::function<bool(const DragAndDropEvent&)> shouldStartDragAndDrop,
                               std::shared_ptr<ui::DragAndDropTarget> target) {
        return m.With<DragAndDropTargetNode>(
            TargetParams{ std::make_shared<const std::function<bool(const DragAndDropEvent&)>>(std::move(shouldStartDragAndDrop)), std::move(target) });
    }

}
