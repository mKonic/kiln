#include "kiln/material3/DragHandle.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Hover.h"
#include "kiln/material3/ColorScheme.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Shapes.h"
#include "kiln/material3/tokens/DragHandleTokens.h"

#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using ui::DpSize;
    namespace T = tokens::DragHandleTokens;

    namespace VerticalDragHandleDefaults {
        DragHandleColors colors() {
            const ColorScheme& c = MaterialTheme::colorScheme();
            return { FromToken(c, T::Color), FromToken(c, T::PressedColor), FromToken(c, T::DraggedColor) };
        }
        DragHandleColors colors(std::optional<ui::Color> color, std::optional<ui::Color> pressedColor, std::optional<ui::Color> draggedColor) {
            const DragHandleColors d = colors();
            auto pick = [](std::optional<ui::Color> v, ui::Color fallback) { return v && v->Specified() ? *v : fallback; };
            return { pick(color, d.color), pick(pressedColor, d.pressedColor), pick(draggedColor, d.draggedColor) };
        }
        DragHandleShapes shapes() {
            const Shapes& s = MaterialTheme::shapes();
            return { FromToken(s, T::Shape), FromToken(s, T::PressedShape), FromToken(s, T::DraggedShape) };
        }
        DragHandleShapes shapes(ShapePtr shape, ShapePtr pressedShape, ShapePtr draggedShape) {
            const DragHandleShapes d = shapes();
            return { shape ? shape : d.shape, pressedShape ? pressedShape : d.pressedShape, draggedShape ? draggedShape : d.draggedShape };
        }
        DragHandleSizes sizes() {
            return { DpSize{ T::Width, T::Height }, DpSize{ T::PressedWidth, T::PressedHeight }, DpSize{ T::DraggedWidth, T::DraggedHeight } };
        }
        DragHandleSizes sizes(std::optional<DpSize> size, std::optional<DpSize> pressedSize, std::optional<DpSize> draggedSize) {
            const DragHandleSizes d = sizes();
            return { size.value_or(d.size), pressedSize.value_or(d.pressedSize), draggedSize.value_or(d.draggedSize) };
        }
    }

    namespace {
        // pressable: pressed from the first down to the up or cancellation (on the Initial pass,
        // so a draggable after it still gets the gesture).
        rt::Task<> Pressable(PointerInputScope& scope, std::shared_ptr<rt::State<bool>> pressed) {
            for (;;) {
                auto down = co_await AwaitFirstDown(scope, false, ui::PointerEventPass::Initial);
                if (!down) continue;
                pressed->Set(true);
                co_await WaitForUpOrCancellation(scope, ui::PointerEventPass::Initial);
                pressed->Set(false);
            }
        }
    }

    void VerticalDragHandle(const ui::Modifier& modifier, const VerticalDragHandleOptions& options) {
        auto& c = *rt::Composition::Current();
        const DragHandleSizes sizes = options.sizes.value_or(VerticalDragHandleDefaults::sizes());
        const DragHandleColors colors = options.colors.value_or(VerticalDragHandleDefaults::colors());
        const DragHandleShapes shapes = options.shapes.value_or(VerticalDragHandleDefaults::shapes());
        auto remembered = RememberInteractionSource();
        auto source = options.interactionSource ? options.interactionSource : remembered;
        auto& pressedRef = c.Remember<std::shared_ptr<rt::State<bool>>>([] { return std::make_shared<rt::State<bool>>(false); });
        auto pressed = pressedRef;
        auto src = source;

        Modifier m = MinimumInteractiveComponentSize(modifier);
        m = Hoverable(std::move(m), source);
        m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(source.get())),
                         [pressed](PointerInputScope& s) { return Pressable(s, pressed); });
        m = GraphicsLayer(std::move(m), [src, pressed, shapes](GraphicsLayerParams& l) {
            l.clip = true;
            l.clipShape = src->IsDragged() ? shapes.draggedShape : pressed->Get() ? shapes.pressedShape : shapes.shape;
        });
        m = LayoutMod(std::move(m), [src, pressed, sizes](MeasureScope& s, Measurable& measurable, const Constraints&) {
            const DpSize size = src->IsDragged() ? sizes.draggedSize : pressed->Get() ? sizes.pressedSize : sizes.size;
            Placeable& p = measurable.Measure(Constraints::Fixed(int(std::lround(s.ToPx(size.width))), int(std::lround(s.ToPx(size.height)))));
            return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& place) { place.PlaceRelative(p, 0, 0); });
        });
        m = DrawBehind(std::move(m), [src, pressed, colors](DrawScope& d) {
            d.DrawRect(src->IsDragged() ? colors.draggedColor : pressed->Get() ? colors.pressedColor : colors.color, {}, d.GetSize());
        });
        m = IndicationMod(std::move(m), source, Ripple());
        Box(m);
    }

}
