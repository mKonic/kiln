// TabSamples.kt
#include "TabSamples.h"
#include "Common.h"
#include "Shared.h"

#include <kiln/animation/Animatable.h>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Border.h>
#include <kiln/foundation/Draw.h>
#include <kiln/graphics/Path.h>
#include <kiln/material3/Badge.h>
#include <kiln/material3/Tab.h>
#include <kiln/material3/TabRow.h>

#include <memory>
#include <vector>

namespace catalog::samples {
    namespace {
        using Titles = std::vector<std::string>;
        const Titles threeTitles = { "Tab 1", "Tab 2", "Tab 3 with lots of text" };
        const Titles shortTitles = { "Tab 1", "Tab 2", "Tab 3" };
        const Titles tenTitles = { "Tab 1", "Tab 2", "Tab 3 with lots of text", "Tab 4", "Tab 5", "Tab 6 with lots of text",
                                   "Tab 7", "Tab 8", "Tab 9 with lots of text", "Tab 10" };

        using TabRowFn = void (*)(int, const Modifier&, const m3::TabRowOptions&, std::function<void()>);

        // The column every sample shares: the row of tabs, then "<what> tab N selected".
        void TabsColumn(int state, const char* what, std::function<void()> row) {
            Column({}, [state, what, row] {
                row();
                TextStyled(std::string(what) + " tab " + std::to_string(state + 1) + " selected", m3::MaterialTheme::typography().bodyLarge,
                           AlignInColumn({}, AlignmentH::CenterHorizontally()));
            });
        }

        std::function<void()> TitleText(const std::string& title, bool twoLines) {
            return [title, twoLines] {
                m3::TextOptions o;
                if (twoLines) {
                    o.maxLines = 2;
                    o.overflow = kiln::text::TextOverflow::Ellipsis;
                }
                m3::Text(title, {}, o);
            };
        }

        // Text tabs (titles of at most two lines) in a fixed row.
        void TextTabs(TabRowFn tabRow, const char* what) {
            auto& state = RememberSaveableState(0);
            const int selected = state.Get();
            TabsColumn(selected, what, [tabRow, selected, &state] {
                tabRow(selected, {}, {}, [selected, &state] {
                    for (int index = 0; index < int(threeTitles.size()); index++)
                        m3::Tab(selected == index, [&state, index] { state.Set(index); }, {}, TitleText(threeTitles[std::size_t(index)], true), nullptr);
                });
            });
        }

        // Icon-only tabs, each with a "Favorite" tooltip.
        void IconTabs(TabRowFn tabRow) {
            auto& state = RememberSaveableState(0);
            const int selected = state.Get();
            TabsColumn(selected, "Icon", [tabRow, selected, &state] {
                tabRow(selected, {}, {}, [selected, &state] {
                    for (int index = 0; index < 3; index++) {
                        PlainTooltipAbove("Favorite", [selected, &state, index] {
                            m3::Tab(selected == index, [&state, index] { state.Set(index); }, {}, nullptr,
                                    [] { m3::Icon(Icons::Filled::Favorite(), std::string("Favorite")); });
                        });
                    }
                });
            });
        }

        using ScrollableRowFn = void (*)(int, const Modifier&, const m3::ScrollableTabRowOptions&, std::function<void()>);

        void ScrollingTextTabs(ScrollableRowFn tabRow, const char* what) {
            auto& state = RememberSaveableState(0);
            const int selected = state.Get();
            TabsColumn(selected, what, [tabRow, selected, &state] {
                tabRow(selected, {}, {}, [selected, &state] {
                    for (int index = 0; index < int(tenTitles.size()); index++)
                        m3::Tab(selected == index, [&state, index] { state.Set(index); }, {}, TitleText(tenTitles[std::size_t(index)], false), nullptr);
                });
            });
        }

        void FancyTab(const std::string& title, std::function<void()> onClick, bool selected) {
            m3::Tab(selected, std::move(onClick), {}, m3::TabOptions{}, [title, selected] {
                Column(FillMax(Height(Padding({}, 10_dp), 50_dp), Axis::Horizontal), Arrangement::SpaceBetween(), AlignmentH::Start(), [title, selected] {
                    const auto& cs = m3::MaterialTheme::colorScheme();
                    Box(Background(AlignInColumn(Size({}, 10_dp), AlignmentH::CenterHorizontally()), selected ? cs.primary : cs.background));
                    TextStyled(title, m3::MaterialTheme::typography().bodyLarge, AlignInColumn({}, AlignmentH::CenterHorizontally()));
                });
            });
        }

        // A rounded rectangle outline 5 dp inside the tab.
        void FancyIndicator(Color color, const Modifier& modifier) {
            Box(Border(FillMax(Padding(modifier, 5_dp), Axis::Both), kiln::foundation::BorderStroke{ 2_dp, color }, RoundedCorner(5_dp)));
        }

        // The outline that slides between tabs: its edge leading the move springs fast, the other
        // slow, and its colour cycles through primary, secondary and tertiary.
        void FancyAnimatedIndicatorWithModifier(m3::TabIndicatorScope& scope, int index, bool isScrollable = false) {
            const auto& cs = m3::MaterialTheme::colorScheme();
            const Color colors[3] = { cs.primary, cs.secondary, cs.tertiary };
            struct Edges {
                std::unique_ptr<kiln::anim::Animatable> start, end;
            };
            auto edges = Here().Remember<std::shared_ptr<Edges>>([] { return std::make_shared<Edges>(); });
            const LaunchScope coroutineScope = RememberCoroutineScope();
            const Color indicatorColor = AnimateColorAsState(colors[index % 3]);

            const auto layout = [edges, coroutineScope, index, isScrollable](MeasureScope& s, Measurable& measurable, const Constraints& constraints,
                                                                             const std::vector<m3::TabPosition>& tabPositions) {
                const float newStart = tabPositions[std::size_t(index)].left.value;
                const float newEnd = tabPositions[std::size_t(index)].Right().value;
                if (!edges->start) edges->start = std::make_unique<kiln::anim::Animatable>(newStart);
                if (!edges->end) edges->end = std::make_unique<kiln::anim::Animatable>(newEnd);
                kiln::anim::Animatable& startAnim = *edges->start;
                kiln::anim::Animatable& endAnim = *edges->end;

                if (endAnim.Target()[0] != newEnd) {
                    const bool growing = endAnim.Target()[0] < newEnd;
                    coroutineScope.Launch([](kiln::anim::Animatable& a, float target, bool fast) -> kiln::rt::Task<> {
                        co_await a.AnimateTo(target, kiln::anim::Spring(1, fast ? 1000.f : 50.f));
                    }(endAnim, newEnd, growing));
                }
                if (startAnim.Target()[0] != newStart) {
                    // Moving right, the right edge leads; moving left, the left one.
                    const bool movingRight = startAnim.Target()[0] < newStart;
                    coroutineScope.Launch([](kiln::anim::Animatable& a, float target, bool fast) -> kiln::rt::Task<> {
                        co_await a.AnimateTo(target, kiln::anim::Spring(1, fast ? 1000.f : 50.f));
                    }(startAnim, newStart, !movingRight));
                }

                const int indicatorEnd = s.RoundToPx(Dp(endAnim.Value()[0]));
                const int indicatorStart = s.RoundToPx(Dp(startAnim.Value()[0]));
                Constraints c = constraints;
                c.minWidth = c.maxWidth = indicatorEnd - indicatorStart;
                Placeable& placeable = measurable.Measure(c);
                const int contentWidth = s.RoundToPx(tabPositions[std::size_t(index)].contentWidth);
                const int tabWidth = s.RoundToPx(tabPositions[std::size_t(index)].width);
                const int relativeOffset = isScrollable ? (tabWidth - contentWidth) / 2 : 0;
                return s.Layout(constraints.maxWidth, constraints.maxHeight,
                                [&placeable, indicatorStart, relativeOffset](PlacementScope& p) { p.Place(placeable, indicatorStart - relativeOffset, 0); });
            };
            Box(DrawWithContent(FillMax(Padding(scope.TabIndicatorLayout({}, layout), 5_dp), Axis::Both),
                                [indicatorColor](ContentDrawScope& d) {
                                    const float stroke = d.ToPx(2_dp);
                                    kiln::gfx::Path path;
                                    const kiln::ui::Size size = d.GetSize();
                                    path.AddRoundRect(kiln::gfx::RoundRect::Uniform({ 0, 0, size.width, size.height }, d.ToPx(5_dp)));
                                    kiln::gfx::Stroke st;
                                    st.width = stroke;
                                    d.DrawPath(path, indicatorColor, &st);
                                }));
        }

        void FancyContainerTabs(bool scrolling) {
            auto& state = RememberSaveableState(0);
            const int selected = state.Get();
            const Titles& titles = scrolling ? tenTitles : shortTitles;
            TabsColumn(selected, scrolling ? "Scrolling fancy transition" : "Fancy transition", [selected, &state, &titles, scrolling] {
                const auto tabs = [selected, &state, &titles] {
                    for (int index = 0; index < int(titles.size()); index++)
                        m3::Tab(selected == index, [&state, index] { state.Set(index); }, {}, TitleText(titles[std::size_t(index)], false), nullptr);
                };
                if (scrolling) {
                    m3::ScrollableTabRowOptions o;
                    o.indicator = [selected](m3::TabIndicatorScope& s) { FancyAnimatedIndicatorWithModifier(s, selected, true); };
                    m3::SecondaryScrollableTabRow(selected, {}, o, tabs);
                } else {
                    m3::TabRowOptions o;
                    o.indicator = [selected](m3::TabIndicatorScope& s) { FancyAnimatedIndicatorWithModifier(s, selected); };
                    m3::SecondaryTabRow(selected, {}, o, tabs);
                }
            });
        }
    }

    void PrimaryTextTabs() { TextTabs(m3::PrimaryTabRow, "Primary"); }
    void PrimaryIconTabs() { IconTabs(m3::PrimaryTabRow); }
    void SecondaryTextTabs() { TextTabs(m3::SecondaryTabRow, "Secondary"); }
    void SecondaryIconTabs() { IconTabs(m3::SecondaryTabRow); }

    void TextAndIconTabs() {
        auto& state = RememberSaveableState(0);
        const int selected = state.Get();
        TabsColumn(selected, "Text and icon", [selected, &state] {
            m3::PrimaryTabRow(selected, {}, {}, [selected, &state] {
                for (int index = 0; index < 3; index++)
                    m3::Tab(selected == index, [&state, index] { state.Set(index); }, {}, TitleText(threeTitles[std::size_t(index)], true),
                            [] { m3::Icon(Icons::Filled::Favorite(), std::nullopt); });
            });
        });
    }

    void LeadingIconTabs() {
        static const Titles titles = { "Tab", "Tab & icon", "Tab 3 with lots of text" };
        auto& state = RememberSaveableState(0);
        const int selected = state.Get();
        TabsColumn(selected, "Leading icon", [selected, &state] {
            m3::PrimaryTabRow(selected, {}, {}, [selected, &state] {
                for (int index = 0; index < 3; index++) {
                    const std::string title = titles[std::size_t(index)];
                    m3::LeadingIconTab(
                        selected == index, [&state, index] { state.Set(index); },
                        [title] { m3::BadgedBox([] { m3::Badge({}, std::nullopt, std::nullopt, [] { m3::Text("999+"); }); }, {}, [title] { m3::Text(title); }); },
                        [] { m3::Icon(Icons::Filled::Favorite(), std::nullopt); }, {});
                }
            });
        });
    }

    void ScrollingPrimaryTextTabs() { ScrollingTextTabs(m3::PrimaryScrollableTabRow, "Scrolling primary"); }
    void ScrollingSecondaryTextTabs() { ScrollingTextTabs(m3::SecondaryScrollableTabRow, "Scrolling secondary"); }

    void FancyTabs() {
        auto& state = RememberSaveableState(0);
        const int selected = state.Get();
        TabsColumn(selected, "Fancy", [selected, &state] {
            m3::SecondaryTabRow(selected, {}, {}, [selected, &state] {
                for (int index = 0; index < 3; index++) FancyTab(shortTitles[std::size_t(index)], [&state, index] { state.Set(index); }, index == selected);
            });
        });
    }

    void FancyIndicatorTabs() {
        auto& state = RememberSaveableState(0);
        const int selected = state.Get();
        TabsColumn(selected, "Fancy indicator", [selected, &state] {
            m3::TabRowOptions o;
            o.indicator = [selected](m3::TabIndicatorScope& s) {
                FancyIndicator(m3::MaterialTheme::colorScheme().primary, s.TabIndicatorOffset({}, selected));
            };
            m3::SecondaryTabRow(selected, {}, o, [selected, &state] {
                for (int index = 0; index < 3; index++)
                    m3::Tab(selected == index, [&state, index] { state.Set(index); }, {}, TitleText(shortTitles[std::size_t(index)], false), nullptr);
            });
        });
    }

    void FancyIndicatorContainerTabs() { FancyContainerTabs(false); }
    void ScrollingFancyIndicatorContainerTabs() { FancyContainerTabs(true); }
}
