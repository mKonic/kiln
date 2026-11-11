#include "kiln/material3/adaptive/NavigationSuiteScaffold.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/Badge.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/ShortNavigationBar.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/runtime/Saveable.h"

namespace kiln::material3::adaptive {

    using namespace foundation;
    using ui::Color;
    using ui::Dp;

    namespace {
        using Value = NavigationSuiteScaffoldValue;

        // spring(SpringDefaultSpatialDamping, SpringDefaultSpatialStiffness)
        anim::SpecPtr AnimationSpec() { return anim::Spring(0.9f, 700.0f); }

        class StateImpl final : public NavigationSuiteScaffoldState {
        public:
            explicit StateImpl(Value initial)
                : m_Anim(initial == Value::Visible ? 1.0f : 0.0f),
                  m_Current([this] { return m_Anim.Float() == 1.0f ? Value::Visible : Value::Hidden; }) {}
            bool IsAnimating() const override { return m_Anim.IsRunning(); }
            Value TargetValue() const override { return m_Anim.Target()[0] == 1.0f ? Value::Visible : Value::Hidden; }
            Value CurrentValue() const override { return m_Current.Get(); }
            rt::Task<> Hide() override { co_await m_Anim.AnimateTo(0.0f, AnimationSpec()); }
            rt::Task<> Show() override { co_await m_Anim.AnimateTo(1.0f, AnimationSpec()); }
            rt::Task<> Toggle() override { co_await m_Anim.AnimateTo(TargetValue() == Value::Visible ? 0.0f : 1.0f, AnimationSpec()); }
            rt::Task<> SnapTo(Value target) override {
                m_Anim.SnapTo(target == Value::Visible ? 1.0f : 0.0f);
                co_return;
            }

        private:
            anim::Animatable m_Anim;
            mutable rt::Derived<Value> m_Current;
        };

        bool IsNavigationBar(NavigationSuiteType t) {
            return t == NavigationSuiteType::ShortNavigationBarCompact || t == NavigationSuiteType::ShortNavigationBarMedium ||
                   t == NavigationSuiteType::NavigationBar;
        }

        // navigationSuiteScaffoldConsumeWindowInsets
        WindowInsets ConsumedInsets(NavigationSuiteType type, const NavigationSuiteScaffoldState& state) {
            if (state.CurrentValue() == Value::Hidden && !state.IsAnimating()) return WindowInsets();
            switch (type) {
                case NavigationSuiteType::ShortNavigationBarCompact:
                case NavigationSuiteType::ShortNavigationBarMedium:
                    // The bar's height is taken from the layout's height in NavigationSuiteScaffoldLayout.
                    return ShortNavigationBarDefaults::windowInsets()
                        .Only(WindowInsetsSides::Bottom())
                        .Add(WindowInsets::Fixed(Dp(0), Dp(0), Dp(0), internal::ShortNavigationBarHeight));
                case NavigationSuiteType::WideNavigationRailCollapsed:
                case NavigationSuiteType::WideNavigationRailExpanded:
                    return WideNavigationRailDefaults::windowInsets().Only(WindowInsetsSides::Start());
                case NavigationSuiteType::NavigationBar:
                    return NavigationBarDefaults::windowInsets()
                        .Only(WindowInsetsSides::Bottom())
                        .Add(WindowInsets::Fixed(Dp(0), Dp(0), Dp(0), internal::TallNavigationBarHeight));
                case NavigationSuiteType::NavigationRail: return NavigationRailDefaults::windowInsets().Only(WindowInsetsSides::Start());
                case NavigationSuiteType::NavigationDrawer: return DrawerDefaults::windowInsets().Only(WindowInsetsSides::Start());
                case NavigationSuiteType::None: break;
            }
            return WindowInsets();
        }

        void NavigationItemIcon(std::function<void()> icon, std::function<void()> badge) {
            if (badge) BadgedBox(std::move(badge), {}, std::move(icon));
            else icon();
        }

        // The arrangement of the older vertical components: weighted spacers around the items.
        void Arranged(const Arrangement& arrangement, const std::function<void()>& content) {
            if (arrangement == Arrangement::Center() || arrangement == Arrangement::Bottom()) Spacer(Weight({}, 1));
            content();
            if (arrangement == Arrangement::Center()) Spacer(Weight({}, 1));
        }

        void SuiteItem(bool isNavigationSuite, NavigationSuiteType type, bool selected, std::function<void()> onClick, std::function<void()> icon,
                       std::function<void()> label, const ui::Modifier& modifier, bool enabled, std::function<void()> badge,
                       const std::optional<NavigationItemColors>& navigationItemColors,
                       const std::optional<NavigationSuiteItemColors>& navigationSuiteItemColors,
                       std::shared_ptr<MutableInteractionSource> interactionSource) {
            const auto badgedIcon = [icon, badge] { NavigationItemIcon(icon, badge); };
            switch (type) {
                case NavigationSuiteType::ShortNavigationBarCompact:
                case NavigationSuiteType::ShortNavigationBarMedium: {
                    ShortNavigationBarItemOptions o;
                    o.enabled = enabled;
                    o.iconPosition = type == NavigationSuiteType::ShortNavigationBarCompact ? NavigationItemIconPosition::Top
                                                                                            : NavigationItemIconPosition::Start;
                    o.colors = navigationItemColors ? *navigationItemColors : ShortNavigationBarItemDefaults::colors();
                    o.interactionSource = std::move(interactionSource);
                    ShortNavigationBarItem(selected, std::move(onClick), badgedIcon, std::move(label), modifier, o);
                    break;
                }
                case NavigationSuiteType::WideNavigationRailCollapsed:
                case NavigationSuiteType::WideNavigationRailExpanded: {
                    WideNavigationRailItemOptions o;
                    o.enabled = enabled;
                    o.colors = navigationItemColors ? *navigationItemColors : WideNavigationRailItemDefaults::colors();
                    o.interactionSource = std::move(interactionSource);
                    WideNavigationRailItem(selected, std::move(onClick), badgedIcon, std::move(label),
                                           type == NavigationSuiteType::WideNavigationRailExpanded, modifier, o);
                    break;
                }
                // A NavigationBarItem needs a RowScope: a short bar item padded to look the same.
                case NavigationSuiteType::NavigationBar: {
                    const NavigationBarItemColors defaults =
                        navigationSuiteItemColors ? navigationSuiteItemColors->navigationBarItemColors : NavigationBarItemDefaults::colors();
                    ShortNavigationBarItemOptions o;
                    o.enabled = enabled;
                    o.colors = (!isNavigationSuite && !navigationItemColors) || isNavigationSuite
                                   ? ShortNavigationBarItemDefaults::colors(defaults.selectedIconColor, defaults.selectedTextColor, defaults.selectedTextColor,
                                                                            defaults.selectedIndicatorColor, defaults.unselectedIconColor,
                                                                            defaults.unselectedTextColor, defaults.disabledIconColor, defaults.disabledTextColor)
                                   : *navigationItemColors;
                    o.interactionSource = std::move(interactionSource);
                    ShortNavigationBarItem(selected, std::move(onClick), badgedIcon, std::move(label),
                                           Padding(modifier, PaddingValues{ Dp(0), Dp(8), Dp(0), Dp(0) }), o);
                    break;
                }
                case NavigationSuiteType::NavigationRail: {
                    NavigationRailItemOptions o;
                    o.enabled = enabled;
                    o.label = std::move(label);
                    if (isNavigationSuite) {
                        o.colors = navigationSuiteItemColors ? navigationSuiteItemColors->navigationRailItemColors : NavigationRailItemDefaults::colors();
                    } else if (navigationItemColors) {
                        const NavigationItemColors& c = *navigationItemColors;
                        o.colors = NavigationRailItemDefaults::colors(c.selectedIconColor, c.selectedTextColorTopIconPosition, c.selectedIndicatorColor,
                                                                      c.unselectedIconColor, c.unselectedTextColor, c.disabledIconColor, c.disabledTextColor);
                    } else {
                        o.colors = NavigationSuiteDefaults::itemColors().navigationRailItemColors;
                    }
                    o.interactionSource = std::move(interactionSource);
                    NavigationRailItem(selected, std::move(onClick), badgedIcon, modifier, o);
                    break;
                }
                case NavigationSuiteType::NavigationDrawer: {
                    NavigationDrawerItemOptions o;
                    o.icon = std::move(icon);
                    o.badge = std::move(badge);
                    if (isNavigationSuite) {
                        o.colors = navigationSuiteItemColors ? navigationSuiteItemColors->navigationDrawerItemColors
                                                             : NavigationDrawerItemDefaults::colors();
                    } else if (navigationItemColors) {
                        const NavigationItemColors& c = *navigationItemColors;
                        o.colors = NavigationDrawerItemDefaults::colors(c.selectedIndicatorColor, Color::Unspecified(), c.selectedIconColor,
                                                                        c.unselectedIconColor, c.selectedTextColorStartIconPosition, c.unselectedTextColor);
                    } else {
                        o.colors = NavigationSuiteDefaults::itemColors().navigationDrawerItemColors;
                    }
                    o.interactionSource = std::move(interactionSource);
                    NavigationDrawerItem([label] { if (label) label(); else Text(""); }, selected, std::move(onClick), modifier, o);
                    break;
                }
                case NavigationSuiteType::None: break;
            }
        }

        NavigationSuiteType DefaultType() { return NavigationSuiteScaffoldDefaults::navigationSuiteType(CurrentWindowAdaptiveInfo()); }
    }

    std::shared_ptr<NavigationSuiteScaffoldState> RememberNavigationSuiteScaffoldState(Value initialValue, rt::Key key) {
        rt::Saver<std::shared_ptr<NavigationSuiteScaffoldState>> saver{
            [](const std::shared_ptr<NavigationSuiteScaffoldState>& s) { return rt::SavedValue(double(s->TargetValue() == Value::Visible)); },
            [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<NavigationSuiteScaffoldState>> {
                if (auto n = rt::SavedNumber(saved)) return std::make_shared<StateImpl>(*n != 0 ? Value::Visible : Value::Hidden);
                return std::nullopt;
            }
        };
        return rt::RememberSaveable<std::shared_ptr<NavigationSuiteScaffoldState>>([initialValue] { return std::make_shared<StateImpl>(initialValue); },
                                                                                   saver, {}, key);
    }

    const char* ToString(NavigationSuiteType type) {
        switch (type) {
            case NavigationSuiteType::ShortNavigationBarCompact: return "ShortNavigationBarCompact";
            case NavigationSuiteType::ShortNavigationBarMedium: return "ShortNavigationBarMedium";
            case NavigationSuiteType::WideNavigationRailCollapsed: return "WideNavigationRailCollapsed";
            case NavigationSuiteType::WideNavigationRailExpanded: return "WideNavigationRailExpanded";
            case NavigationSuiteType::NavigationBar: return "NavigationBar";
            case NavigationSuiteType::NavigationRail: return "NavigationRail";
            case NavigationSuiteType::NavigationDrawer: return "NavigationDrawer";
            case NavigationSuiteType::None: return "None";
        }
        return "";
    }

    namespace NavigationSuiteScaffoldDefaults {
        NavigationSuiteType navigationSuiteType(const WindowAdaptiveInfo& info) {
            if (info.windowSizeClass.minWidthDp == 0) return NavigationSuiteType::ShortNavigationBarCompact;   // WidthSizeClasses.Compact
            if (info.windowPosture.isTabletop || info.windowSizeClass.minHeightDp == 0) return NavigationSuiteType::ShortNavigationBarMedium;
            return NavigationSuiteType::WideNavigationRailCollapsed;
        }
        NavigationSuiteType calculateFromAdaptiveInfo(const WindowAdaptiveInfo& info) {
            if (info.windowPosture.isTabletop || info.windowSizeClass.minHeightDp == 0 || info.windowSizeClass.minWidthDp == 0)
                return NavigationSuiteType::NavigationBar;
            return NavigationSuiteType::NavigationRail;
        }
        Color containerColor() { return MaterialTheme::colorScheme().background; }
        Color contentColor() { return MaterialTheme::colorScheme().onBackground; }
    }

    namespace NavigationSuiteDefaults {
        NavigationSuiteColors colors() {
            NavigationSuiteColors c;
            c.shortNavigationBarContentColor = ShortNavigationBarDefaults::contentColor();
            c.shortNavigationBarContainerColor = ShortNavigationBarDefaults::containerColor();
            c.wideNavigationRailColors = WideNavigationRailDefaults::colors();
            c.navigationBarContainerColor = NavigationBarDefaults::containerColor();
            c.navigationBarContentColor = ContentColorFor(c.navigationBarContainerColor);
            c.navigationRailContainerColor = NavigationRailDefaults::ContainerColor();
            c.navigationRailContentColor = ContentColorFor(c.navigationRailContainerColor);
            c.navigationDrawerContainerColor = DrawerDefaults::standardContainerColor();   // the deprecated DrawerDefaults.containerColor
            c.navigationDrawerContentColor = ContentColorFor(c.navigationDrawerContainerColor);
            return c;
        }
        NavigationSuiteItemColors itemColors() {
            return { NavigationBarItemDefaults::colors(), NavigationRailItemDefaults::colors(), NavigationDrawerItemDefaults::colors() };
        }
    }

    void NavigationSuite(NavigationSuiteType type, const ui::Modifier& modifier, const NavigationSuiteOptions& options, std::function<void()> content) {
        const NavigationSuiteColors colors = options.colors.value_or(NavigationSuiteDefaults::colors());
        const auto primaryAction = options.primaryActionContent ? options.primaryActionContent : std::function<void()>([] {});
        switch (type) {
            case NavigationSuiteType::ShortNavigationBarCompact:
            case NavigationSuiteType::ShortNavigationBarMedium: {
                ShortNavigationBarOptions o;
                o.containerColor = colors.shortNavigationBarContainerColor;
                o.contentColor = colors.shortNavigationBarContentColor;
                ShortNavigationBar(modifier, o, std::move(content));
                break;
            }
            case NavigationSuiteType::WideNavigationRailCollapsed:
            case NavigationSuiteType::WideNavigationRailExpanded: {
                WideNavigationRailOptions o;
                o.header = primaryAction;
                o.arrangement = options.verticalArrangement;
                o.colors = colors.wideNavigationRailColors;
                if (type == NavigationSuiteType::WideNavigationRailExpanded) o.state = RememberWideNavigationRailState(WideNavigationRailValue::Expanded);
                WideNavigationRail(modifier, o, std::move(content));
                break;
            }
            // A ShortNavigationBar made as tall as a NavigationBar, so it looks the same.
            case NavigationSuiteType::NavigationBar: {
                ShortNavigationBarOptions o;
                o.containerColor = colors.navigationBarContainerColor;
                o.contentColor = colors.navigationBarContentColor;
                ShortNavigationBar(SizeIn(modifier, Dp::Unspecified(), internal::TallNavigationBarHeight, Dp::Unspecified(), Dp::Unspecified()), o,
                                   std::move(content));
                break;
            }
            case NavigationSuiteType::NavigationRail: {
                NavigationRailOptions o;
                o.header = primaryAction;
                o.containerColor = colors.navigationRailContainerColor;
                o.contentColor = colors.navigationRailContentColor;
                NavigationRail(modifier, o, [arrangement = options.verticalArrangement, content] { Arranged(arrangement, content); });
                break;
            }
            case NavigationSuiteType::NavigationDrawer: {
                DrawerSheetOptions o;
                o.drawerContainerColor = colors.navigationDrawerContainerColor;
                o.drawerContentColor = colors.navigationDrawerContentColor;
                PermanentDrawerSheet(modifier, o, [arrangement = options.verticalArrangement, content, primaryAction] {
                    primaryAction();
                    Arranged(arrangement, content);
                });
                break;
            }
            case NavigationSuiteType::None: break;
        }
    }

    void NavigationSuiteItem(bool selected, std::function<void()> onClick, std::function<void()> icon, std::function<void()> label,
                             const ui::Modifier& modifier, const NavigationSuiteItemOptions& o) {
        SuiteItem(false, o.navigationSuiteType.value_or(DefaultType()), selected, std::move(onClick), std::move(icon), std::move(label), modifier, o.enabled,
                  o.badge, o.colors, std::nullopt, o.interactionSource);
    }

    void NavigationSuiteScaffoldLayout(std::function<void()> navigationSuite, NavigationSuiteType type,
                                       std::shared_ptr<NavigationSuiteScaffoldState> state, std::function<void()> primaryActionContent,
                                       ui::AlignmentH alignment, std::function<void()> content) {
        if (!state) state = RememberNavigationSuiteScaffoldState();
        const float progress = AnimateFloatAsState(state->CurrentValue() == Value::Hidden ? 0.0f : 1.0f, AnimationSpec());
        const bool isNavigationBar = IsNavigationBar(type);
        auto policy = std::make_shared<ui::LambdaMeasurePolicy>(
            [progress, isNavigationBar, alignment](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                auto find = [&](const char* id) -> ui::Measurable& {
                    for (auto* m : children)
                        if (const std::string* tag = LayoutIdOf(*m); tag && *tag == id) return *m;
                    return *children[0];
                };
                const ui::Constraints loose{ 0, c.maxWidth, 0, c.maxHeight };
                ui::Placeable& navigation = find("navigationSuite").Measure(loose);
                ui::Placeable& primaryAction = find("primaryActionContent").Measure(loose);
                const int layoutWidth = c.maxWidth, layoutHeight = c.maxHeight;
                ui::Constraints forContent = c;
                if (isNavigationBar) {
                    forContent.minHeight = forContent.maxHeight = layoutHeight - int(float(navigation.Height()) * progress);
                } else {
                    forContent.minWidth = forContent.maxWidth = layoutWidth - int(float(navigation.Width()) * progress);
                }
                ui::Placeable& contentPlaceable = find("content").Measure(forContent);
                const int actionPadding = s.RoundToPx(internal::PrimaryActionContentPadding);
                return s.Layout(layoutWidth, layoutHeight, [&, progress, isNavigationBar, alignment, layoutWidth, layoutHeight, actionPadding](ui::PlacementScope& p) {
                    if (isNavigationBar) {
                        // The content above the bar, the bar at the bottom, the primary action above the bar.
                        p.PlaceRelative(contentPlaceable, 0, 0);
                        const int barShown = int(float(navigation.Height()) * progress);
                        p.PlaceRelative(navigation, 0, layoutHeight - barShown);
                        const int x = alignment == ui::AlignmentH::Start()                ? actionPadding
                                      : alignment == ui::AlignmentH::CenterHorizontally() ? (layoutWidth - primaryAction.Width()) / 2
                                                                                          : layoutWidth - primaryAction.Width() - actionPadding;
                        p.PlaceRelative(primaryAction, x, layoutHeight - primaryAction.Height() - actionPadding - barShown);
                    } else {
                        // The suite at the start, the content beside it.
                        p.PlaceRelative(navigation, int(0 - float(navigation.Width()) * (1.0f - progress)), 0);
                        p.PlaceRelative(contentPlaceable, int(float(navigation.Width()) * progress), 0);
                    }
                });
            });
        auto primary = primaryActionContent ? primaryActionContent : std::function<void()>([] {});
        // Each in a Box so the parent's (Surface's) minimum constraints do not reach it.
        Layout({}, policy, [navigationSuite, primary, content] {
            Box(LayoutId({}, "navigationSuite"), ui::Alignment::TopStart(), false, navigationSuite);
            Box(LayoutId({}, "primaryActionContent"), ui::Alignment::TopStart(), false, primary);
            Box(LayoutId({}, "content"), ui::Alignment::TopStart(), false, content);
        });
    }

    void NavigationSuiteScaffold(std::function<void()> navigationItems, const ui::Modifier& modifier, const NavigationSuiteScaffoldOptions& o,
                                 std::function<void()> content) {
        const NavigationSuiteType type = o.navigationSuiteType.value_or(DefaultType());
        const auto state = o.state ? o.state : RememberNavigationSuiteScaffoldState();
        const NavigationSuiteColors colors = o.navigationSuiteColors.value_or(NavigationSuiteDefaults::colors());
        SurfaceOptions surface;
        surface.color = o.containerColor.value_or(NavigationSuiteScaffoldDefaults::containerColor());
        surface.contentColor = o.contentColor.value_or(NavigationSuiteScaffoldDefaults::contentColor());
        Surface(modifier, surface, [=] {
            NavigationSuiteScaffoldLayout(
                [=] {
                    NavigationSuiteOptions suite;
                    suite.colors = colors;
                    suite.primaryActionContent = o.primaryActionContent;
                    suite.verticalArrangement = o.navigationItemVerticalArrangement;
                    NavigationSuite(type, {}, suite, navigationItems);
                },
                type, state, o.primaryActionContent, o.primaryActionContentHorizontalAlignment,
                [=] { Box(ConsumeWindowInsets({}, ConsumedInsets(type, *state)), ui::Alignment::TopStart(), false, content); });
        });
    }

    void NavigationSuiteScope::Item(bool selected, std::function<void()> onClick, std::function<void()> icon, const NavigationSuiteScopeItemOptions& options) {
        items.push_back({ selected, std::move(onClick), std::move(icon), options });
    }

    void NavigationSuite(const ui::Modifier& modifier, NavigationSuiteType layoutType, const std::optional<NavigationSuiteColors>& colorsArg,
                         std::function<void(NavigationSuiteScope&)> content) {
        NavigationSuiteScope scope;
        if (content) content(scope);
        const NavigationSuiteColors colors = colorsArg.value_or(NavigationSuiteDefaults::colors());
        const NavigationSuiteItemColors defaultItemColors = NavigationSuiteDefaults::itemColors();
        const auto items = std::make_shared<std::vector<NavigationSuiteScope::ItemData>>(std::move(scope.items));
        switch (layoutType) {
            case NavigationSuiteType::NavigationBar: {
                NavigationBarOptions o;
                o.containerColor = colors.navigationBarContainerColor;
                o.contentColor = colors.navigationBarContentColor;
                NavigationBar(modifier, o, [items, defaultItemColors] {
                    for (const auto& it : *items) {
                        NavigationBarItemOptions io;
                        io.enabled = it.options.enabled;
                        io.label = it.options.label;
                        io.alwaysShowLabel = it.options.alwaysShowLabel;
                        io.colors = it.options.colors ? it.options.colors->navigationBarItemColors : defaultItemColors.navigationBarItemColors;
                        io.interactionSource = it.options.interactionSource;
                        NavigationBarItem(it.selected, it.onClick, [icon = it.icon, badge = it.options.badge] { NavigationItemIcon(icon, badge); },
                                          it.options.modifier, io);
                    }
                });
                break;
            }
            case NavigationSuiteType::NavigationRail: {
                NavigationRailOptions o;
                o.containerColor = colors.navigationRailContainerColor;
                o.contentColor = colors.navigationRailContentColor;
                NavigationRail(modifier, o, [items, defaultItemColors] {
                    for (const auto& it : *items) {
                        NavigationRailItemOptions io;
                        io.enabled = it.options.enabled;
                        io.label = it.options.label;
                        io.alwaysShowLabel = it.options.alwaysShowLabel;
                        io.colors = it.options.colors ? it.options.colors->navigationRailItemColors : defaultItemColors.navigationRailItemColors;
                        io.interactionSource = it.options.interactionSource;
                        NavigationRailItem(it.selected, it.onClick, [icon = it.icon, badge = it.options.badge] { NavigationItemIcon(icon, badge); },
                                           it.options.modifier, io);
                    }
                });
                break;
            }
            case NavigationSuiteType::NavigationDrawer: {
                DrawerSheetOptions o;
                o.drawerContainerColor = colors.navigationDrawerContainerColor;
                o.drawerContentColor = colors.navigationDrawerContentColor;
                PermanentDrawerSheet(modifier, o, [items, defaultItemColors] {
                    for (const auto& it : *items) {
                        NavigationDrawerItemOptions io;
                        io.icon = it.icon;
                        io.badge = it.options.badge;
                        io.colors = it.options.colors ? it.options.colors->navigationDrawerItemColors : defaultItemColors.navigationDrawerItemColors;
                        io.interactionSource = it.options.interactionSource;
                        NavigationDrawerItem([label = it.options.label] { if (label) label(); else Text(""); }, it.selected, it.onClick,
                                             it.options.modifier, io);
                    }
                });
                break;
            }
            case NavigationSuiteType::None: break;
            default: {
                NavigationSuiteOptions suite;
                suite.colors = colors;
                NavigationSuite(layoutType, modifier, suite, [items, layoutType] {
                    for (const auto& it : *items) {
                        SuiteItem(true, layoutType, it.selected, it.onClick, it.icon, it.options.label, it.options.modifier, it.options.enabled,
                                  it.options.badge, std::nullopt, it.options.colors.value_or(NavigationSuiteDefaults::itemColors()),
                                  it.options.interactionSource);
                    }
                });
                break;
            }
        }
    }

    void NavigationSuiteScaffold(std::function<void(NavigationSuiteScope&)> navigationSuiteItems, const ui::Modifier& modifier,
                                 const NavigationSuiteScaffoldScopeOptions& o, std::function<void()> content) {
        const NavigationSuiteType layoutType = o.layoutType.value_or(NavigationSuiteScaffoldDefaults::calculateFromAdaptiveInfo(CurrentWindowAdaptiveInfo()));
        const auto state = o.state ? o.state : RememberNavigationSuiteScaffoldState();
        SurfaceOptions surface;
        surface.color = o.containerColor.value_or(NavigationSuiteScaffoldDefaults::containerColor());
        surface.contentColor = o.contentColor.value_or(NavigationSuiteScaffoldDefaults::contentColor());
        Surface(modifier, surface, [=] {
            NavigationSuiteScaffoldLayout([=] { NavigationSuite({}, layoutType, o.navigationSuiteColors, navigationSuiteItems); }, layoutType, state, {},
                                          NavigationSuiteScaffoldDefaults::primaryActionContentAlignment, [=] {
                                              WindowInsets insets;
                                              if (!(state->CurrentValue() == Value::Hidden && !state->IsAnimating())) {
                                                  switch (layoutType) {
                                                      case NavigationSuiteType::NavigationBar:
                                                          insets = NavigationBarDefaults::windowInsets().Only(WindowInsetsSides::Bottom());
                                                          break;
                                                      case NavigationSuiteType::NavigationRail:
                                                          insets = NavigationRailDefaults::windowInsets().Only(WindowInsetsSides::Start());
                                                          break;
                                                      case NavigationSuiteType::NavigationDrawer:
                                                          insets = DrawerDefaults::windowInsets().Only(WindowInsetsSides::Start());
                                                          break;
                                                      default: break;
                                                  }
                                              }
                                              Box(ConsumeWindowInsets({}, insets), ui::Alignment::TopStart(), false, content ? content : [] {});
                                          });
        });
    }

}
