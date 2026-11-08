#include "kiln/material3/MaterialTheme.h"

#include "kiln/foundation/text/Selection.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/internal/PrecisionPointer.h"
#include "kiln/material3/tokens/TypographyTokens.h"

#include <cmath>

namespace kiln::material3 {

    namespace {
        rt::Composition& Comp() { return *rt::Composition::Current(); }
        // LocalUsingExpressiveTheme
        rt::Local<bool> LocalUsingExpressiveTheme{ false };
    }

    const rt::Local<MaterialThemeValues>& LocalMaterialTheme() {
        static const rt::Local<MaterialThemeValues> local{ MaterialThemeValues{} };
        return local;
    }

    const ColorScheme& MaterialTheme::colorScheme() { return Comp().Read(LocalMaterialTheme()).colorScheme; }
    const Typography& MaterialTheme::typography() { return Comp().Read(LocalMaterialTheme()).typography; }
    const Shapes& MaterialTheme::shapes() { return Comp().Read(LocalMaterialTheme()).shapes; }
    const MotionSchemePtr& MaterialTheme::motionScheme() { return Comp().Read(LocalMaterialTheme()).motionScheme; }

    void MaterialTheme(const MaterialThemeArgs& args, std::function<void()> content) {
        auto& c = Comp();
        const MaterialThemeValues& current = c.Read(LocalMaterialTheme());
        MaterialThemeValues theme;
        theme.colorScheme = args.colorScheme.value_or(current.colorScheme);
        theme.motionScheme = args.motionScheme.value_or(current.motionScheme);
        theme.shapes = args.shapes.value_or(current.shapes);
        theme.typography = args.typography.value_or(current.typography);
        // rememberTextSelectionColors(colorScheme)
        const Color primary = theme.colorScheme.primary;
        const foundation::TextSelectionColors selection{ primary, primary.WithAlpha(TextSelectionBackgroundOpacity) };
        const text::TextStyle body = theme.typography.bodyLarge;
        c.Provide(LocalMaterialTheme(), std::move(theme), [&c, selection, body, content = std::move(content)] {
            c.Provide(foundation::LocalIndication, Ripple(), [&c, selection, body, content] {
                c.Provide(foundation::LocalTextSelectionColors, selection, [body, content] {
                    internal::EnsurePrecisionPointerListenersRegistered([body, content] { ProvideTextStyle(body, content); });
                });
            });
        });
    }

    void MaterialExpressiveTheme(const MaterialThemeArgs& args, std::function<void()> content) {
        auto& c = Comp();
        if (c.Read(LocalUsingExpressiveTheme)) {
            MaterialTheme(args, std::move(content));
            return;
        }
        MaterialThemeArgs expressive;
        expressive.colorScheme = args.colorScheme ? *args.colorScheme : ExpressiveLightColorScheme();
        expressive.motionScheme = args.motionScheme ? *args.motionScheme : MotionScheme::Expressive();
        expressive.shapes = args.shapes ? *args.shapes : Shapes{};
        expressive.typography = args.typography ? *args.typography : Typography{};
        c.Provide(LocalUsingExpressiveTheme, true, [expressive, content = std::move(content)] { MaterialTheme(expressive, content); });
    }

    Color ContentColorFor(Color backgroundColor) {
        const Color c = ContentColorFor(MaterialTheme::colorScheme(), backgroundColor);
        return c.Specified() ? c : Comp().Read(LocalContentColor);
    }

    const rt::Local<text::TextStyle>& LocalTextStyle() {
        static const rt::Local<text::TextStyle> local{ tokens::DefaultTextStyle() };
        return local;
    }

    void ProvideTextStyle(const text::TextStyle& value, std::function<void()> content) {
        auto& c = Comp();
        text::TextStyle merged = c.Read(LocalTextStyle()).Merge(value);
        c.Provide(LocalTextStyle(), std::move(merged), std::move(content));
    }

    Color ApplyTonalElevation(const ColorScheme& scheme, Color backgroundColor, ui::Dp elevation) {
        const bool enabled = Comp().Read(LocalTonalElevationEnabled);
        return backgroundColor == scheme.surface && enabled ? SurfaceColorAtElevation(scheme, elevation) : backgroundColor;
    }

    Color Value(tokens::ColorToken token) { return FromToken(MaterialTheme::colorScheme(), token); }
    const text::TextStyle& Value(tokens::TypographyToken token) { return FromToken(MaterialTheme::typography(), token); }
    foundation::ShapePtr Value(tokens::ShapeToken token) { return FromToken(MaterialTheme::shapes(), token); }
    anim::SpecPtr Value(tokens::MotionSchemeToken token) { return FromToken(*MaterialTheme::motionScheme(), token); }

    // --- minimumInteractiveComponentSize ---------------------------------------------------------

    namespace {
        struct MinimumInteractiveParams {
            ui::Dp size;
            bool operator==(const MinimumInteractiveParams&) const = default;
        };
        // MinimumInteractiveModifierNode
        struct MinimumInteractiveNode final : ui::ModifierNode, ui::LayoutModifier {
            MinimumInteractiveParams params;
            explicit MinimumInteractiveNode(MinimumInteractiveParams p) : params(p) {}
            void OnParamsChanged() { InvalidateMeasure(); }
            ui::LayoutModifier* AsLayout() override { return this; }

            ui::MeasureResult Measure(ui::MeasureScope& s, ui::Measurable& wrapped, const ui::Constraints& c) override {
                const ui::Dp size = params.size.Specified() ? ui::Dp(std::max(params.size.value, 0.0f)) : params.size;
                ui::Placeable& placeable = wrapped.Measure(c);
                const bool enforcement = Owner() != nullptr && size.Specified() && size.value > 0;
                const int sizePx = size.Specified() ? s.RoundToPx(size) : 0;
                const int width = enforcement ? std::max(placeable.Width(), sizePx) : placeable.Width();
                const int height = enforcement ? std::max(placeable.Height(), sizePx) : placeable.Height();
                std::vector<std::pair<const ui::AlignmentLine*, int>> lines;
                if (enforcement) {
                    // updateAlignmentLines
                    lines.push_back({ &MinimumInteractiveLeftAlignmentLine, std::max(int(std::lround((sizePx - placeable.Width()) / 2.0f)), 0) });
                    lines.push_back({ &MinimumInteractiveTopAlignmentLine, std::max(int(std::lround((sizePx - placeable.Height()) / 2.0f)), 0) });
                }
                return s.Layout(width, height, [&placeable, width, height](ui::PlacementScope& p) {
                    const int centerX = int(std::lround((width - placeable.Width()) / 2.0f));
                    const int centerY = int(std::lround((height - placeable.Height()) / 2.0f));
                    p.Place(placeable, centerX, centerY);
                }, std::move(lines));
            }
        };
    }

    ui::Modifier MinimumInteractiveComponentSize(ui::Modifier m) {
        rt::Composition* c = rt::Composition::Current();
        const ui::Dp size = c ? c->Read(LocalMinimumInteractiveComponentSize) : LocalMinimumInteractiveComponentSize.Default();
        return std::move(m).With<MinimumInteractiveNode>(MinimumInteractiveParams{ size });
    }

}
