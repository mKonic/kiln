#include "kiln/material3/ListItem.h"

#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/Helpers.h"
#include "kiln/material3/internal/ListItemLayout.h"
#include "kiln/material3/tokens/ListTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace tokens;

    namespace {
        using Content = std::function<void()>;
        enum class Selection { None, Radio, Checkbox };
        struct ColorState {
            bool enabled, selected, dragged;
            bool operator==(const ColorState&) const = default;
        };
        Content Decorate(Content content, Color color, std::optional<text::TextStyle> style, bool box, PaddingValues padding = {}, std::optional<Dp> minimum = {}) {
            if (!content) return {};
            return [=] {
                auto provide = [=] {
                    auto* c = rt::Composition::Current();
                    c->Provide(LocalContentColor, color, [=] {
                        auto styled = [=] { if (style) ProvideTextStyle(*style, content); else content(); };
                        if (minimum) c->Provide(LocalMinimumInteractiveComponentSize, *minimum, styled);
                        else styled();
                    });
                };
                if (box) Box(Padding({}, padding), ui::Alignment::TopStart(), false, provide);
                else provide();
            };
        }
        ShapePtr AnimatedShape(const ListItemShapes& shapes, bool selected, const std::shared_ptr<MutableInteractionSource>& source) {
            auto target = shapes.ShapeForInteraction(selected, source && source->IsPressed(), source && source->IsFocused(), source && source->IsHovered(), source && source->IsDragged());
            for (const auto& shape : { shapes.shape, shapes.selectedShape, shapes.pressedShape, shapes.focusedShape, shapes.hoveredShape, shapes.draggedShape })
                if (!std::dynamic_pointer_cast<const CornerBasedShape>(shape)) return target;
            struct Identity { ListItemShapes shapes; std::size_t version = 0; };
            auto& identity = rt::Composition::Current()->Remember<Identity>([shapes] { return Identity{ shapes }; });
            if (!(identity.shapes == shapes)) { identity.shapes = shapes; ++identity.version; }
            return internal::RememberAnimatedShape(std::dynamic_pointer_cast<const CornerBasedShape>(target), Value(MotionSchemeKeyTokens::FastSpatial), identity.version);
        }
        void Interactive(bool segmented, Selection selection, bool selected, Content click, const ui::Modifier& modifier, const ListItemOptions& o, Content content) {
            auto& c = *rt::Composition::Current();
            const auto source = click ? (o.interactionSource ? o.interactionSource : RememberInteractionSource()) : nullptr;
            const auto colors = o.colors.value_or(segmented ? ListItemDefaults::segmentedColors() : ListItemDefaults::colors());
            const auto shapes = o.shapes.value_or(ListItemDefaults::shapes());
            const auto shape = AnimatedShape(shapes, selected, source);
            const bool dragged = source && source->IsDragged();
            auto& transition = UpdateTransition(ColorState{ o.enabled, selected, dragged });
            const auto effects = Value(MotionSchemeKeyTokens::DefaultEffects);
            const auto spec = [effects](const Transition<ColorState>::Segment&) { return effects; };
            const auto container = AnimateColor<ColorState>(transition, [colors](const ColorState& s) { return colors.ContainerColor(s.enabled, s.selected, s.dragged); }, spec);
            const auto label = AnimateColor<ColorState>(transition, [colors](const ColorState& s) { return colors.ContentColor(s.enabled, s.selected, s.dragged); }, spec);
            const auto leading = AnimateColor<ColorState>(transition, [colors](const ColorState& s) { return colors.LeadingContentColor(s.enabled, s.selected, s.dragged); }, spec);
            const auto trailing = AnimateColor<ColorState>(transition, [colors](const ColorState& s) { return colors.TrailingContentColor(s.enabled, s.selected, s.dragged); }, spec);
            const auto overline = AnimateColor<ColorState>(transition, [colors](const ColorState& s) { return colors.OverlineContentColor(s.enabled, s.selected, s.dragged); }, spec);
            const auto supporting = AnimateColor<ColorState>(transition, [colors](const ColorState& s) { return colors.SupportingContentColor(s.enabled, s.selected, s.dragged); }, spec);
            const auto elevation = o.elevation.value_or(ListItemDefaults::elevation());
            const auto shadow = AnimateDpAsState(dragged ? elevation.draggedElevation : elevation.elevation, Value(MotionSchemeKeyTokens::FastSpatial));
            const auto padding = o.contentPadding.value_or(ListItemDefaults::contentPadding());
            const auto alignment = o.verticalAlignment ? o.verticalAlignment : ListItemDefaults::verticalAlignment();
            const auto density = c.Read(ui::LocalDensity);
            auto m = Semantics(modifier, [=](ui::SemanticsConfiguration& config) {
                if (!click && !o.enabled) config.disabled = true;
                if (selection == Selection::Radio) { config.selected = selected; config.role = ui::Role::RadioButton; }
                if (selection == Selection::Checkbox) { config.toggleableState = selected ? 1 : 0; config.role = ui::Role::Checkbox; }
            }, true);
            if (click) m = MinimumInteractiveComponentSize(m);
            m = ZIndex(m, shadow.value > 0 ? 1 : 0);
            GraphicsLayerParams layer;
            layer.shadowElevation = density.ToPx(shadow);
            layer.clipShape = shape;
            m = Clip(Background(GraphicsLayer(m, layer), container, shape), shape);
            if (click) {
                ClickableOptions clickable;
                clickable.enabled = o.enabled;
                clickable.interactionSource = source;
                clickable.indication = Ripple({ .focusRingShape = shape });
                clickable.useLocalIndication = false;
                clickable.onLongClick = o.onLongClick;
                if (!o.onLongClickLabel.empty()) clickable.onLongClickLabel = o.onLongClickLabel;
                m = Clickable(m, click, clickable);
            }
            const auto minimum = c.Read(LocalMinimumInteractiveComponentSize);
            const float min = minimum.Specified() ? minimum.value : 0;
            const bool swapPadding = !padding.rtlAware && c.Read(ui::LocalLayoutDirection) == ui::LayoutDirection::Rtl;
            const Dp leadingMin(std::max(0.0f, min - (swapPadding ? padding.end : padding.start).value - ListTokens::ItemBetweenSpace.value));
            const Dp trailingMin(std::max(0.0f, min - (swapPadding ? padding.start : padding.end).value - ListTokens::ItemBetweenSpace.value));
            std::array<Content, 5> slots{
                Decorate(content, label, Value(ListTokens::ItemLabelTextFont), true),
                Decorate(o.overlineContent, overline, Value(ListTokens::ItemOverlineFont), true),
                Decorate(o.supportingContent, supporting, Value(ListTokens::ItemSupportingTextFont), true),
                Decorate(o.leadingContent, leading, Value(ListTokens::ItemLeadingAvatarLabelFont), true, { Dp(0), Dp(0), ListTokens::ItemBetweenSpace, Dp(0) }, leadingMin),
                Decorate(o.trailingContent, trailing, Value(ListTokens::ItemTrailingSupportingTextFont), true, { ListTokens::ItemBetweenSpace, Dp(0), Dp(0), Dp(0) }, trailingMin)
            };
            c.Provide(LocalContentColor, label, [=] { internal::ListItemLayout(Padding(m, padding), false, padding.top + padding.bottom, alignment, slots); });
        }
        ListItemOptions SegmentedOptions(const ListItemShapes& shapes, const ListItemOptions& o) { auto result = o; result.shapes = shapes; return result; }
    }

    void ListItem(Content headline, const ui::Modifier& modifier, const LegacyListItemOptions& o) {
        const auto colors = o.colors.value_or(ListItemDefaults::colors());
        SurfaceOptions surface;
        surface.shape = ListItemDefaults::shape();
        surface.color = colors.ContainerColor(true, false, false);
        surface.contentColor = colors.ContentColor(true, false, false);
        surface.tonalElevation = o.tonalElevation;
        surface.shadowElevation = o.shadowElevation;
        std::array<Content, 5> slots{
            Decorate(headline, colors.ContentColor(true, false, false), Value(ListTokens::ItemLabelTextFont), false),
            Decorate(o.overlineContent, colors.OverlineContentColor(true, false, false), Value(ListTokens::ItemOverlineFont), false),
            Decorate(o.supportingContent, colors.SupportingContentColor(true, false, false), Value(ListTokens::ItemSupportingTextFont), false),
            Decorate(o.leadingContent, colors.LeadingContentColor(true, false, false), {}, true, { Dp(0), Dp(0), Dp(16), Dp(0) }),
            Decorate(o.trailingContent, colors.TrailingContentColor(true, false, false), Value(ListTokens::ItemTrailingSupportingTextFont), true, { Dp(16), Dp(0), Dp(0), Dp(0) })
        };
        Surface(Semantics({}, [](ui::SemanticsConfiguration&) {}, true).Then(modifier), surface, [=] { internal::ListItemLayout({}, true, Dp(0), {}, slots); });
    }
    void ListItem(const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(false, Selection::None, false, {}, m, o, std::move(content)); }
    void ListItem(Content click, const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(false, Selection::None, false, std::move(click), m, o, std::move(content)); }
    void ListItem(bool selected, Content click, const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(false, Selection::Radio, selected, std::move(click), m, o, std::move(content)); }
    void ListItem(bool checked, std::function<void(bool)> change, const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(false, Selection::Checkbox, checked, [=] { change(!checked); }, m, o, std::move(content)); }
    void SegmentedListItem(const ListItemShapes& shapes, const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(true, Selection::None, false, {}, m, SegmentedOptions(shapes, o), std::move(content)); }
    void SegmentedListItem(Content click, const ListItemShapes& shapes, const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(true, Selection::None, false, std::move(click), m, SegmentedOptions(shapes, o), std::move(content)); }
    void SegmentedListItem(bool selected, Content click, const ListItemShapes& shapes, const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(true, Selection::Radio, selected, std::move(click), m, SegmentedOptions(shapes, o), std::move(content)); }
    void SegmentedListItem(bool checked, std::function<void(bool)> change, const ListItemShapes& shapes, const ui::Modifier& m, const ListItemOptions& o, Content content) { Interactive(true, Selection::Checkbox, checked, [=] { change(!checked); }, m, SegmentedOptions(shapes, o), std::move(content)); }
}
