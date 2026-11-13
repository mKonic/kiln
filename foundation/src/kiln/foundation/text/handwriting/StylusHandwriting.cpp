#include "kiln/foundation/text/handwriting/StylusHandwriting.h"
#include <kiln/Function.h>

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Hover.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>
#include <memory>

namespace kiln::foundation {

    namespace {
        // HandwritingBoundsExpansion: 10 dp to the sides, 40 dp above and below.
        const DpTouchBoundsExpansion kHandwritingBoundsExpansion{ Dp(10), Dp(40), Dp(10), Dp(40), false };

        rt::Task<> Detect(PointerInputScope& scope, std::shared_ptr<StylusHandwritingCallbacks> cb) {
            for (;;) {
                // awaitEachGesture: wait until every pointer is up between gestures.
                const auto firstDown = co_await AwaitFirstDown(scope, true, PointerEventPass::Initial);
                if (!firstDown) continue;
                const bool stylus = firstDown->type == ui::PointerType::Stylus || firstDown->type == ui::PointerType::Eraser;
                if (!stylus) { co_await AwaitAllPointersUp(scope); continue; }
                const ui::IntSize size = scope.Size();
                const bool inBounds = firstDown->position.x >= 0 && firstDown->position.x < float(size.width) && firstDown->position.y >= 0 &&
                                      firstDown->position.y < float(size.height);
                // A focused editor, or one the stroke starts on, goes first.
                const bool focused = cb->isFocused && cb->isFocused();
                const PointerEventPass pass = focused || inBounds ? PointerEventPass::Initial : PointerEventPass::Main;
                std::optional<PointerInputChange> exceeded;
                for (;;) {
                    const auto e = co_await scope.AwaitPointerEvent(pass);
                    if (!e) break;
                    const PointerInputChange* change = nullptr;
                    for (const auto& c : e->changes) if (!c.IsConsumed() && c.id == firstDown->id && c.pressed) { change = &c; break; }
                    if (!change) break;
                    if (change->uptimeMillis - firstDown->uptimeMillis >= scope.Config().longPressTimeoutMillis) break;
                    if (e->classification == 2) break;   // deep press
                    const Offset d = change->position - firstDown->position;
                    if (std::sqrt(d.x * d.x + d.y * d.y) > scope.Config().handwritingSlop) { exceeded = *change; break; }
                }
                if (!exceeded) { co_await AwaitAllPointersUp(scope); continue; }
                if (!(cb->isFocused && cb->isFocused()) && cb->requestFocus) cb->requestFocus();
                if (cb->onHandwritingSlopExceeded) cb->onHandwritingSlopExceeded();
                exceeded->Consume();
                // The rest of this stroke belongs to the handwriting.
                for (;;) {
                    const auto e = co_await scope.AwaitPointerEvent(PointerEventPass::Initial);
                    if (!e) break;
                    bool tracked = false;
                    for (auto c : e->changes)
                        if (!c.IsConsumed() && c.id == firstDown->id && c.pressed) { c.Consume(); tracked = true; break; }
                    if (!tracked) break;
                }
                co_await AwaitAllPointersUp(scope);
            }
        }
    }

    Modifier StylusHandwriting(Modifier m, bool enabled, bool supported, bool showHoverIcon, StylusHandwritingCallbacks callbacks) {
        if (!enabled || !supported) return m;
        if (showHoverIcon) m = StylusHoverIcon(std::move(m), PointerIcon::Handwriting, false, kHandwritingBoundsExpansion);
        auto cb = std::make_shared<StylusHandwritingCallbacks>(std::move(callbacks));
        return PointerInput(std::move(m), rt::Keys(0), [cb](PointerInputScope& s) { return Detect(s, cb); }, kHandwritingBoundsExpansion);
    }

    namespace {
        // The detector's own focus (FocusEventModifierNode + FocusRequesterModifierNode over the
        // content below) and its latest callback, kept across recompositions.
        struct DetectorState {
            FocusRequester requester;
            bool focused = false;
            kiln::Function<void()> callback;
            ui::Owner* owner = nullptr;
        };

        // The detector's owner (requireView): set while its node is attached.
        struct AnchorParams {
            DetectorState* state;
            bool operator==(const AnchorParams&) const = default;
        };
        struct AnchorNode final : ModifierNode {
            AnchorParams params;
            explicit AnchorNode(AnchorParams p) : params(p) {}
            void OnParamsChanged() { OnAttach(); }
            void OnAttach() override { params.state->owner = Owner() ? Owner()->GetOwner() : nullptr; }
            void OnDetach() override { params.state->owner = nullptr; }
        };

        struct HandlerParams {
            bool operator==(const HandlerParams&) const = default;
        };
        // HandwritingHandlerNode: on gaining focus, accept the delegation once the editor's
        // input connection exists (launched, so after the focus change has started input).
        struct HandlerNode final : ModifierNode, ui::FocusEventModifier {
            HandlerParams params;
            std::optional<FocusState> last;
            explicit HandlerNode(HandlerParams p) : params(p) {}
            void OnParamsChanged() {}
            ui::FocusEventModifier* AsFocusEvent() override { return this; }
            void OnFocusEvent(FocusState s) override {
                if (last == s) return;
                last = s;
                if (!ui::HasFocus(s) || !Owner() || !Owner()->GetOwner()) return;
                ui::Owner* owner = Owner()->GetOwner();
                std::weak_ptr<const char> alive = m_Alive;
                rt::Dispatcher::Main().Post([owner, alive] { if (!alive.expired()) owner->AcceptStylusHandwritingDelegation(); });
            }
            std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
        };
    }

    Modifier HandwritingDetector(Modifier m, kiln::Function<void()> callback) {
        if (!ui::IsStylusHandwritingSupported()) return m;
        auto state = std::make_shared<DetectorState>();
        if (auto* c = rt::Composition::Current()) state = c->Remember<std::shared_ptr<DetectorState>>([&] { return state; });
        state->callback = std::move(callback);
        m = m.With<AnchorNode>(AnchorParams{ state.get() });
        m = StylusHoverIcon(std::move(m), PointerIcon::Handwriting, false, kHandwritingBoundsExpansion);
        StylusHandwritingCallbacks cb;
        cb.isFocused = [state] { return state->focused; };
        cb.requestFocus = [state] { state->requester.RequestFocus(); };
        cb.onHandwritingSlopExceeded = [state] {
            if (state->callback) state->callback();
            if (state->owner) state->owner->PrepareStylusHandwritingDelegation();
        };
        m = StylusHandwriting(std::move(m), true, true, false, std::move(cb));
        m = OnFocusChanged(std::move(m), [state](FocusState s) { state->focused = ui::IsFocused(s); });
        return FocusRequesterModifier(std::move(m), state->requester);
    }

    Modifier HandwritingHandler(Modifier m) {
        if (!ui::IsStylusHandwritingSupported()) return m;
        return m.With<HandlerNode>(HandlerParams{});
    }

}
