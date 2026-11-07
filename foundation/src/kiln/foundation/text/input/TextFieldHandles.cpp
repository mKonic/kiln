#include "kiln/foundation/text/input/TextFieldHandles.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/foundation/text/HandlePopup.h"
#include "kiln/ui/LayoutNode.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        constexpr float kHandleSizeDp = 25;                                       // HandleWidth / HandleHeight
        const float kCursorHandleWidthDp = kHandleSizeDp * 2 / (1 + 1.41421356f);  // CursorHandleWidth
        constexpr float kMinTouchTargetDp = 40;                                    // MinTouchTargetSizeForHandles

        // createHandleImage: a circle with its top-left quadrant squared off.
        gfx::Path HandlePath(float radius) {
            gfx::Path p;
            p.AddRect({ 0, 0, radius, radius });
            p.AddOval({ 0, 0, 2 * radius, 2 * radius });
            return p;
        }

        // isLeftSelectionHandle
        bool IsLeft(bool isStartHandle, text::ResolvedTextDirection d, bool crossed) {
            const bool ltr = d == text::ResolvedTextDirection::Ltr;
            return isStartHandle ? (ltr != crossed) : (ltr == crossed);
        }

        enum class Kind : std::uint8_t { Cursor, Start, End };

        void Handle(TextFieldSelectionState& s, Kind kind, bool isLeft) {
            auto* c = rt::Composition::Current();
            const Color color = c->Read(LocalTextSelectionColors).handleColor;
            TextFieldSelectionState* sp = &s;
            const auto keys = rt::Keys(reinterpret_cast<std::uintptr_t>(sp), int(kind));
            Modifier m = PointerInput(Modifier{}, keys, [sp](PointerInputScope& ps) { return sp->DetectDirectTouchInteraction(ps); });
            if (kind == Kind::Cursor) {
                m = PointerInput(std::move(m), keys, [sp](PointerInputScope& ps) { return sp->CursorHandleDragGestures(ps); });
                m = PointerInput(std::move(m), keys, [sp](PointerInputScope& ps) { return sp->CursorHandleTapGestures(ps); });
            } else {
                const bool start = kind == Kind::Start;
                m = PointerInput(std::move(m), keys, [sp, start](PointerInputScope& ps) { return sp->SelectionHandlePressGestures(ps, start); });
                m = PointerInput(std::move(m), keys, [sp, start](PointerInputScope& ps) { return sp->SelectionHandleDragGestures(ps, start); });
            }
            // requiredSizeIn(min 40 dp): the touch target; the icon at its top (centred for the
            // cursor, towards the point for a selection handle).
            m = SizeIn(std::move(m), Dp(kMinTouchTargetDp), Dp(kMinTouchTargetDp), Dp::Unspecified(), Dp::Unspecified(), false);
            const float iconWidth = kind == Kind::Cursor ? kCursorHandleWidthDp : kHandleSizeDp;
            m = DrawBehind(std::move(m), [sp, kind, isLeft, color, iconWidth](DrawScope& d) {
                const float w = d.ToPx(Dp(iconWidth)), h = d.ToPx(Dp(kHandleSizeDp));
                const ui::Size box = d.GetSize();
                if (kind == Kind::Cursor) {
                    // drawCursorHandle: the image turned 45 degrees around its top-left corner,
                    // moved right by the radius.
                    const float radius = w / 2;
                    d.Save();
                    d.Translate({ (box.width - w) / 2 + radius, 0 });
                    d.Rotate(45, { 0, 0 });
                    d.DrawPath(HandlePath(radius), color);
                    d.Restore();
                    return;
                }
                // iconVisible: the handle has a position.
                const TextFieldHandleState st = sp->GetSelectionHandleState(kind == Kind::Start, true);
                if (std::isnan(st.position.x)) return;
                const float left = isLeft ? box.width - w : 0;   // Arrangement.Absolute.Right / Left
                const float radius = w / 2;
                d.Save();
                d.Translate({ left, 0 });
                if (isLeft) d.Scale(-1, 1, { w / 2, h / 2 });
                d.DrawPath(HandlePath(radius), color);
                d.Restore();
            });
            Box(m, Alignment::TopStart(), false, {}, rt::Key(int(kind) + 1));
        }
    }

    void TextFieldWithHandles(TextFieldSelectionState& selection, bool showHandles, bool showCursorHandle, std::function<void()> content) {
        TextFieldSelectionState* s = &selection;
        // The content measures as if alone; the handle popups' anchors take no space.
        auto policy = std::make_shared<ui::LambdaMeasurePolicy>([](MeasureScope& ms, std::span<Measurable* const> children, const Constraints& c) {
            Placeable& p = children[0]->Measure(c);
            std::vector<Placeable*> anchors;
            for (std::size_t i = 1; i < children.size(); ++i) anchors.push_back(&children[i]->Measure(Constraints::Fixed(0, 0)));
            return ms.Layout(p.Width(), p.Height(), [&p, anchors](PlacementScope& ps) {
                ps.Place(p, 0, 0);
                for (Placeable* a : anchors) ps.Place(*a, 0, 0);
            });
        });
        Layout(Modifier{}, policy, [s, showHandles, showCursorHandle, content] {
            content();
            // Recomposes only when visibility (not the position) changes.
            rt::Composition::Current()->Child([s, showHandles, showCursorHandle] {
                if (!showHandles) return;
                // Each handle in a popup at its position in the core node's coordinates.
                for (bool start : { true, false }) {
                    const TextFieldHandleState st = s->GetSelectionHandleState(start, false);
                    if (!st.visible) continue;
                    const bool left = IsLeft(start, st.direction, st.handlesCrossed);
                    HandlePopup([s, start] {
                        const TextFieldHandleState h = s->GetSelectionHandleState(start, true);
                        if (std::isnan(h.position.x)) return h.position;
                        return s->textLayoutState.FromTextLayoutToCore(h.position);
                    }, left ? 1.0f : 0.0f, [s, start, left] { Handle(*s, start ? Kind::Start : Kind::End, left); },
                    rt::Key(start ? 0x7e17 : 0x7e18));
                }
                if (showCursorHandle && s->GetCursorHandleState(false).visible)
                    HandlePopup([s] {
                        const TextFieldHandleState h = s->GetCursorHandleState(true);
                        if (std::isnan(h.position.x)) return h.position;
                        return s->textLayoutState.FromTextLayoutToCore(h.position);
                    }, 0.5f, [s] { Handle(*s, Kind::Cursor, false); }, rt::Key(0x7e19));
            }, rt::Key(0x7e16));
        });
    }

}
