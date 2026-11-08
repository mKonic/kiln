#include "kiln/material3/FloatingActionButtonMenu.h"

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/tokens/FabBaselineTokens.h"
#include "kiln/material3/tokens/FabMediumTokens.h"
#include "kiln/material3/tokens/FabLargeTokens.h"
#include "kiln/material3/tokens/FabMenuBaselineTokens.h"
#include "kiln/material3/tokens/FabPrimaryContainerTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

#include <cmath>

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;
    using ui::Dp;

    namespace {
        struct SizeParams { Dp size; bool fixedChild = false; bool operator==(const SizeParams&) const = default; };
        struct SizeNode final : ui::ModifierNode, ui::LayoutModifier {
            SizeParams params;
            explicit SizeNode(SizeParams p) : params(p) {}
            ui::LayoutModifier* AsLayout() override { return this; }
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& m, const ui::Constraints& c) override {
                const int size = s.RoundToPx(params.size);
                auto& p = m.Measure(params.fixedChild ? ui::Constraints::Fixed(size, size) : c);
                return s.Layout(size, size, [&p, size](ui::PlacementScope& ps) { ps.Place(p, (size - p.Width()) / 2, (size - p.Height()) / 2); });
            }
        };
        std::function<Dp(float)> Sizes(Dp start, Dp end) {
            return [=](float progress) { return Dp(start.value + (end.value - start.value) * progress); };
        }
    }

    void ToggleFloatingActionButton(bool checked, std::function<void(bool)> change, const ui::Modifier& modifier,
                                    const ToggleFloatingActionButtonOptions& options, std::function<void(const ToggleFloatingActionButtonScope&)> content) {
        const float progress = AnimateFloatAsState(checked ? 1.0f : 0.0f, Value(MotionSchemeKeyTokens::FastSpatial));
        const auto size = options.containerSize ? options.containerSize : ToggleFloatingActionButtonDefaults::containerSize();
        const auto radius = options.containerCornerRadius ? options.containerCornerRadius : ToggleFloatingActionButtonDefaults::containerCornerRadius();
        const auto color = options.containerColor ? options.containerColor : ToggleFloatingActionButtonDefaults::containerColor();
        const Dp initial = size(0);
        const auto shape = RoundedCorner(radius(progress));
        Box(SizeMod({}, initial, initial), options.contentAlignment, false, [=] {
            GraphicsLayerParams layer;
            layer.shadowElevation = rt::Composition::Current()->Read(ui::LocalDensity).ToPx(FabPrimaryContainerTokens::ContainerElevation);
            layer.clipShape = shape;
            layer.clip = true;
            auto m = Background(GraphicsLayer(modifier, layer), color(progress), shape);
            ClickableOptions toggle;
            RippleOptions ripple;
            ripple.radius = Dp(std::hypot(initial.value / 2, initial.value / 2));
            ripple.focusRingShape = shape;
            toggle.indication = Ripple(ripple);
            toggle.useLocalIndication = false;
            m = Toggleable(m, checked, change, toggle);
            m = m.With<SizeNode>(SizeParams{ size(progress) });
            Box(m, ui::Alignment::TopStart(), false, [=] { content({ progress }); });
        });
    }

    namespace ToggleFloatingActionButtonDefaults {
        std::function<ui::Color(float)> containerColor(ui::Color initial, ui::Color final) {
            return [=](float progress) { return ui::Lerp(initial, final, progress); };
        }
        std::function<ui::Color(float)> containerColor() {
            const auto& scheme = MaterialTheme::colorScheme();
            return containerColor(scheme.primaryContainer, scheme.primary);
        }
        std::function<Dp(float)> containerSize(Dp initial, Dp final) { return Sizes(initial, final); }
        std::function<Dp(float)> containerSize() { return containerSize(FabBaselineTokens::ContainerHeight, FabMenuBaselineTokens::CloseButtonContainerHeight); }
        std::function<Dp(float)> containerSizeMedium() { return containerSize(FabMediumTokens::ContainerHeight, FabMenuBaselineTokens::CloseButtonContainerHeight); }
        std::function<Dp(float)> containerSizeLarge() { return containerSize(FabLargeTokens::ContainerHeight, FabMenuBaselineTokens::CloseButtonContainerHeight); }
        std::function<Dp(float)> containerCornerRadius(Dp initial, Dp final) { return Sizes(initial, final); }
        std::function<Dp(float)> containerCornerRadius() { return containerCornerRadius(Dp(16)); }
        std::function<Dp(float)> containerCornerRadiusMedium() { return containerCornerRadius(Dp(20)); }
        std::function<Dp(float)> containerCornerRadiusLarge() { return containerCornerRadius(Dp(28)); }
        std::function<ui::Color(float)> iconColor(ui::Color initial, ui::Color final) {
            return [=](float progress) { return ui::Lerp(initial, final, progress); };
        }
        std::function<ui::Color(float)> iconColor() {
            const auto& scheme = MaterialTheme::colorScheme();
            return iconColor(scheme.onPrimaryContainer, scheme.onPrimary);
        }
        std::function<Dp(float)> iconSize(Dp initial, Dp final) { return Sizes(initial, final); }
        std::function<Dp(float)> iconSize() { return iconSize(FabBaselineTokens::IconSize, FabMenuBaselineTokens::CloseButtonIconSize); }
        std::function<Dp(float)> iconSizeMedium() { return iconSize(FabMediumTokens::IconSize, FabMenuBaselineTokens::CloseButtonIconSize); }
        std::function<Dp(float)> iconSizeLarge() { return iconSize(Dp(36), FabMenuBaselineTokens::CloseButtonIconSize); }
        ui::Modifier AnimateIcon(ui::Modifier m, float progress, std::function<ui::Color(float)> color, std::function<Dp(float)> size) {
            if (!color) color = iconColor();
            if (!size) size = iconSize();
            m = m.With<SizeNode>(SizeParams{ size(progress), true });
            GraphicsLayerParams layer;
            layer.colorFilter = gfx::ColorFilter::Tint(color(progress));
            return GraphicsLayer(std::move(m), layer);
        }
    }
}
