#include "kiln/material3/Surface.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/internal/ChildParentSemantics.h"
#include "kiln/ui/Locals.h"

namespace kiln::material3 {

    using foundation::ShapePtr;
    using ui::Color;
    using ui::Dp;
    using ui::Modifier;

    namespace {
        rt::Composition& Comp() { return *rt::Composition::Current(); }

        // Modifier.surface: the shadow layer, the border, the background, then the clip.
        Modifier SurfaceModifier(Modifier m, const ShapePtr& shape, Color backgroundColor, const std::optional<foundation::BorderStroke>& border,
                                 float shadowElevation) {
            if (shadowElevation > 0) {
                foundation::GraphicsLayerParams p;
                p.shadowElevation = shadowElevation;
                p.clipShape = shape;
                p.clip = false;
                m = foundation::GraphicsLayer(std::move(m), p);
            }
            if (border) m = foundation::Border(std::move(m), *border, shape);
            m = foundation::Background(std::move(m), backgroundColor, shape);
            return foundation::Clip(std::move(m), shape);
        }

        struct Resolved {
            ShapePtr shape;
            Color color, contentColor;
            Dp absoluteElevation;
        };
        Resolved Resolve(const SurfaceOptions& o) {
            Resolved r;
            r.shape = o.shape ? o.shape : foundation::RectangleShape();
            r.color = o.color ? *o.color : MaterialTheme::colorScheme().surface;
            r.contentColor = o.contentColor ? *o.contentColor : ContentColorFor(r.color);
            r.absoluteElevation = Dp(Comp().Read(LocalAbsoluteTonalElevation).value + o.tonalElevation.value);
            return r;
        }

        // CompositionLocalProvider(LocalContentColor, LocalAbsoluteTonalElevation) { Box(...) }.
        void Provide(const Resolved& r, std::function<void()> body) {
            auto& c = Comp();
            c.Provide(LocalContentColor, r.contentColor, [&c, r, body = std::move(body)] {
                c.Provide(LocalAbsoluteTonalElevation, r.absoluteElevation, body);
            });
        }

        float ShadowPx(Dp elevation) { return Comp().Read(ui::LocalDensity).ToPx(elevation); }

        // The interactive forms: minimum size, the focus ring indication when the theme asks
        // for inset rings, the surface, then `interactive` (clickable / selectable / toggleable)
        // with the ripple, and childSemantics.
        void Interactive(const SurfaceOptions& options, const Modifier& modifier, std::shared_ptr<foundation::MutableInteractionSource> source,
                         std::function<Modifier(Modifier, foundation::ClickableOptions)> interactive, bool enabled, std::function<void()> content) {
            auto& c = Comp();
            if (!source) source = c.Remember<std::shared_ptr<foundation::MutableInteractionSource>>(
                [] { return std::make_shared<foundation::MutableInteractionSource>(); });
            const Resolved r = Resolve(options);
            const float shadow = ShadowPx(options.shadowElevation);
            const bool insetRing = c.Read(LocalRippleThemeConfiguration).focus.has_value();
            Provide(r, [=, &c] {
                Modifier m = MinimumInteractiveComponentSize(modifier);
                if (insetRing) {
                    RippleOptions ring;
                    ring.focusRingShape = r.shape;
                    ring.enablePressIndication = false;
                    ring.enableFocusIndication = true;
                    ring.enableDragIndication = false;
                    ring.enableHoverIndication = false;
                    m = Ripple(ring)->Create(std::move(m), source);
                }
                m = SurfaceModifier(std::move(m), r.shape, ApplyTonalElevation(MaterialTheme::colorScheme(), r.color, r.absoluteElevation),
                                    options.border, shadow);
                foundation::ClickableOptions co;
                co.interactionSource = source;
                RippleOptions ripple;
                ripple.focusRingShape = r.shape;
                ripple.enableFocusIndication = !insetRing;
                co.indication = Ripple(ripple);
                co.enabled = enabled;
                m = internal::ChildSemantics(interactive(std::move(m), std::move(co)));
                foundation::Box(m, foundation::Alignment::TopStart(), true, content);
                (void)c;
            });
        }
    }

    void Surface(const Modifier& modifier, const SurfaceOptions& options, std::function<void()> content) {
        const Resolved r = Resolve(options);
        const float shadow = ShadowPx(options.shadowElevation);
        Provide(r, [=] {
            Modifier m = SurfaceModifier(modifier, r.shape, ApplyTonalElevation(MaterialTheme::colorScheme(), r.color, r.absoluteElevation),
                                         options.border, shadow);
            // isContainer (isTraversalGroup), and an empty pointerInput that keeps touches here.
            m = foundation::Semantics(std::move(m), [](ui::SemanticsConfiguration& c) { c.isTraversalGroup = true; });
            m = foundation::PointerInput(std::move(m), rt::Keys(0), [](foundation::PointerInputScope&) -> rt::Task<> { co_return; });
            foundation::Box(m, foundation::Alignment::TopStart(), true, content);
        });
    }

    void Surface(std::function<void()> onClick, const Modifier& modifier, const SurfaceOptions& options, std::function<void()> content, bool enabled,
                 std::shared_ptr<foundation::MutableInteractionSource> interactionSource) {
        Interactive(options, modifier, std::move(interactionSource),
                    [onClick = std::move(onClick)](Modifier m, foundation::ClickableOptions co) { return foundation::Clickable(std::move(m), onClick, std::move(co)); },
                    enabled, std::move(content));
    }

    void Surface(bool selected, std::function<void()> onClick, const Modifier& modifier, const SurfaceOptions& options, std::function<void()> content,
                 bool enabled, std::shared_ptr<foundation::MutableInteractionSource> interactionSource) {
        Interactive(options, modifier, std::move(interactionSource),
                    [selected, onClick = std::move(onClick)](Modifier m, foundation::ClickableOptions co) {
                        return foundation::Selectable(std::move(m), selected, onClick, std::move(co));
                    },
                    enabled, std::move(content));
    }

    void Surface(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const SurfaceOptions& options,
                 std::function<void()> content, bool enabled, std::shared_ptr<foundation::MutableInteractionSource> interactionSource) {
        Interactive(options, modifier, std::move(interactionSource),
                    [checked, onCheckedChange = std::move(onCheckedChange)](Modifier m, foundation::ClickableOptions co) {
                        return foundation::Toggleable(std::move(m), checked, onCheckedChange, std::move(co));
                    },
                    enabled, std::move(content));
    }

    namespace internal {
        void Surface(std::function<Color()> backgroundColor, const Modifier& modifier, Dp tonalElevation, Dp shadowElevation, std::function<void()> content) {
            auto& c = Comp();
            const Dp absolute = Dp(c.Read(LocalAbsoluteTonalElevation).value + tonalElevation.value);
            const bool tonalElevationEnabled = c.Read(LocalTonalElevationEnabled);
            const ColorScheme scheme = MaterialTheme::colorScheme();
            const float shadowPx = ShadowPx(shadowElevation);
            c.Provide(LocalAbsoluteTonalElevation, absolute, [=] {
                Modifier m = modifier;
                if (shadowElevation.value > 0) {
                    // graphicsLayer { shadowElevation = ...; clip = false } (the default shape).
                    m = foundation::GraphicsLayer(std::move(m), [shadowPx](foundation::GraphicsLayerParams& p) {
                        p.shadowElevation = shadowPx;
                        p.clip = false;
                    });
                }
                m = foundation::DrawBehind(std::move(m), [=](ui::DrawScope& s) {
                    const Color resolved = backgroundColor();
                    // Note: the tonal colour uses this surface's own tonal elevation, as Compose does.
                    const Color color = resolved == scheme.surface && tonalElevationEnabled ? SurfaceColorAtElevation(scheme, tonalElevation) : resolved;
                    s.DrawRect(color, {}, s.GetSize());
                });
                foundation::Box(m, foundation::Alignment::TopStart(), false, content);
            });
        }
    }

}
