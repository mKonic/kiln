#include "kiln/material3/Ripple.h"

#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/ripple/Ripple.h"
#include "kiln/material3/tokens/StateTokens.h"

namespace kiln::material3 {

    namespace RippleDefaults {
        RippleAlpha DefaultRippleAlpha() {
            return { tokens::StateTokens::DraggedStateLayerOpacity, tokens::StateTokens::FocusStateLayerOpacity,
                     tokens::StateTokens::HoverStateLayerOpacity, tokens::StateTokens::PressedStateLayerOpacity };
        }
        RippleThemeConfiguration OpacityFocusThemeConfiguration() { return {}; }
        RippleThemeConfiguration InsetFocusRingThemeConfiguration() {
            return { RippleThemeConfiguration::InsetRing{ ui::Dp(0), ui::Dp(2), ui::Dp(1), ui::Dp(3) } };
        }
    }

    namespace {
        // RippleNodeFactory with DelegatingThemeAwareRippleNode: the theme is resolved where the
        // indication is created (in composition, so theme changes recompose and update it).
        class RippleIndication final : public foundation::Indication {
        public:
            explicit RippleIndication(RippleOptions o) : m_Options(std::move(o)) {
                if (!m_Options.focusRingShape)
                    m_Options.focusRingShape = m_Options.radius.Specified() ? foundation::RoundedCorner(m_Options.radius) : foundation::RectangleShape();
            }

            ui::Modifier Create(ui::Modifier m, std::shared_ptr<foundation::MutableInteractionSource> source) const override {
                rt::Composition* c = rt::Composition::Current();
                const std::optional<RippleConfiguration> configuration =
                    c ? c->Read(LocalRippleConfiguration) : LocalRippleConfiguration.Default();
                // A null configuration removes the ripple.
                if (!configuration) return m;
                const RippleThemeConfiguration theme = c ? c->Read(LocalRippleThemeConfiguration) : RippleThemeConfiguration{};
                const ui::Color contentColor = c ? c->Read(LocalContentColor) : LocalContentColor.Default();
                const MaterialThemeValues& values = c ? c->Read(LocalMaterialTheme()) : LocalMaterialTheme().Default();

                ripple::RippleNodeConfiguration config;
                config.isBounded = m_Options.bounded;
                config.radius = m_Options.radius;
                // calculateColor: the given colour, else the configuration's, else the content colour.
                const ui::Color fallback = configuration->color.Specified() ? configuration->color : contentColor;
                config.color = [producer = m_Options.colorProducer, color = m_Options.color, fallback] {
                    const ui::Color user = producer ? producer() : color;
                    return user.Specified() ? user : fallback;
                };
                const RippleAlpha* alpha = configuration->rippleAlpha ? &*configuration->rippleAlpha : nullptr;
                using Opacity = ripple::RippleNodeConfiguration::Opacity;
                using Focus = ripple::RippleNodeConfiguration::Focus;
                config.press = m_Options.enablePressIndication
                                   ? Opacity::Of(alpha ? alpha->pressedAlpha : tokens::StateTokens::PressedStateLayerOpacity) : Opacity::None();
                config.hover = m_Options.enableHoverIndication
                                   ? Opacity::Of(alpha ? alpha->hoveredAlpha : tokens::StateTokens::HoverStateLayerOpacity) : Opacity::None();
                config.drag = m_Options.enableDragIndication
                                  ? Opacity::Of(alpha ? alpha->draggedAlpha : tokens::StateTokens::DraggedStateLayerOpacity) : Opacity::None();
                if (!m_Options.enableFocusIndication) {
                    config.focus = Focus::None();
                } else if (!theme.focus) {
                    config.focus = Focus::Of(alpha ? alpha->focusedAlpha : tokens::StateTokens::FocusStateLayerOpacity);
                } else {
                    ripple::RippleNodeConfiguration::InsetRing ring;
                    ring.shape = m_Options.focusRingShape;
                    ring.outerStrokeInset = theme.focus->outerStrokeInset;
                    ring.outerStrokeWidth = theme.focus->outerStrokeWidth;
                    ring.innerStrokeInset = theme.focus->innerStrokeInset;
                    ring.innerStrokeWidth = theme.focus->innerStrokeWidth;
                    // calculateOuterStrokeColor / calculateInnerStrokeColor
                    const ui::Color outer = configuration->focus ? configuration->focus->outerStrokeColor : values.colorScheme.secondary;
                    const ui::Color inner = configuration->focus ? configuration->focus->innerStrokeColor : values.colorScheme.onSecondary;
                    ring.outerStrokeColor = [outer] { return outer; };
                    ring.innerStrokeColor = [inner] { return inner; };
                    ring.focusingAnimationSpec = values.motionScheme->FastSpatialSpec();
                    ring.unfocusingAnimationSpec = values.motionScheme->FastEffectsSpec();
                    config.focus = Focus::Ring(std::move(ring));
                }
                return ripple::RippleModifier(std::move(m), std::move(source), std::move(config));
            }

        private:
            RippleOptions m_Options;
        };
    }

    foundation::IndicationPtr Ripple(const RippleOptions& options) {
        const bool defaults = !options.radius.Specified() && !options.color.Specified() && !options.colorProducer && !options.focusRingShape &&
                              options.enablePressIndication && options.enableFocusIndication && options.enableHoverIndication &&
                              options.enableDragIndication;
        // DefaultBoundedRipple / DefaultUnboundedRipple
        if (defaults) {
            static const foundation::IndicationPtr bounded = std::make_shared<RippleIndication>(RippleOptions{});
            static const foundation::IndicationPtr unbounded = std::make_shared<RippleIndication>([] { RippleOptions o; o.bounded = false; return o; }());
            return options.bounded ? bounded : unbounded;
        }
        return std::make_shared<RippleIndication>(options);
    }

}
