#include "kiln/material3/Menu.h"

#include "kiln/foundation/Animate.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Hover.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Ripple.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/PrecisionPointer.h"
#include "kiln/material3/tokens/ElevationTokens.h"
#include "kiln/material3/tokens/ListTokens.h"
#include "kiln/material3/tokens/MenuTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/SegmentedMenuTokens.h"
#include "kiln/material3/tokens/ShapeTokens.h"
#include "kiln/material3/tokens/StandardMenuTokens.h"
#include "kiln/material3/tokens/VibrantMenuTokens.h"

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using namespace tokens;

    namespace {
        Color Or(Color c, Color fallback) { return c.Specified() ? c : fallback; }

        constexpr float ExpandedScaleTarget = 1, ClosedScaleTarget = 0.8f, ExpandedAlphaTarget = 1, ClosedAlphaTarget = 0;
        constexpr Dp MenuListItemContainerHeight{ 48 }, DropdownMenuItemHorizontalPadding{ 12 }, DropdownMenuGroupVerticalPadding{ 2 },
            DropdownMenuVerticalPadding{ 8 }, DropdownMenuItemDefaultMinWidth{ 112 }, DropdownMenuItemDefaultMaxWidth{ 280 },
            DropdownMenuGroupDefaultMinHeight{ 32 };
        const PaddingValues DropdownMenuSelectableItemPadding{ Dp(4), Dp(0), Dp(4), Dp(0) };
        const PaddingValues DropdownMenuSelectableItemWithSupportTextPadding{ Dp(4), Dp(2), Dp(4), Dp(2) };

        // MenuArrangement: the leading icon at the start, the text after it, trailing content at the end.
        struct MenuArrangementPolicy final : ArrangementPolicy {
            Dp leadingSpacing, trailingSpacing;
            bool hasLeadingIcon, hasTrailingIcon;
            MenuArrangementPolicy(Dp spacing, bool leading, bool trailing)
                : leadingSpacing(spacing), trailingSpacing(spacing), hasLeadingIcon(leading), hasTrailingIcon(trailing) {}
            Dp Spacing() const override { return Dp((leadingSpacing.value + trailingSpacing.value) / 2); }
            void Arrange(const Density& d, int total, std::span<const int> sizes, LayoutDirection dir, std::span<int> out) const override {
                if (sizes.empty()) return;
                const int spacing1 = d.RoundToPx(leadingSpacing);
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    const int size = sizes[i];
                    int x = 0;
                    if (i == 0) x = 0;
                    else if (i == 1 && hasLeadingIcon) x = sizes[0] + spacing1;
                    else if (i == 1 && hasTrailingIcon) x = total - size;
                    else if (i == 2) x = total - size;
                    out[i] = dir == LayoutDirection::Ltr ? x : total - x - size;
                }
            }
            bool Equals(const ArrangementPolicy& other) const override {
                auto* o = dynamic_cast<const MenuArrangementPolicy*>(&other);
                return o && o->leadingSpacing == leadingSpacing && o->trailingSpacing == trailingSpacing && o->hasLeadingIcon == hasLeadingIcon &&
                       o->hasTrailingIcon == hasTrailingIcon;
            }
        };
        Arrangement ItemHorizontalArrangement(bool hasLeadingIcon = true, bool hasTrailingIcon = true) {
            const Dp spacing = internal::ShouldUsePrecisionPointerComponentSizing() ? Dp(12) : Dp(8);
            return Arrangement::Custom(std::make_shared<MenuArrangementPolicy>(spacing, hasLeadingIcon, hasTrailingIcon));
        }

        ShapePtr Rounded(CornerSize top, CornerSize bottom) { return RoundedCorner(top, top, bottom, bottom); }

        // The menu's scale and fade on its expanded transition.
        struct MenuAnimation {
            float scale, alpha;
        };
        MenuAnimation AnimateMenu(MutableTransitionState<bool>& expandedState) {
            auto& transition = UpdateTransition(expandedState);
            const anim::SpecPtr scaleSpec = Value(MotionSchemeKeyTokens::FastSpatial), alphaSpec = Value(MotionSchemeKeyTokens::FastEffects);
            const float scale = AnimateFloat<bool>(transition, [](const bool& e) { return e ? ExpandedScaleTarget : ClosedScaleTarget; },
                                                   [scaleSpec](const auto&) { return scaleSpec; });
            const float alpha = AnimateFloat<bool>(transition, [](const bool& e) { return e ? ExpandedAlphaTarget : ClosedAlphaTarget; },
                                                   [alphaSpec](const auto&) { return alphaSpec; });
            return { scale, alpha };
        }
        Modifier MenuLayer(Modifier m, MenuAnimation a, std::shared_ptr<const DropdownMenuPopupPositionProvider> provider) {
            return GraphicsLayer(std::move(m), [a, provider](GraphicsLayerParams& p) {
                p.scaleX = a.scale;
                p.scaleY = a.scale;
                p.alpha = a.alpha;
                const auto origin = provider->GetTransformOrigin();
                p.originX = origin.pivotFractionX;
                p.originY = origin.pivotFractionY;
            });
        }

        void DropdownMenuContent(const Modifier& modifier, MutableTransitionState<bool>& expandedState,
                                 std::shared_ptr<const DropdownMenuPopupPositionProvider> provider, ScrollState& scrollState, ShapePtr shape,
                                 Color containerColor, Dp tonalElevation, Dp shadowElevation, std::optional<BorderStroke> border,
                                 std::function<void()> content) {
            const MenuAnimation a = AnimateMenu(expandedState);
            SurfaceOptions surface;
            surface.shape = std::move(shape);
            surface.color = containerColor;
            surface.tonalElevation = tonalElevation;
            surface.shadowElevation = shadowElevation;
            surface.border = std::move(border);
            const Modifier column = VerticalScroll(Width(Padding(modifier, Dp(0), DropdownMenuVerticalPadding), IntrinsicSize::Max), scrollState);
            Surface(MenuLayer({}, a, std::move(provider)), surface,
                    [column, content = std::move(content)] { Column(column, Arrangement::Top(), AlignmentH::Start(), content); });
        }

        void ProvideContentColor(Color color, std::function<void()> content) {
            rt::Composition::Current()->Provide(LocalContentColor, color, std::move(content));
        }

        // shapeByInteraction: morphs between corner-based shapes; others switch at once.
        ShapePtr ItemShapeByInteraction(const MenuItemShapes& shapes, bool selected, anim::SpecPtr spec) {
            const ShapePtr shape = selected ? shapes.selectedShape : shapes.shape;
            auto a = std::dynamic_pointer_cast<const CornerBasedShape>(shapes.shape);
            auto b = std::dynamic_pointer_cast<const CornerBasedShape>(shapes.selectedShape);
            if (a && b) {
                const std::size_t key = std::hash<const void*>{}(shapes.shape.get()) ^ (std::hash<const void*>{}(shapes.selectedShape.get()) << 1);
                return internal::RememberAnimatedShape(std::dynamic_pointer_cast<const CornerBasedShape>(shape), std::move(spec), key);
            }
            return shape;
        }
        ShapePtr GroupShapeByInteraction(const MenuGroupShapes& shapes, bool hasBeenHovered, bool hovered, anim::SpecPtr spec) {
            const ShapePtr shape = hasBeenHovered && !hovered ? shapes.inactiveShape : shapes.shape;
            auto a = std::dynamic_pointer_cast<const CornerBasedShape>(shapes.shape);
            auto b = std::dynamic_pointer_cast<const CornerBasedShape>(shapes.inactiveShape);
            if (a && b) {
                const std::size_t key = std::hash<const void*>{}(shapes.shape.get()) ^ (std::hash<const void*>{}(shapes.inactiveShape.get()) << 1);
                return internal::RememberAnimatedShape(std::dynamic_pointer_cast<const CornerBasedShape>(shape), std::move(spec), key);
            }
            return shape;
        }

        void WrappedLeadingIcon(std::function<void()> content) {
            Box(DefaultMinSize({}, SegmentedMenuTokens::ItemLeadingIconSize, Dp::Unspecified()), Alignment::TopStart(), false, std::move(content));
        }

        void LabelWithSupportingText(std::function<void()> supportingText, std::function<void()> content) {
            Column({}, Arrangement::Top(), AlignmentH::Start(), [&] {
                ProvideTextStyle(MaterialTheme::typography().labelLarge, content);
                ProvideTextStyle(MaterialTheme::typography().bodyMedium, supportingText);
            });
        }

        struct ItemColors {
            Color container, leadingIcon, text, trailingContent;
        };
        // The expressive item (Menu.kt's private DropdownMenuItemContent).
        void ExpressiveItemContent(bool selected, std::function<void()> onClick, std::function<void()> text, const Modifier& modifier,
                                   const MenuItemOptions& o, ItemColors colors, const MenuItemShapes& shapes) {
            auto& c = *rt::Composition::Current();
            const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
            const anim::SpecPtr expandAndShrinkSpec = Value(MotionSchemeKeyTokens::FastSpatial), fadeSpec = Value(MotionSchemeKeyTokens::FastEffects),
                                morphSpec = Value(MotionSchemeKeyTokens::FastSpatial), colorSpec = Value(MotionSchemeKeyTokens::FastEffects);
            const Color container = AnimateColorAsState(colors.container, colorSpec);
            const ShapePtr itemShape = ItemShapeByInteraction(shapes, selected, morphSpec);
            const bool hasLeadingIcon = bool(o.leadingIcon) || bool(o.selectedLeadingIcon);
            const bool hasTrailingContent = bool(o.trailingContent);
            Arrangement arrangement = o.horizontalArrangement ? *o.horizontalArrangement : MenuDefaults::DropdownMenuItemHorizontalArrangement();
            if (dynamic_cast<const MenuArrangementPolicy*>(arrangement.policy.get()))
                arrangement = ItemHorizontalArrangement(hasLeadingIcon, hasTrailingContent);
            const PaddingValues contentPadding = o.contentPadding ? *o.contentPadding : MenuDefaults::DropdownMenuSelectableItemContentPadding();
            (void)c;

            SurfaceOptions surface;
            surface.shape = itemShape;
            surface.color = container;
            const Modifier m = Padding(FillMax(modifier, Axis::Horizontal),
                                       o.supportingText ? DropdownMenuSelectableItemWithSupportTextPadding : DropdownMenuSelectableItemPadding);
            auto body = [=] {
                ProvideTextStyle(MaterialTheme::typography().labelLarge, [&] {
                    const Modifier row = Padding(SizeIn({}, DropdownMenuItemDefaultMinWidth, SegmentedMenuTokens::Item, DropdownMenuItemDefaultMaxWidth,
                                                        Dp::Unspecified()),
                                                 contentPadding);
                    Row(row, arrangement, AlignmentV::CenterVertically(), [&] {
                        if (hasLeadingIcon) {
                            ProvideContentColor(colors.leadingIcon, [&] {
                                Box({}, Alignment::Center(), false, [&] {
                                    if (o.selectedLeadingIcon) {
                                        if (!o.leadingIcon) {
                                            AnimatedVisibility(selected, {}, [&] { WrappedLeadingIcon(o.selectedLeadingIcon); },
                                                               ExpandHorizontally(expandAndShrinkSpec) + FadeIn(fadeSpec),
                                                               ShrinkHorizontally(expandAndShrinkSpec) + FadeOut(fadeSpec));
                                        } else if (selected) {
                                            WrappedLeadingIcon(o.selectedLeadingIcon);
                                        } else {
                                            WrappedLeadingIcon(o.leadingIcon);
                                        }
                                    } else {
                                        WrappedLeadingIcon(o.leadingIcon);
                                    }
                                });
                            });
                        }
                        // Weighted without filling: measured after the icons, in the space they
                        // leave, at its own width when it fits.
                        ProvideContentColor(colors.text, [&] {
                            Box(Weight({}, 1, false), Alignment::CenterStart(), false, [&] {
                                if (o.supportingText) LabelWithSupportingText(o.supportingText, text);
                                else text();
                            });
                        });
                        if (hasTrailingContent) {
                            ProvideContentColor(colors.trailingContent, [&] {
                                Box(DefaultMinSize({}, SegmentedMenuTokens::ItemTrailingIconSize, Dp::Unspecified()), Alignment::Center(), false,
                                    o.trailingContent);
                            });
                        }
                    });
                });
            };
            Surface(selected, std::move(onClick), m, surface, body, o.enabled, source);
        }

        Modifier WithRole(const Modifier& modifier, Role role) {
            return Semantics(modifier, [role](SemanticsConfiguration& config) { config.role = role; });
        }

        // Cached per theme shapes, so the morph state keyed on them survives recomposition.
        struct ShapeCache {
            const Shapes* shapes = nullptr;
            MenuItemShapes standalone, leading, middle, trailing;
            MenuGroupShapes standaloneGroup, leadingGroup, middleGroup, trailingGroup;
        };
        const ShapeCache& Cache() {
            thread_local ShapeCache cache;
            const Shapes& shapes = MaterialTheme::shapes();
            if (cache.shapes != &shapes || !cache.standalone.shape) {
                cache.shapes = &shapes;
                const ShapePtr selected = Value(SegmentedMenuTokens::ItemSelectedShape);
                const ShapePtr inactive = Value(SegmentedMenuTokens::InactiveContainerShape);
                cache.standalone = { Value(SegmentedMenuTokens::ItemShape), selected };
                cache.middle = { Value(SegmentedMenuTokens::ItemShape), selected };
                cache.leading = { Rounded(ShapeTokens::CornerValueMedium, ShapeTokens::CornerValueExtraSmall), selected };
                cache.trailing = { Rounded(ShapeTokens::CornerValueExtraSmall, ShapeTokens::CornerValueMedium), selected };
                cache.standaloneGroup = { Value(SegmentedMenuTokens::ContainerShape), inactive };
                cache.middleGroup = { Value(SegmentedMenuTokens::GroupShape), inactive };
                cache.leadingGroup = { Rounded(ShapeTokens::CornerValueLarge, ShapeTokens::CornerValueSmall), inactive };
                cache.trailingGroup = { Rounded(ShapeTokens::CornerValueSmall, ShapeTokens::CornerValueLarge), inactive };
            }
            return cache;
        }
    }

    // --- colours -------------------------------------------------------------------------------

    MenuItemColors MenuItemColors::Copy(Color text, Color container, Color leading, Color trailing, Color disabledText, Color disabledContainer,
                                        Color disabledLeading, Color disabledTrailing) const {
        MenuItemColors r{ Or(text, textColor),
                          Or(leading, leadingIconColor),
                          Or(trailing, trailingIconColor),
                          Or(disabledText, disabledTextColor),
                          Or(disabledLeading, disabledLeadingIconColor),
                          Or(disabledTrailing, disabledTrailingIconColor) };
        r.containerColor = Or(container, containerColor);
        r.disabledContainerColor = Or(disabledContainer, disabledContainerColor);
        return r;
    }

    SelectableMenuItemColors SelectableMenuItemColors::Unspecified() {
        const Color u = Color::Unspecified();
        return { u, u, u, u, u, u, u, u, u, u, u, u };
    }
    SelectableMenuItemColors SelectableMenuItemColors::Copy(const SelectableMenuItemColors& o) const {
        return { Or(o.textColor, textColor),
                 Or(o.containerColor, containerColor),
                 Or(o.leadingIconColor, leadingIconColor),
                 Or(o.trailingContentColor, trailingContentColor),
                 Or(o.disabledTextColor, disabledTextColor),
                 Or(o.disabledContainerColor, disabledContainerColor),
                 Or(o.disabledLeadingIconColor, disabledLeadingIconColor),
                 Or(o.disabledTrailingContentColor, disabledTrailingContentColor),
                 Or(o.selectedTextColor, selectedTextColor),
                 Or(o.selectedContainerColor, selectedContainerColor),
                 Or(o.selectedLeadingIconColor, selectedLeadingIconColor),
                 Or(o.selectedTrailingContentColor, selectedTrailingContentColor) };
    }

    // --- MenuDefaults --------------------------------------------------------------------------

    namespace MenuDefaults {
        PopupProperties DefaultMenuProperties() {
            PopupProperties p;
            p.focusable = true;
            return p;
        }
        const Dp TonalElevation = ElevationTokens::Level0;
        const Dp ShadowElevation = MenuTokens::ContainerElevation;
        const Dp LeadingIconSize = SegmentedMenuTokens::ItemLeadingIconSize;
        const Dp GroupSpacing = SegmentedMenuTokens::SegmentedGap;
        Dp TrailingIconSize() { return internal::ShouldUsePrecisionPointerComponentSizing() ? Dp(24) : SegmentedMenuTokens::ItemTrailingIconSize; }
        ShapePtr shape() { return Value(MenuTokens::ContainerShape); }
        Color containerColor() { return Value(MenuTokens::ContainerColor); }
        Color groupStandardContainerColor() { return Value(StandardMenuTokens::ContainerColor); }
        Color groupVibrantContainerColor() { return Value(VibrantMenuTokens::ContainerColor); }
        ShapePtr leadingGroupShape() { return Cache().leadingGroup.shape; }
        ShapePtr middleGroupShape() { return Value(SegmentedMenuTokens::GroupShape); }
        ShapePtr trailingGroupShape() { return Cache().trailingGroup.shape; }
        ShapePtr leadingItemShape() { return Cache().leading.shape; }
        ShapePtr middleItemShape() { return Value(SegmentedMenuTokens::ItemShape); }
        ShapePtr trailingItemShape() { return Cache().trailing.shape; }
        ShapePtr standaloneItemShape() { return middleItemShape(); }
        ShapePtr selectedItemShape() { return Value(SegmentedMenuTokens::ItemSelectedShape); }
        ShapePtr standaloneGroupShape() { return Value(SegmentedMenuTokens::ContainerShape); }
        ShapePtr inactiveGroupShape() { return Value(SegmentedMenuTokens::InactiveContainerShape); }
        const PaddingValues HorizontalDividerPadding{ Dp(12), Dp(2), Dp(12), Dp(2) };
        const PaddingValues DropdownMenuGroupLabelHorizontalPadding{ Dp(12), Dp(0), Dp(4), Dp(0) };
        const PaddingValues DropdownMenuItemContentPadding{ DropdownMenuItemHorizontalPadding, Dp(0), DropdownMenuItemHorizontalPadding, Dp(0) };
        const PaddingValues DropdownMenuGroupContentPadding{ Dp(0), DropdownMenuGroupVerticalPadding, Dp(0), DropdownMenuGroupVerticalPadding };
        PaddingValues DropdownMenuSelectableItemContentPadding() {
            constexpr Dp vertical{ 12 };
            if (internal::ShouldUsePrecisionPointerComponentSizing()) return { Dp(16), vertical, Dp(10), vertical };
            return { DropdownMenuItemHorizontalPadding, vertical, DropdownMenuItemHorizontalPadding, vertical };
        }
        PaddingValues DropdownMenuItemTrailingLabelHorizontalPadding() {
            if (internal::ShouldUsePrecisionPointerComponentSizing()) return { Dp(0), Dp(0), Dp(6), Dp(0) };
            return {};
        }

        MenuGroupShapes groupShape(int index, int count) {
            const ShapeCache& s = Cache();
            if (count == 1) return s.standaloneGroup;
            if (index == 0) return s.leadingGroup;
            if (index == count - 1) return s.trailingGroup;
            return s.middleGroup;
        }
        MenuItemShapes itemShape(int index, int count) {
            const ShapeCache& s = Cache();
            if (count == 1) return s.standalone;
            if (index == 0) return s.leading;
            if (index == count - 1) return s.trailing;
            return s.middle;
        }
        MenuItemShapes itemShapes(ShapePtr shape, ShapePtr selectedShape) { return Cache().standalone.Copy(std::move(shape), std::move(selectedShape)); }
        MenuGroupShapes groupShapes(ShapePtr shape, ShapePtr inactiveShape) {
            return Cache().standaloneGroup.Copy(std::move(shape), std::move(inactiveShape));
        }

        MenuItemColors itemColors() {
            MenuItemColors c{ Value(ListTokens::ItemLabelTextColor),
                              Value(ListTokens::ItemLeadingIconColor),
                              Value(ListTokens::ItemTrailingIconColor),
                              Value(ListTokens::ItemDisabledLabelTextColor).WithAlpha(ListTokens::ItemDisabledLabelTextOpacity),
                              Value(ListTokens::ItemDisabledLeadingIconColor).WithAlpha(ListTokens::ItemDisabledLeadingIconOpacity),
                              Value(ListTokens::ItemDisabledTrailingIconColor).WithAlpha(ListTokens::ItemDisabledTrailingIconOpacity) };
            return c;
        }
        MenuItemColors itemVibrantColors() {
            MenuItemColors c{ Value(VibrantMenuTokens::ItemLabelTextColor),
                              Value(VibrantMenuTokens::ItemLeadingIconColor),
                              Value(VibrantMenuTokens::ItemTrailingIconColor),
                              Value(VibrantMenuTokens::ItemDisabledLabelTextColor).WithAlpha(VibrantMenuTokens::ItemDisabledLabelTextOpacity),
                              Value(VibrantMenuTokens::ItemDisabledLeadingIconColor).WithAlpha(VibrantMenuTokens::ItemDisabledLeadingIconOpacity),
                              Value(VibrantMenuTokens::ItemDisabledTrailingIconColor).WithAlpha(VibrantMenuTokens::ItemDisabledTrailingIconOpacity) };
            c.containerColor = Value(VibrantMenuTokens::ContainerColor);
            c.disabledContainerColor = Value(VibrantMenuTokens::ContainerColor);
            return c;
        }
        SelectableMenuItemColors selectableItemColors() {
            return { Value(StandardMenuTokens::ItemLabelTextColor),
                     Value(StandardMenuTokens::ContainerColor),
                     Value(StandardMenuTokens::ItemLeadingIconColor),
                     Value(StandardMenuTokens::ItemTrailingIconColor),
                     Value(StandardMenuTokens::ItemDisabledLabelTextColor).WithAlpha(StandardMenuTokens::ItemDisabledLabelTextOpacity),
                     Value(StandardMenuTokens::ContainerColor),
                     Value(StandardMenuTokens::ItemDisabledLeadingIconColor).WithAlpha(StandardMenuTokens::ItemDisabledLeadingIconOpacity),
                     Value(StandardMenuTokens::ItemDisabledTrailingIconColor).WithAlpha(StandardMenuTokens::ItemDisabledTrailingIconOpacity),
                     Value(StandardMenuTokens::ItemSelectedLabelTextColor),
                     Value(StandardMenuTokens::ItemSelectedContainerColor),
                     Value(StandardMenuTokens::ItemSelectedLeadingIconColor),
                     Value(StandardMenuTokens::ItemSelectedTrailingIconColor) };
        }
        SelectableMenuItemColors selectableItemVibrantColors() {
            return { Value(VibrantMenuTokens::ItemLabelTextColor),
                     Value(VibrantMenuTokens::ContainerColor),
                     Value(VibrantMenuTokens::ItemLeadingIconColor),
                     Value(VibrantMenuTokens::ItemTrailingIconColor),
                     Value(VibrantMenuTokens::ItemDisabledLabelTextColor).WithAlpha(VibrantMenuTokens::ItemDisabledLabelTextOpacity),
                     Value(VibrantMenuTokens::ContainerColor),
                     Value(VibrantMenuTokens::ItemDisabledLeadingIconColor).WithAlpha(VibrantMenuTokens::ItemDisabledLeadingIconOpacity),
                     Value(VibrantMenuTokens::ItemDisabledTrailingIconColor).WithAlpha(VibrantMenuTokens::ItemDisabledTrailingIconOpacity),
                     Value(VibrantMenuTokens::ItemSelectedLabelTextColor),
                     Value(VibrantMenuTokens::ItemSelectedContainerColor),
                     Value(VibrantMenuTokens::ItemSelectedLeadingIconColor),
                     Value(VibrantMenuTokens::ItemSelectedTrailingIconColor) };
        }

        Arrangement DropdownMenuItemHorizontalArrangement() { return ItemHorizontalArrangement(); }

        void DropdownMenuGroupLabel(const Modifier& modifier, std::function<void()> content, Alignment contentAlignment, PaddingValues padding) {
            ProvideTextStyle(MaterialTheme::typography().labelLarge, [&] {
                Box(Padding(SizeIn(modifier, DropdownMenuItemDefaultMinWidth, DropdownMenuGroupDefaultMinHeight, DropdownMenuItemDefaultMaxWidth,
                                   Dp::Unspecified()),
                            padding),
                    contentAlignment, false, content);
            });
        }
        void DropdownMenuItemTrailingLabel(const Modifier& modifier, std::function<void()> content) {
            const PaddingValues padding = DropdownMenuItemTrailingLabelHorizontalPadding();
            ProvideTextStyle(MaterialTheme::typography().labelLarge, [&] { Box(Padding(modifier, padding), Alignment::TopStart(), false, content); });
        }

        std::shared_ptr<const DropdownMenuPopupPositionProvider> rememberDropdownMenuPopupPositionProvider(const MenuAnchorPosition& anchorPosition,
                                                                                                         DpOffset offset) {
            auto& c = *rt::Composition::Current();
            const Density density = c.Read(LocalDensity);
            return c.Remember<std::shared_ptr<const DropdownMenuPopupPositionProvider>>(
                [&] { return std::make_shared<DropdownMenuPopupPositionProvider>(offset, density, anchorPosition); },
                rt::Keys(static_cast<const void*>(&anchorPosition), offset.x.value, offset.y.value, density.density, density.fontScale));
        }
    }

    // --- DropdownMenu --------------------------------------------------------------------------

    void DropdownMenu(bool expanded, std::function<void()> onDismissRequest, const Modifier& modifier, const DropdownMenuOptions& o,
                      std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        auto& expandedState = c.RememberNew<MutableTransitionState<bool>>({}, {}, false);
        expandedState.SetTargetState(expanded);
        ScrollState& scrollState = o.scrollState ? *o.scrollState : RememberScrollState();
        if (!(expandedState.CurrentState() || expandedState.TargetState())) return;
        const Density density = c.Read(LocalDensity);
        const DpOffset offset = o.offset;
        auto provider = c.Remember<std::shared_ptr<const DropdownMenuPopupPositionProvider>>(
            [&] {
                auto p = std::make_shared<DropdownMenuPopupPositionProvider>(offset, density, MenuAnchorPosition::Below());
                p->horizontalMargin = 0;
                return std::shared_ptr<const DropdownMenuPopupPositionProvider>(std::move(p));
            },
            rt::Keys(offset.x.value, offset.y.value, density.density, density.fontScale));
        const ShapePtr shape = o.shape ? o.shape : MenuDefaults::shape();
        const Color containerColor = o.containerColor ? *o.containerColor : MenuDefaults::containerColor();
        const Dp tonal = o.tonalElevation, shadow = o.shadowElevation ? *o.shadowElevation : MenuDefaults::ShadowElevation;
        const auto border = o.border;
        auto* state = &expandedState;
        auto* scroll = &scrollState;
        Popup(provider, std::move(onDismissRequest), o.properties ? *o.properties : MenuDefaults::DefaultMenuProperties(),
              [=, content = std::move(content)] {
                  DropdownMenuContent(modifier, *state, provider, *scroll, shape, containerColor, tonal, shadow, border, content);
              });
    }

    void DropdownMenuPopup(bool expanded, std::function<void()> onDismissRequest, const Modifier& modifier,
                           std::shared_ptr<const DropdownMenuPopupPositionProvider> provider, std::function<void()> content,
                           std::optional<PopupProperties> properties) {
        auto& c = *rt::Composition::Current();
        auto& expandedState = c.RememberNew<MutableTransitionState<bool>>({}, {}, false);
        expandedState.SetTargetState(expanded);
        if (!(expandedState.CurrentState() || expandedState.TargetState())) return;
        if (!provider) provider = MenuDefaults::rememberDropdownMenuPopupPositionProvider(MenuAnchorPosition::Below());
        auto* state = &expandedState;
        Popup(provider, std::move(onDismissRequest), properties ? *properties : MenuDefaults::DefaultMenuProperties(),
              [=, content = std::move(content)] {
                  const MenuAnimation a = AnimateMenu(*state);
                  Column(MenuLayer(Width(modifier, IntrinsicSize::Max), a, provider), Arrangement::Top(), AlignmentH::Start(), content);
              });
    }

    void DropdownMenuGroup(const MenuGroupShapes& shapes, const Modifier& modifier, const DropdownMenuGroupOptions& o, std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        const auto source = o.interactionSource ? o.interactionSource : RememberInteractionSource();
        const bool hovered = source->IsHovered();
        auto& hasBeenHovered = c.RememberState(false);
        if (hovered && !hasBeenHovered.Peek()) hasBeenHovered.Set(true);
        const ShapePtr groupShape = GroupShapeByInteraction(shapes, hasBeenHovered.Get(), hovered, Value(MotionSchemeKeyTokens::FastSpatial));
        SurfaceOptions surface;
        surface.shape = groupShape;
        surface.color = o.containerColor ? *o.containerColor : MenuDefaults::groupStandardContainerColor();
        surface.tonalElevation = o.tonalElevation;
        surface.shadowElevation = o.shadowElevation ? *o.shadowElevation : MenuDefaults::ShadowElevation;
        surface.border = o.border;
        const Modifier column = Padding(modifier, o.contentPadding);
        Surface(Hoverable({}, source), surface, [column, content = std::move(content)] { Column(column, Arrangement::Top(), AlignmentH::Start(), content); });
    }

    // --- items ---------------------------------------------------------------------------------

    void DropdownMenuItem(std::function<void()> text, std::function<void()> onClick, const Modifier& modifier, const DropdownMenuItemOptions& o) {
        const MenuItemColors colors = o.colors ? *o.colors : MenuDefaults::itemColors();
        const bool enabled = o.enabled;
        ClickableOptions click;
        click.enabled = enabled;
        click.interactionSource = o.interactionSource;
        click.indication = Ripple();
        click.useLocalIndication = false;
        // The preferred minimum and maximum widths apply to intrinsic measurement.
        const Modifier row = Padding(SizeIn(FillMax(Clickable(modifier, std::move(onClick), click), Axis::Horizontal), DropdownMenuItemDefaultMinWidth,
                                            MenuListItemContainerHeight, DropdownMenuItemDefaultMaxWidth, Dp::Unspecified()),
                                     o.contentPadding);
        auto leading = o.leadingIcon, trailing = o.trailingIcon;
        Row(row, Arrangement::Start(), AlignmentV::CenterVertically(), [=] {
            ProvideTextStyle(MaterialTheme::typography().labelLarge, [&] {
                if (leading) {
                    ProvideContentColor(colors.LeadingIconColor(enabled), [&] {
                        Box(DefaultMinSize({}, ListTokens::ItemLeadingIconSize, Dp::Unspecified()), Alignment::TopStart(), false, leading);
                    });
                }
                ProvideContentColor(colors.TextColor(enabled), [&] {
                    const PaddingValues padding{ leading ? DropdownMenuItemHorizontalPadding : Dp(0), Dp(0),
                                                 trailing ? DropdownMenuItemHorizontalPadding : Dp(0), Dp(0) };
                    Box(Padding(Weight({}, 1), padding), Alignment::TopStart(), false, text);
                });
                if (trailing) {
                    ProvideContentColor(colors.TrailingIconColor(enabled), [&] {
                        Box(DefaultMinSize({}, ListTokens::ItemTrailingIconSize, Dp::Unspecified()), Alignment::TopStart(), false, trailing);
                    });
                }
            });
        });
    }

    void DropdownMenuItem(std::function<void()> onClick, std::function<void()> text, ShapePtr shape, const Modifier& modifier, const MenuItemOptions& o,
                          std::optional<MenuItemColors> colors) {
        const MenuItemColors c = colors ? *colors : MenuDefaults::itemColors();
        MenuItemOptions options = o;
        options.selectedLeadingIcon = nullptr;
        ExpressiveItemContent(false, std::move(onClick), std::move(text), WithRole(modifier, Role::Button), options,
                              { c.ContainerColor(o.enabled), c.LeadingIconColor(o.enabled), c.TextColor(o.enabled), c.TrailingIconColor(o.enabled) },
                              MenuDefaults::itemShapes(std::move(shape)));
    }

    void CheckableDropdownMenuItem(bool checked, std::function<void(bool)> onCheckedChange, std::function<void()> text, const MenuItemShapes& shapes,
                                   const Modifier& modifier, const MenuItemOptions& o, std::optional<SelectableMenuItemColors> colors) {
        const SelectableMenuItemColors c = colors ? *colors : MenuDefaults::selectableItemColors();
        ExpressiveItemContent(checked, [checked, onCheckedChange = std::move(onCheckedChange)] { onCheckedChange(!checked); }, std::move(text),
                              WithRole(modifier, Role::Checkbox), o,
                              { c.ContainerColor(o.enabled, checked), c.LeadingIconColor(o.enabled, checked), c.TextColor(o.enabled, checked),
                                c.TrailingContentColor(o.enabled, checked) },
                              shapes);
    }

    void SelectableDropdownMenuItem(bool selected, std::function<void()> onClick, std::function<void()> text, const MenuItemShapes& shapes,
                                    const Modifier& modifier, const MenuItemOptions& o, std::optional<SelectableMenuItemColors> colors) {
        const SelectableMenuItemColors c = colors ? *colors : MenuDefaults::selectableItemColors();
        ExpressiveItemContent(selected, std::move(onClick), std::move(text), WithRole(modifier, Role::RadioButton), o,
                              { c.ContainerColor(o.enabled, selected), c.LeadingIconColor(o.enabled, selected), c.TextColor(o.enabled, selected),
                                c.TrailingContentColor(o.enabled, selected) },
                              shapes);
    }

}
