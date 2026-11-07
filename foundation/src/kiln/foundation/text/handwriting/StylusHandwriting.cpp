#include "kiln/foundation/text/handwriting/StylusHandwriting.h"

#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Hover.h"

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

}
