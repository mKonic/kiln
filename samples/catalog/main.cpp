// The androidx material3 catalog on kiln (Material3CatalogApp, NavGraph and the ui/ screens).
// Start on a screen with the route the Compose catalog takes as its favourite: "home",
// "component/<id>" or "example/<id>/<index>" (debug.kiln.catalog.route on Android,
// KILN_CATALOG_ROUTE on the host).
#include <kiln/kiln.hpp>
#include <kiln/Function.h>

#include "Catalog.h"
#include "ComponentPreviews.h"
#include "gen/Icons.h"

#include <kiln/foundation/Back.h>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Draw.h>
#include <kiln/foundation/Gestures.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/foundation/Image.h>
#include <kiln/foundation/Insets.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/foundation/VectorPainter.h>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/text/TextFieldState.h>
#include <kiln/runtime/Saveable.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/Card.h>
#include <kiln/material3/Divider.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Menu.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Text.h>
#include <kiln/material3/TextField.h>
#include <kiln/material3/TextFieldDefaults.h>
#include <kiln/runtime/Composition.h>

#include <algorithm>
#include <map>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

using namespace kiln::foundation;
using namespace kiln::ui;
namespace m3 = kiln::material3;
namespace gv = kiln::gfx::vector;

namespace catalog {
    namespace {

        std::string InitialRoute() {
#if defined(__ANDROID__)
            char value[PROP_VALUE_MAX] = {};
            __system_property_get("debug.kiln.catalog.route", value);
            return value;
#else
            const char* value = std::getenv("KILN_CATALOG_ROUTE");
            return value ? value : "";
#endif
        }

        // The catalog's vector drawables (res/drawable).
        const gv::ImageVectorPtr& ComponentIcon() {
            static const gv::ImageVectorPtr icon = [] {
                gv::ImageVector::Builder b("ic_component", Dp(52), Dp(53), 52, 53);
                b.Path(gv::ParsePathString(
                           "M32,0L32.0002,16.0599C32.4949,16.0202 32.9951,16 33.5,16C43.7173,16 52,24.2827 52,34.5C52,44.7173 "
                           "43.7173,53 33.5,53C23.2827,53 15,44.7173 15,34.5C15,33.6522 15.057,32.8178 15.1675,32.0002L0,32L0,0L32,0Z"
                           "M33.5,18.5C32.9944,18.5 32.4944,18.5234 32.0008,18.5693L32,32L17.6941,32.0004C17.5663,32.8149 17.5,33.6497 "
                           "17.5,34.5C17.5,43.3366 24.6634,50.5 33.5,50.5C42.3366,50.5 49.5,43.3366 49.5,34.5C49.5,25.6634 42.3366,18.5 "
                           "33.5,18.5ZM29.5002,2.5L2.5,2.5L2.5,29.5L15.6835,29.5003C17.5115,22.9726 22.8306,17.9035 29.5002,16.4335L29.5002,2.5Z"
                           "M18.2968,29.5L29.5,29.5L29.5007,19.0039C24.2157,20.3639 19.9874,24.3562 18.2968,29.5Z"),
                       kiln::gfx::Brush::SolidColor(Color(0xFF000000u)));
                return b.Build();
            }();
            return icon;
        }
        const gv::ImageVectorPtr& PaletteIcon() {
            static const gv::ImageVectorPtr icon = [] {
                gv::ImageVector::Builder b("ic_palette_24dp", Dp(24), Dp(24), 24, 24);
                const auto black = kiln::gfx::Brush::SolidColor(Color(0xFF000000u));
                b.Path(gv::ParsePathString(
                           "M12,22C6.49,22 2,17.51 2,12S6.49,2 12,2s10,4.04 10,9c0,3.31 -2.69,6 -6,6h-1.77c-0.28,0 -0.5,0.22 -0.5,0.5 "
                           "0,0.12 0.05,0.23 0.13,0.33 0.41,0.47 0.64,1.06 0.64,1.67 0,1.38 -1.12,2.5 -2.5,2.5zM12,4c-4.41,0 -8,3.59 -8,8s3.59,8 "
                           "8,8c0.28,0 0.5,-0.22 0.5,-0.5 0,-0.16 -0.08,-0.28 -0.14,-0.35 -0.41,-0.46 -0.63,-1.05 -0.63,-1.65 0,-1.38 1.12,-2.5 "
                           "2.5,-2.5L16,15c2.21,0 4,-1.79 4,-4 0,-3.86 -3.59,-7 -8,-7z"),
                       black);
                for (const char* dot : { "M6.5,11.5m-1.5,0a1.5,1.5 0,1 1,3 0a1.5,1.5 0,1 1,-3 0", "M9.5,7.5m-1.5,0a1.5,1.5 0,1 1,3 0a1.5,1.5 0,1 1,-3 0",
                                         "M14.5,7.5m-1.5,0a1.5,1.5 0,1 1,3 0a1.5,1.5 0,1 1,-3 0", "M17.5,11.5m-1.5,0a1.5,1.5 0,1 1,3 0a1.5,1.5 0,1 1,-3 0" })
                    b.Path(gv::ParsePathString(dot), black);
                return b.Build();
            }();
            return icon;
        }

        // ItemBanner: a ribbon across the top end corner.
        void ItemBanner(const std::string& text, Dp bannerSize) {
            Box(FillMax({}, Axis::Both), Alignment::TopEnd(), false, [=] {
                const bool isRtl = kiln::rt::Composition::Current()->Read(LocalLayoutDirection) == LayoutDirection::Rtl;
                const Color badgeColor = m3::MaterialTheme::colorScheme().secondaryContainer;
                Box(Background(SizeMod(GraphicsLayer({}, [isRtl](GraphicsLayerParams& l) {
                                                         l.rotationZ = isRtl ? -45.0f : 45.0f;
                                                         l.translationX = isRtl ? -l.size.width / 2 : l.size.width / 2;
                                                         l.translationY = -l.size.height / 2;
                                                     }),
                                       bannerSize, bannerSize),
                               badgeColor),
                    Alignment::BottomCenter(), false, [=] {
                        m3::ProvideTextStyle(m3::MaterialTheme::typography().labelMedium, [=] {
                            m3::TextOptions o;
                            o.color = m3::ContentColorFor(badgeColor);
                            m3::Text(text, {}, o);
                        });
                    });
            });
        }

        // CatalogTopAppBar's external links menu. The links open nothing here: kiln has no browser intent yet.
        void MoreMenu(bool expanded, kiln::Function<void()> onDismissRequest) {
            m3::DropdownMenu(expanded, onDismissRequest, {}, {}, [=] {
                const auto item = [&](const char* text) {
                    m3::DropdownMenuItem([=] { m3::Text(text); }, onDismissRequest, {}, {});
                };
                item("View design guidelines");
                item("View developer docs");
                item("View source code");
                m3::HorizontalDivider();
                item("Report an issue");
                m3::HorizontalDivider();
                item("Terms of service");
                item("Privacy policy");
                item("Open source licenses");
            });
        }

        struct ScaffoldArgs {
            std::string title;
            kiln::Function<void()> topBarBottomContent;
            std::shared_ptr<m3::TopAppBarScrollBehavior> scrollBehavior;
            bool showBackNavigationIcon = false;
            kiln::Function<void()> onBackClick;
            bool favorite = false;
            kiln::Function<void()> onFavoriteClick;
        };

        void CatalogTopAppBar(const ScaffoldArgs& a) {
            auto& moreMenuExpanded = RememberState(false);
            m3::TopAppBarOptions o;
            o.scrollBehavior = a.scrollBehavior;
            o.actions = [a, &moreMenuExpanded] {
                Box({}, Alignment::TopStart(), false, [a, &moreMenuExpanded] {
                    Row({}, Arrangement::Start(), AlignmentV::Top(), [a, &moreMenuExpanded] {
                        m3::IconButton(a.onFavoriteClick, {}, {}, [favorite = a.favorite] {
                            const auto& cs = m3::MaterialTheme::colorScheme();
                            m3::Icon(favorite ? Icons::Filled::PushPin() : Icons::Outlined::PushPin(), std::string("Pin screen"), {},
                                     favorite ? cs.primary : kiln::rt::Composition::Current()->Read(m3::LocalContentColor));
                        });
                        m3::IconButton([] {}, {}, {}, [] { m3::Icon(PaletteIcon(), std::string("Change theme")); });
                        m3::IconButton([&moreMenuExpanded] { moreMenuExpanded.Set(true); }, {}, {},
                                       [] { m3::Icon(Icons::Filled::MoreVert(), std::string("External links")); });
                    });
                    MoreMenu(moreMenuExpanded.Get(), [&moreMenuExpanded] { moreMenuExpanded.Set(false); });
                });
            };
            if (a.showBackNavigationIcon) {
                o.navigationIcon = [onBack = a.onBackClick] {
                    m3::IconButton(onBack, {}, {}, [] { m3::Icon(Icons::AutoMirrored::Filled::ArrowBack(), std::string("Back")); });
                };
            }
            m3::TopAppBar([title = a.title] {
                m3::TextOptions t;
                t.maxLines = 1;
                t.overflow = kiln::text::TextOverflow::Ellipsis;
                m3::Text(title, {}, t);
            }, {}, o);
        }

        // CatalogScaffold: the top bar (and what goes under it) over the content.
        void CatalogScaffold(const ScaffoldArgs& a, kiln::Function<void(const PaddingValues&)> content) {
            m3::ScaffoldOptions o;
            o.topBar = [a] {
                Column({}, Arrangement::Top(), AlignmentH::Start(), [a] {
                    CatalogTopAppBar(a);
                    if (a.topBarBottomContent) a.topBarBottomContent();
                });
            };
            m3::Scaffold(NestedScroll({}, a.scrollBehavior->NestedScrollConnection()), o, [content](const PaddingValues& padding) {
                Box(Padding({}, padding), Alignment::TopStart(), false, [content] { content(PaddingValues{}); });
            });
        }

        // CatalogSearchField: a tonal text field on the top bar's colour.
        void CatalogSearchField(TextFieldState& textFieldState, std::shared_ptr<m3::TopAppBarScrollBehavior> scrollBehavior) {
            const m3::TopAppBarColors colors = m3::TopAppBarDefaults::topAppBarColors();
            const Color target = scrollBehavior->State().OverlappedFraction() > 0.01f ? colors.scrolledContainerColor : colors.containerColor;
            const Color container = AnimateColorAsState(target, m3::MaterialTheme::motionScheme()->DefaultEffectsSpec());
            Box(Padding(Padding(DrawBehind({}, [container](DrawScope& d) { d.DrawRect(container, {}, d.GetSize()); }), 16_dp, 0_dp), { 0_dp, 0_dp, 0_dp, 4_dp }),
                Alignment::TopStart(), false, [&textFieldState] {
                    m3::TextFieldOptions o;
                    o.lineLimits = TextFieldLineLimits::SingleLine();
                    o.placeholder = [] { m3::Text("Search samples"); };
                    o.leadingIcon = [] { m3::Icon(Icons::Filled::Search(), std::nullopt); };
                    if (!textFieldState.Text().empty()) {
                        o.trailingIcon = [&textFieldState] {
                            m3::IconButton([&textFieldState] { textFieldState.ClearText(); }, {}, {},
                                           [] { m3::Icon(Icons::Filled::Clear(), std::string("Clear text")); });
                        };
                    }
                    o.shape = m3::TextFieldDefaults::roundedShape();
                    o.colors = m3::TextFieldDefaults::tonalColors();
                    m3::TextField(textFieldState, FillMax({}, Axis::Horizontal), o);
                });
        }

        bool ContainsIgnoreCase(const std::string& haystack, const std::string& needle) {
            const auto lower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                return s;
            };
            return lower(haystack).find(lower(needle)) != std::string::npos;
        }

        PaddingValues Plus(const PaddingValues& p, Dp all) { return { p.start + all, p.top + all, p.end + all, p.bottom + all }; }

        // ComponentItem.kt, ExampleItem.kt and Component.kt dimensions.
        constexpr Dp ComponentItemHeight{ 180 }, ComponentItemOuterPadding{ 4 }, ComponentItemInnerPadding{ 16 }, ComponentItemIconSize{ 80 },
            ComponentItemBannerSize{ 80 };
        constexpr Dp ExampleItemPadding{ 16 }, ExampleItemTextPadding{ 8 }, ExampleItemBannerSize{ 64 };
        constexpr Dp ComponentIconSize{ 108 }, ComponentIconVerticalPadding{ 42 }, ComponentPadding{ 16 }, ComponentDescriptionPadding{ 32 },
            ComponentExampleItemPadding{ 8 };

        constexpr Dp ComponentItemPreviewBottomPadding{ 16 };
        constexpr float ComponentItemPreviewAlpha = 0.8f;

        // ComponentPreviewBitmap: the preview greyed out (no saturation, 80% alpha), drawn live
        // once and then shown as the image captured from it (cached per component and surface
        // colour). A cover takes touches while it is live, so they reach the tile.
        kiln::gfx::ColorFilter GrayscaleColorFilter() {
            return kiln::gfx::ColorFilter::ColorMatrix({
                0.213f, 0.715f, 0.072f, 0, 0,
                0.213f, 0.715f, 0.072f, 0, 0,
                0.213f, 0.715f, 0.072f, 0, 0,
                0, 0, 0, 1, 0,
            });
        }
        std::map<std::pair<std::string, std::uint32_t>, kiln::gfx::ImageBitmap>& PreviewBitmapCache() {
            static std::map<std::pair<std::string, std::uint32_t>, kiln::gfx::ImageBitmap> cache;
            return cache;
        }

        void ComponentPreviewBox(const std::string& name, const kiln::Function<void()>& preview) {
            auto& c = *kiln::rt::Composition::Current();
            const auto cacheKey = std::make_pair(name, m3::MaterialTheme::colorScheme().surface.argb);
            auto& bitmap = c.Remember<std::shared_ptr<kiln::rt::State<kiln::gfx::ImageBitmap>>>(
                [&] {
                    auto it = PreviewBitmapCache().find(cacheKey);
                    return std::make_shared<kiln::rt::State<kiln::gfx::ImageBitmap>>(it == PreviewBitmapCache().end() ? kiln::gfx::ImageBitmap() : it->second);
                }, kiln::rt::Keys(name, cacheKey.second));
            const Modifier placed = Padding(AlignInBox({}, Alignment::Center()), { 0_dp, 0_dp, 0_dp, ComponentItemPreviewBottomPadding });
            if (const kiln::gfx::ImageBitmap shown = bitmap->Get()) {
                Image(shown, std::nullopt, placed, Alignment::Center(), ContentScale::Fit(), ComponentItemPreviewAlpha, GrayscaleColorFilter());
                return;
            }
            GraphicsLayerParams layer;
            layer.alpha = ComponentItemPreviewAlpha;
            layer.colorFilter = GrayscaleColorFilter();
            auto& coordinates = c.Remember<std::shared_ptr<kiln::rt::State<std::optional<LayoutCoordinates>>>>(
                [] { return std::make_shared<kiln::rt::State<std::optional<LayoutCoordinates>>>(std::nullopt); });
            auto coords = coordinates;
            auto target = bitmap;
            // The capture is the layout under the alpha and filter (they apply to the image).
            Box(GraphicsLayer(placed, layer), Alignment::Center(), false, [coords, preview] {
                const Modifier captured = OnGloballyPositioned(ClearAndSetSemantics(Padding({}, 4_dp), [](SemanticsConfiguration&) {}),
                                                               [coords](const LayoutCoordinates& lc) { if (!coords->Peek() && lc.node) coords->Set(lc); });
                Box(captured, Alignment::Center(), false, [preview] {
                    preview();
                    Box(PointerInput(MatchParentSize({}), kiln::rt::Keys(0), [](PointerInputScope&) -> kiln::rt::Task<> { co_return; }));
                });
            });
            const bool positioned = coordinates->Get().has_value();
            c.LaunchedEffect(kiln::rt::Keys(positioned), [coords, target, cacheKey, positioned]() -> kiln::rt::Task<> {
                return [](decltype(coords) cs, decltype(target) t, std::pair<std::string, std::uint32_t> key, bool ready) -> kiln::rt::Task<> {
                    if (!ready) co_return;
                    co_await kiln::rt::NextFrame{};   // drawn once
                    const kiln::gfx::ImageBitmap captured = DrawToImageBitmap(*cs->Peek());
                    if (!captured) co_return;
                    PreviewBitmapCache()[key] = captured;
                    t->Set(captured);
                }(coords, target, cacheKey, positioned);
            });
        }

        void ComponentItem(const Component& component, kiln::Function<void()> onClick) {
            m3::OutlinedCard(onClick, Padding(Height({}, ComponentItemHeight), ComponentItemOuterPadding), {}, [&component] {
                Box(FillMax({}, Axis::Both), Alignment::TopStart(), false, [&component] {
                    Box(Padding(FillMax({}, Axis::Both), ComponentItemInnerPadding), Alignment::TopStart(), false, [&component] {
                        if (auto preview = ComponentPreview(component.name)) {
                            ComponentPreviewBox(component.name, preview);
                        } else {
                            const Color tint = kiln::rt::Composition::Current()->Read(m3::LocalContentColor);
                            Image(RememberVectorPainter(ComponentIcon()), std::nullopt, AlignInBox(SizeMod({}, ComponentItemIconSize, ComponentItemIconSize), Alignment::Center()),
                                  Alignment::Center(), ContentScale::Inside(), 1,
                                  component.tintIcon ? std::optional(kiln::gfx::ColorFilter::Tint(tint)) : std::nullopt);
                        }
                        m3::TextOptions t;
                        t.style = m3::MaterialTheme::typography().bodySmall;
                        m3::Text(component.name, AlignInBox({}, Alignment::BottomStart()), t);
                    });
                    if (component.HasExpressiveExamples()) ItemBanner("Expr", ComponentItemBannerSize);
                });
            });
        }

        void ExampleItem(const Example& example, kiln::Function<void()> onClick) {
            m3::OutlinedCard(onClick, FillMax({}, Axis::Horizontal), {}, [&example] {
                Box(FillMax({}, Axis::Both), Alignment::TopStart(), false, [&example] {
                    Row(Padding({}, ExampleItemPadding), Arrangement::Start(), AlignmentV::Top(), [&example] {
                        Column(Weight({}, 1, true), Arrangement::Top(), AlignmentH::Start(), [&example] {
                            m3::TextOptions title, body;
                            title.style = m3::MaterialTheme::typography().titleSmall;
                            body.style = m3::MaterialTheme::typography().bodySmall;
                            m3::Text(example.name, {}, title);
                            Spacer(Height({}, ExampleItemTextPadding));
                            m3::Text(example.content ? example.description : example.description + " (not ported to kiln yet)", {}, body);
                        });
                        Spacer(Width({}, ExampleItemPadding));
                        m3::Icon(Icons::AutoMirrored::Filled::KeyboardArrowRight(), std::nullopt,
                                 AlignInRow({}, AlignmentV::CenterVertically()));
                    });
                    if (example.isExpressive) ItemBanner("Expr", ExampleItemBannerSize);
                });
            });
        }

        struct Nav {
            kiln::rt::State<std::vector<std::string>>* stack;
            // Each route's saveable state, kept while it is on the back stack (NavHost's
            // SaveableStateHolder per entry): home's scroll position survives an example.
            kiln::rt::SaveableStateHolder* states = nullptr;
            std::string Current() const { return stack->Get().back(); }
            void Navigate(const std::string& route) const {
                auto s = stack->Peek();
                s.push_back(route);
                stack->Set(s);
            }
            void Back() const {
                auto s = stack->Peek();
                if (s.size() <= 1) return;
                const std::string popped = s.back();
                s.pop_back();
                if (states && std::find(s.begin(), s.end(), popped) == s.end()) states->RemoveState(popped);
                stack->Set(s);
            }
        };

        void Home(const Nav& nav, const std::string& favoriteRoute, kiln::Function<void(const std::string&)> onFavorite) {
            auto& textFieldState = RememberTextFieldState();
            const auto scrollBehavior = m3::TopAppBarDefaults::pinnedScrollBehavior();
            ScaffoldArgs a;
            a.title = "Compose Material 3";
            a.topBarBottomContent = [&textFieldState, scrollBehavior] { CatalogSearchField(textFieldState, scrollBehavior); };
            a.scrollBehavior = scrollBehavior;
            a.favorite = favoriteRoute == "home";
            a.onFavoriteClick = [onFavorite] { onFavorite("home"); };
            CatalogScaffold(a, [nav, &textFieldState](const PaddingValues& padding) {
                const std::string search = textFieldState.TextUtf8();
                std::vector<const Component*> filtered;
                for (const auto& c : Components()) {
                    const bool blank = search.find_first_not_of(" \t\n") == std::string::npos;
                    if (blank || ContainsIgnoreCase(c.name, search) || ContainsIgnoreCase(c.description, search)) filtered.push_back(&c);
                }
                if (!filtered.empty()) {
                    LazyGridOptions o;
                    o.contentPadding = Plus(padding, 12_dp);
                    LazyVerticalGrid(ConsumeWindowInsets({}, padding), GridCells::Adaptive(180_dp), RememberLazyGridState(),
                                     [nav, filtered](LazyGridScope& s) {
                                         s.Items(int(filtered.size()), [nav, filtered](int i) {
                                             const Component* c = filtered[size_t(i)];
                                             ComponentItem(*c, [nav, c] { nav.Navigate("component/" + std::to_string(c->id)); });
                                         }, [filtered](int i) { return kiln::rt::Key(filtered[size_t(i)]->id); });
                                     },
                                     o);
                } else {
                    m3::TextOptions t;
                    t.textAlign = kiln::text::TextAlign::Center;
                    m3::Text("No matching samples.\nCheck your search filter and settings.",
                             FillMax(Padding(Padding({}, 16_dp, 0_dp), { 0_dp, 48_dp, 0_dp, 0_dp }), Axis::Horizontal), t);
                }
            });
        }

        void ComponentScreen(const Nav& nav, const Component& component, const std::string& favoriteRoute,
                             kiln::Function<void(const std::string&)> onFavorite) {
            const std::string route = "component/" + std::to_string(component.id);
            ScaffoldArgs a;
            a.title = component.name;
            a.scrollBehavior = m3::TopAppBarDefaults::pinnedScrollBehavior();
            a.showBackNavigationIcon = true;
            a.onBackClick = [nav] { nav.Back(); };
            a.favorite = favoriteRoute == route;
            a.onFavoriteClick = [onFavorite, route] { onFavorite(route); };
            CatalogScaffold(a, [nav, &component](const PaddingValues& padding) {
                LazyListOptions o;
                o.contentPadding = Plus(padding, ComponentPadding);
                LazyColumn(ConsumeWindowInsets({}, padding), RememberLazyListState(), [nav, &component](LazyListScope& s) {
                    s.Item([&component] {
                        Box(Padding(FillMax({}, Axis::Horizontal), 0_dp, ComponentIconVerticalPadding), Alignment::TopStart(), false, [&component] {
                            const Color tint = kiln::rt::Composition::Current()->Read(m3::LocalContentColor);
                            Image(RememberVectorPainter(ComponentIcon()), std::nullopt, AlignInBox(SizeMod({}, ComponentIconSize, ComponentIconSize), Alignment::Center()),
                                  Alignment::Center(), ContentScale::Fit(), 1,
                                  component.tintIcon ? std::optional(kiln::gfx::ColorFilter::Tint(tint)) : std::nullopt);
                        });
                    });
                    s.Item([&component] {
                        Column({}, Arrangement::Top(), AlignmentH::Start(), [&component] {
                            m3::TextOptions large, medium;
                            large.style = m3::MaterialTheme::typography().bodyLarge;
                            medium.style = m3::MaterialTheme::typography().bodyMedium;
                            m3::Text("Description", {}, large);
                            Spacer(Height({}, ComponentPadding));
                            m3::Text(component.description, {}, medium);
                            Spacer(Height({}, ComponentDescriptionPadding));
                        });
                    });
                    s.Item([] {
                        m3::TextOptions large;
                        large.style = m3::MaterialTheme::typography().bodyLarge;
                        m3::Text("Examples", {}, large);
                        Spacer(Height({}, ComponentPadding));
                    });
                    if (!component.examples.empty()) {
                        s.Items(int(component.examples.size()), [nav, &component](int i) {
                            const Example& e = component.examples[size_t(i)];
                            ExampleItem(e, [nav, &component, i] {
                                nav.Navigate("example/" + std::to_string(component.id) + "/" + std::to_string(i));
                            });
                            Spacer(Height({}, ComponentExampleItemPadding));
                        });
                    } else {
                        s.Item([] {
                            Box(FillMax({}, Axis::Both), Alignment::Center(), false, [] {
                                m3::TextOptions medium;
                                medium.style = m3::MaterialTheme::typography().bodyMedium;
                                m3::Text("No examples", {}, medium);
                            });
                        });
                    }
                }, o);
            });
        }

        void ExampleScreen(const Nav& nav, const Component& component, int index, const std::string& favoriteRoute,
                           kiln::Function<void(const std::string&)> onFavorite) {
            const Example& example = component.examples[size_t(index)];
            const std::string route = "example/" + std::to_string(component.id) + "/" + std::to_string(index);
            ScaffoldArgs a;
            a.title = example.name;
            a.scrollBehavior = m3::TopAppBarDefaults::pinnedScrollBehavior();
            a.showBackNavigationIcon = true;
            a.onBackClick = [nav] { nav.Back(); };
            a.favorite = favoriteRoute == route;
            a.onFavoriteClick = [onFavorite, route] { onFavorite(route); };
            const int width = component.exampleWidthDp;
            CatalogScaffold(a, [&example, width](const PaddingValues& padding) {
                Box(Padding(ConsumeWindowInsets(FillMax({}, Axis::Both), WindowInsets::SafeDrawing()), padding), Alignment::Center(), false, [&example, width] {
                    const auto content = [&example] {
                        if (example.content) example.content();
                        else m3::Text("Not ported to kiln yet");
                    };
                    if (width > 0) Box(Width(WrapContent({}, Axis::Horizontal), Dp(float(width))), Alignment::TopStart(), false, content);
                    else content();
                });
            });
        }

        // The route's parts: component id and example index (-1 when absent).
        struct Parsed { int component = -1, example = -1; };
        Parsed Parse(const std::string& route) {
            Parsed p;
            if (std::sscanf(route.c_str(), "example/%d/%d", &p.component, &p.example) == 2) return p;
            p = {};
            std::sscanf(route.c_str(), "component/%d", &p.component);
            return p;
        }
        const Component* Find(int id) {
            for (const auto& c : Components())
                if (c.id == id) return &c;
            return nullptr;
        }

        void NavGraph() {
            auto& stack = RememberState<std::vector<std::string>>({ "home" });
            auto& initialLaunch = RememberState(true);
            auto& favorite = RememberState(InitialRoute());
            auto& states = kiln::rt::RememberSaveableStateHolder();
            const Nav nav{ &stack, &states };
            const auto onFavorite = [&favorite](const std::string& route) { favorite.Set(favorite.Peek() == route ? std::string() : route); };
            BackHandler(stack.Get().size() > 1, [nav] { nav.Back(); });
            const std::string route = nav.Current();
            const Parsed p = Parse(route);
            const Component* component = p.component > 0 ? Find(p.component) : nullptr;
            const std::string fav = favorite.Get();
            // Each destination in its own scope, keyed by route, as NavHost keeps them apart.
            kiln::rt::Composition::Current()->Child([=, &states] {
                kiln::rt::SaveableStateProvider(states, route, [=] {
                    if (component && p.example >= 0) ExampleScreen(nav, *component, p.example, fav, onFavorite);
                    else if (component) ComponentScreen(nav, *component, fav, onFavorite);
                    else Home(nav, fav, onFavorite);
                });
            }, kiln::rt::Key(std::string_view(route)));
            // maybeNavigate, on the first composition after home: an example opens over its
            // component, over home.
            if (initialLaunch.Peek()) {
                initialLaunch.Set(false);
                const std::string initial = InitialRoute();
                const Parsed ip = Parse(initial);
                std::vector<std::string> s{ "home" };
                if (ip.component > 0 && Find(ip.component)) s.push_back("component/" + std::to_string(ip.component));
                if (ip.example >= 0 && Find(ip.component) && size_t(ip.example) < Find(ip.component)->examples.size()) s.push_back(initial);
                if (s.size() > 1) stack.Set(s);
            }
        }
    }
}

namespace catalog {
    namespace {
        // Golden mode (host, KILN_CATALOG_GOLDEN=<dir>): every example in turn, opened over its
        // component and home as the catalog does, settled for a fixed number of frames, then
        // captured to <dir>/<route>.pam; exits after the last. tools/catalog-golden drives it
        // and compares two runs (a renderer change against its parent commit).
        std::vector<std::string> AllExampleRoutes() {
            std::vector<std::string> out;
            for (const auto& c : Components())
                for (std::size_t i = 0; i < c.examples.size(); ++i) out.push_back("example/" + std::to_string(c.id) + "/" + std::to_string(i));
            return out;
        }
        std::vector<std::string> StackFor(const std::string& route) {
            const Parsed p = Parse(route);
            return { "home", "component/" + std::to_string(p.component), route };
        }
        void WritePam(const std::string& path, const kiln::gfx::ImageBitmap& b) {
            std::FILE* f = std::fopen(path.c_str(), "wb");
            if (!f) return;
            std::fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", b.Width(), b.Height());
            std::fwrite(b.Pixels(), 1, std::size_t(b.Width()) * b.Height() * 4, f);
            std::fclose(f);
        }

        void Golden(std::string dir) {
            // KILN_CATALOG_GOLDEN_FROM=<route>: start there (narrowing a failure down).
            static const std::vector<std::string> routes = [] {
                std::vector<std::string> all = AllExampleRoutes();
                if (const char* from = std::getenv("KILN_CATALOG_GOLDEN_FROM")) {
                    auto it = std::find(all.begin(), all.end(), std::string(from));
                    if (it != all.end()) all.erase(all.begin(), it);
                }
                return all;
            }();
            auto& c = *kiln::rt::Composition::Current();
            auto& index = RememberState(0);
            auto& stack = RememberState<std::vector<std::string>>(StackFor(routes.front()));
            auto coords = c.Remember<std::shared_ptr<std::optional<LayoutCoordinates>>>([] { return std::make_shared<std::optional<LayoutCoordinates>>(); });
            const Nav nav{ &stack };
            const std::string route = nav.Current();
            Box(OnGloballyPositioned(FillMax({}, Axis::Both), [coords](const LayoutCoordinates& lc) { *coords = lc; }), Alignment::TopStart(), false,
                [nav, route] {
                    const Parsed p = Parse(route);
                    const Component* component = p.component > 0 ? Find(p.component) : nullptr;
                    kiln::rt::Composition::Current()->Child([=] {
                        if (component && p.example >= 0) ExampleScreen(nav, *component, p.example, "", [](const std::string&) {});
                    }, kiln::rt::Key(std::string_view(route)));
                });
            const int i = index.Get();
            auto* indexState = &index;
            auto* stackState = &stack;
            c.LaunchedEffect(kiln::rt::Keys(i), [=]() -> kiln::rt::Task<> {
                return [](int i, std::string dir, decltype(coords) cs, kiln::rt::State<int>* idx, kiln::rt::State<std::vector<std::string>>* st) -> kiln::rt::Task<> {
                    for (int f = 0; f < 90; ++f) co_await kiln::rt::NextFrame{};   // settled (1.5 s of animation)
                    if (*cs) {
                        const kiln::gfx::ImageBitmap shot = DrawToImageBitmap(**cs);
                        std::string name = routes[std::size_t(i)];
                        for (char& ch : name) if (ch == '/') ch = '_';
                        if (shot) WritePam(dir + "/" + name + ".pam", shot);
                        std::fprintf(stderr, "golden: %s\n", routes[std::size_t(i)].c_str());
                    }
                    if (std::size_t(i + 1) >= routes.size()) std::exit(0);
                    st->Set(StackFor(routes[std::size_t(i + 1)]));
                    idx->Set(i + 1);
                }(i, dir, coords, indexState, stackState);
            });
        }
    }
}

namespace catalog {
    // CatalogTheme with the default Theme (ThemeColorMode.System, ColorMode.Baseline): the
    // baseline light or dark scheme as the system's night mode says.
    void CatalogTheme(kiln::Function<void()> content) {
        kiln::material3::MaterialThemeArgs args;
        args.colorScheme = kiln::ui::IsSystemInDarkTheme() ? kiln::material3::DarkColorScheme() : kiln::material3::LightColorScheme();
        kiln::material3::MaterialTheme(args, std::move(content));
    }
}

extern "C" void kiln_main(kiln_app* app) {
#if !defined(__ANDROID__)
    if (const char* golden = std::getenv("KILN_CATALOG_GOLDEN")) {
        const std::string dir = golden;
        kiln::SetContent(app, [dir] { catalog::CatalogTheme([dir] { catalog::Golden(dir); }); });
        return;
    }
#endif
    kiln::SetContent(app, [] { catalog::CatalogTheme([] { catalog::NavGraph(); }); });
}
