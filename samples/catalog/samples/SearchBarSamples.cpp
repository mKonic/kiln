// SearchBarSamples.kt
#include "SearchBarSamples.h"
#include <kiln/Function.h>
#include "Common.h"
#include "Shared.h"

#include <kiln/foundation/AnimatedVisibility.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/foundation/Scroll.h>
#include <kiln/foundation/Semantics.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/ListItem.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/SearchBar.h>

#include <memory>
#include <string>

namespace catalog::samples {
    namespace SD = m3::SearchBarDefaults;
    using StatePtr = std::shared_ptr<m3::SearchBarState>;

    namespace {
        void Collapse(const StatePtr& state, const LaunchScope& scope) { scope.Launch(state->AnimateToCollapsed()); }

        void SampleSearchResults(kiln::Function<void(const std::string&)> onResultClick) {
            Column(VerticalScroll({}, RememberScrollState()), [onResultClick] {
                for (int idx = 0; idx < 10; ++idx) {
                    const std::string resultText = "Suggestion " + std::to_string(idx);
                    m3::ListItemOptions o;
                    o.supportingContent = [] { m3::Text("Additional info"); };
                    o.leadingContent = [] { m3::Icon(Icons::Filled::Star(), std::nullopt); };
                    m3::ListItemColors c;
                    c.containerColor = Color::Transparent();
                    o.colors = m3::ListItemDefaults::colors(c);
                    m3::ListItem(Padding(FillMax(Clickable({}, [onResultClick, resultText] { onResultClick(resultText); }), Axis::Horizontal), 16_dp, 4_dp), o,
                                 [resultText] { m3::Text(resultText); });
                }
            });
        }

        void SampleLeadingIcon(const StatePtr& state, const LaunchScope& scope) {
            if (state->CurrentValue() == m3::SearchBarValue::Expanded) {
                PlainTooltipAbove("Back", [state, scope] {
                    m3::IconButton([state, scope] { Collapse(state, scope); }, {}, {},
                                   [] { m3::Icon(Icons::AutoMirrored::Filled::ArrowBack(), std::string("Back")); });
                });
            } else {
                m3::Icon(Icons::Filled::Search(), std::nullopt);
            }
        }

        void SampleTrailingIcon() {
            PlainTooltipAbove("Mic", [] { m3::IconButton([] { /* doSomething() */ }, {}, {}, [] { m3::Icon(Icons::Filled::Mic(), std::string("Mic")); }); });
        }

        // The navigation icon and actions: with isAnimated, they slide out while the bar expands.
        void SlidingTipButton(const StatePtr& state, bool isAnimated, bool fromStart, const char* tip, const ImageVectorPtr* icon) {
            const int sign = fromStart ? -1 : 1;
            AnimatedVisibility(
                !isAnimated || state->TargetValue() == m3::SearchBarValue::Collapsed, {},
                [tip, icon] {
                    PlainTooltipAbove(tip, [tip, icon] { m3::IconButton([] { /* doSomething() */ }, {}, {}, [tip, icon] { m3::Icon(*icon, std::string(tip)); }); });
                },
                SlideIn([sign](IntSize s) { return IntOffset{ sign * s.width, 0 }; }, m3::MaterialTheme::motionScheme()->FastSpatialSpec()),
                SlideOut([sign](IntSize s) { return IntOffset{ sign * s.width, 0 }; }, kiln::anim::Tween(150, 0)));
        }
        void SampleNavigationIcon(const StatePtr& state, bool isAnimated = false) { SlidingTipButton(state, isAnimated, true, "Menu", &Icons::Filled::Menu()); }
        void SampleActions(const StatePtr& state, bool isAnimated = false) {
            SlidingTipButton(state, isAnimated, false, "Account", &Icons::Filled::AccountCircle());
        }

        kiln::Function<void()> InputField(kiln::foundation::TextFieldState& textFieldState, StatePtr state, LaunchScope scope,
                                         std::optional<m3::TextFieldColors> colors = std::nullopt) {
            return [&textFieldState, state, scope, colors] {
                m3::SearchBarInputFieldOptions o;
                o.colors = colors;
                o.placeholder = [] { m3::Text("Search", ClearAndSetSemantics({}, [](SemanticsConfiguration&) {})); };
                o.leadingIcon = [state, scope] { SampleLeadingIcon(state, scope); };
                o.trailingIcon = SampleTrailingIcon;
                SD::InputField(textFieldState, state, [state, scope](std::string) { Collapse(state, scope); }, {}, o);
            };
        }

        kiln::Function<void()> Results(kiln::foundation::TextFieldState& textFieldState, StatePtr state, LaunchScope scope) {
            return [&textFieldState, state, scope] {
                SampleSearchResults([&textFieldState, state, scope](const std::string& result) {
                    textFieldState.SetTextAndPlaceCursorAtEnd(result);
                    Collapse(state, scope);
                });
            };
        }

        // The scaffolds' content: "Text 0".."Text 99", 8 dp apart.
        void TextList(const PaddingValues& padding) {
            LazyListOptions o;
            o.contentPadding = padding;
            o.arrangement = Arrangement::SpacedBy(8_dp);
            LazyColumn({}, RememberLazyListState(), [](LazyListScope& scope) {
                scope.Items(100, [](int i) { m3::Text("Text " + std::to_string(i), Padding(FillMax({}, Axis::Horizontal), 16_dp, 0_dp)); });
            }, o);
        }
    }

    void SimpleSearchBarSample() {
        const StatePtr searchBarState = m3::RememberSearchBarState();
        auto& textFieldState = RememberTextFieldState();
        const LaunchScope scope = RememberCoroutineScope();
        const auto inputField = InputField(textFieldState, searchBarState, scope);
        m3::SearchBar(searchBarState, inputField);
        m3::ExpandedFullScreenSearchBar(searchBarState, inputField, {}, {}, Results(textFieldState, searchBarState, scope));
    }

    void FullScreenSearchBarScaffoldSample() {
        auto& textFieldState = RememberTextFieldState();
        const StatePtr searchBarState = m3::RememberContainedSearchBarState();
        const LaunchScope scope = RememberCoroutineScope();
        const auto scrollBehavior = SD::enterAlwaysSearchBarScrollBehavior();
        m3::AppBarWithSearchColors colors = SD::appBarWithSearchColors();
        colors.searchBarColors = SD::containedColors(*searchBarState);
        const auto inputField = InputField(textFieldState, searchBarState, scope, colors.searchBarColors.inputFieldColors);
        m3::ScaffoldOptions s;
        s.topBar = [=, &textFieldState] {
            m3::AppBarWithSearchOptions o;
            o.scrollBehavior = scrollBehavior;
            o.colors = colors;
            o.navigationIcon = [searchBarState] { SampleNavigationIcon(searchBarState, true); };
            o.actions = [searchBarState] { SampleActions(searchBarState, true); };
            m3::AppBarWithSearch(searchBarState, inputField, {}, o);
            m3::ExpandedFullScreenSearchBarOptions e;
            e.colors = colors.searchBarColors;
            m3::ExpandedFullScreenContainedSearchBar(searchBarState, inputField, {}, e, Results(textFieldState, searchBarState, scope));
        };
        m3::Scaffold(NestedScroll({}, scrollBehavior->NestedScrollConnection()), s, TextList);
    }

    void DockedSearchBarScaffoldSample() {
        auto& textFieldState = RememberTextFieldState();
        const StatePtr searchBarState = m3::RememberSearchBarWithGapState();
        const LaunchScope scope = RememberCoroutineScope();
        const auto scrollBehavior = SD::enterAlwaysSearchBarScrollBehavior();
        const m3::AppBarWithSearchColors colors = SD::appBarWithSearchColors();
        const auto inputField = InputField(textFieldState, searchBarState, scope, colors.searchBarColors.inputFieldColors);
        m3::ScaffoldOptions s;
        s.topBar = [=, &textFieldState] {
            m3::AppBarWithSearchOptions o;
            o.scrollBehavior = scrollBehavior;
            o.colors = colors;
            o.navigationIcon = [searchBarState] { SampleNavigationIcon(searchBarState); };
            o.actions = [searchBarState] { SampleActions(searchBarState); };
            m3::AppBarWithSearch(searchBarState, inputField, {}, o);
            m3::ExpandedDockedSearchBarWithGap(searchBarState, inputField, {}, {}, Results(textFieldState, searchBarState, scope));
        };
        m3::Scaffold(NestedScroll({}, scrollBehavior->NestedScrollConnection()), s, TextList);
    }
}
