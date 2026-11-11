#pragma once

// SearchBar.kt: search bars. A collapsed SearchBar (or one in an AppBarWithSearch, which can
// scroll away with the content) expands into ExpandedFullScreenSearchBar (its own window, the
// input field moving and growing into it) or ExpandedDockedSearchBar (a popup dropping down
// from it), both following the predictive back gesture or Back. The input field expands the
// bar when tapped, focused by touch, typed in, or on Down with a keyboard; Search runs onSearch.
// The older SearchBar / DockedSearchBar take an expanded flag instead of a state.
//
// Not ported: the overloads that take the query as a String (they need the value-based
// BasicTextField kiln lacks) and the Styleable variants (Compose's internal style system).

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Dialog.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Interaction.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Popup.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/material3/TextFieldColors.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Modifier.h"
#include "kiln/ui/NestedScroll.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    enum class SearchBarValue : std::uint8_t { Collapsed, Expanded };

    // Where the collapsed bar sits in its window, in pixels.
    struct SearchBarBounds {
        float left = 0, top = 0, right = 0, bottom = 0;
        float Width() const { return right - left; }
        float Height() const { return bottom - top; }
        bool operator==(const SearchBarBounds&) const = default;
    };

    class SearchBarState {
    public:
        // Fade specs: how the expanded content fades (contained and with-gap bars); unset: snap.
        SearchBarState(SearchBarValue initialValue, anim::SpecPtr animationSpecForExpand, anim::SpecPtr animationSpecForCollapse,
                       anim::SpecPtr animationSpecForContentFadeIn = nullptr, anim::SpecPtr animationSpecForContentFadeOut = nullptr);
        // Restored from saved progress values (SearchBarState.Saver).
        SearchBarState(float progress, float contentProgress, anim::SpecPtr animationSpecForExpand, anim::SpecPtr animationSpecForCollapse,
                       anim::SpecPtr animationSpecForContentFadeIn, anim::SpecPtr animationSpecForContentFadeOut);

        // 0 collapsed .. 1 expanded; reading it subscribes every frame.
        float Progress() const;
        bool IsAnimating() const { return m_Animatable->IsRunning(); }
        SearchBarValue TargetValue() const;
        // Expanded once past 2% (a spring's tolerance); reading it subscribes only to changes.
        SearchBarValue CurrentValue() const { return m_Current.Get(); }
        rt::Task<> AnimateToExpanded();
        rt::Task<> AnimateToCollapsed();
        void SnapTo(float fraction);

        // Internal.
        float ContentProgress() const;
        float RawProgress() const { return m_Animatable->Float(); }   // uncoerced
        bool IsExpanded() const { return CurrentValue() == SearchBarValue::Expanded; }
        rt::State<bool> expandsToFullScreen{ false };
        // The collapsed bar's bounds in its window (empty until it is placed).
        rt::State<SearchBarBounds> collapsedBounds{ SearchBarBounds{} };
        rt::State<bool> collapsedPlaced{ false };
        // collapsedCoords?.size?.width, else unset.
        std::optional<int> CollapsedWidth() const;
        anim::SpecPtr animationSpecForExpand, animationSpecForCollapse, animationSpecForContentFadeIn, animationSpecForContentFadeOut;

    private:
        void Derive();
        std::shared_ptr<anim::Animatable> m_Animatable, m_ContentAnimatable;
        rt::State<SearchBarValue> m_Current{ SearchBarValue::Collapsed };
    };

    // rememberSearchBarState. Unset specs: the motion scheme's slow spatial (expand) and default
    // spatial (collapse).
    std::shared_ptr<SearchBarState> RememberSearchBarState(SearchBarValue initialValue = SearchBarValue::Collapsed, anim::SpecPtr animationSpecForExpand = nullptr,
                                                           anim::SpecPtr animationSpecForCollapse = nullptr);
    // For ExpandedFullScreenContainedSearchBar: fast spatial both ways, the content fading in and out.
    std::shared_ptr<SearchBarState> RememberContainedSearchBarState(SearchBarValue initialValue = SearchBarValue::Collapsed,
                                                                    anim::SpecPtr animationSpecForExpand = nullptr, anim::SpecPtr animationSpecForCollapse = nullptr,
                                                                    anim::SpecPtr animationSpecForContentFadeIn = nullptr,
                                                                    anim::SpecPtr animationSpecForContentFadeOut = nullptr);
    // For ExpandedDockedSearchBarWithGap: default spatial to expand, fast spatial to collapse.
    std::shared_ptr<SearchBarState> RememberSearchBarWithGapState(SearchBarValue initialValue = SearchBarValue::Collapsed,
                                                                  anim::SpecPtr animationSpecForExpand = nullptr, anim::SpecPtr animationSpecForCollapse = nullptr,
                                                                  anim::SpecPtr animationSpecForContentFadeIn = nullptr,
                                                                  anim::SpecPtr animationSpecForContentFadeOut = nullptr);

    // How far an AppBarWithSearch has scrolled away (0 shown .. scrollOffsetLimit hidden).
    class SearchBarScrollState {
    public:
        SearchBarScrollState(float initialScrollOffsetLimit, float initialScrollOffset, float initialContentOffset);
        float ScrollOffset() const { return m_ScrollOffset.Get(); }
        void SetScrollOffset(float offset);   // kept within [scrollOffsetLimit, 0]
        float ScrollOffsetLimit() const { return m_Limit.Get(); }
        void SetScrollOffsetLimit(float limit) { m_Limit.Set(limit); }
        float ContentOffset() const { return m_ContentOffset.Get(); }
        void SetContentOffset(float offset) { m_ContentOffset.Set(offset); }

    private:
        rt::State<float> m_Limit, m_ScrollOffset, m_ContentOffset;
    };
    std::shared_ptr<SearchBarScrollState> RememberSearchBarScrollState(float initialScrollOffsetLimit = -std::numeric_limits<float>::max(),
                                                                       float initialScrollOffset = 0, float initialContentOffset = 0);

    class SearchBarScrollBehavior {
    public:
        virtual ~SearchBarScrollBehavior() = default;
        virtual std::shared_ptr<SearchBarScrollState> ScrollState() const = 0;
        virtual std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const = 0;
        // On the bar: drags it, clips it and shrinks it by the scroll offset.
        virtual ui::Modifier SearchBarScrollBehaviorModifier() const = 0;
        // overlappedFraction: how far content has scrolled under the bar.
        float OverlappedFraction() const;
    };

    struct SearchBarColors {
        ui::Color containerColor, dividerColor;
        TextFieldColors inputFieldColors;
        bool operator==(const SearchBarColors&) const = default;
    };

    struct AppBarWithSearchColors {
        SearchBarColors searchBarColors;
        ui::Color scrolledSearchBarContainerColor = ui::Color::Unspecified();   // Unspecified: no change on scroll
        ui::Color appBarContainerColor;
        ui::Color scrolledAppBarContainerColor = ui::Color::Unspecified();
        ui::Color appBarNavigationIconColor, appBarActionIconColor;
        // Between the plain and scrolled colours (FastOutLinearIn on the fraction).
        ui::Color SearchBarContainerColor(float colorTransitionFraction) const;
        ui::Color AppBarContainerColor(float colorTransitionFraction) const;
        bool operator==(const AppBarWithSearchColors&) const = default;
    };

    struct SearchBarInputFieldOptions {
        bool enabled = true, readOnly = false;
        std::optional<text::TextStyle> textStyle;   // unset: LocalTextStyle
        std::function<void()> placeholder, leadingIcon, trailingIcon, prefix, suffix;
        foundation::InputTransformationPtr inputTransformation;
        foundation::OutputTransformationPtr outputTransformation;
        foundation::ScrollState* scrollState = nullptr;   // null: remembered
        foundation::ShapePtr shape;                       // null: inputFieldShape
        std::optional<TextFieldColors> colors;            // unset: inputFieldColors()
        std::shared_ptr<foundation::MutableInteractionSource> interactionSource;
        foundation::KeyboardOptions keyboardOptions;      // the IME action is always Search
        foundation::TextFieldLineLimits lineLimits = foundation::TextFieldLineLimits::SingleLine();
    };

    namespace SearchBarDefaults {
        extern const ui::Dp TonalElevation, ShadowElevation, InputFieldHeight, dockedDropdownGapSize;
        foundation::ShapePtr inputFieldShape();
        foundation::ShapePtr fullScreenShape();
        ui::Color collapsedContainedSearchBarColor();
        ui::Color fullScreenContainedSearchBarColor();
        foundation::ShapePtr dockedShape();
        foundation::ShapePtr dockedDropdownShape();
        ui::Color dockedDropdownScrimColor();
        inline const foundation::PaddingValues AppBarContentPadding{};
        // System bars and the display cutout, horizontal and top.
        foundation::WindowInsets windowInsets();
        foundation::WindowInsets fullScreenWindowInsets();   // safe drawing

        // enterAlwaysSearchBarScrollBehavior: the bar scrolls away with the content and comes back
        // as soon as the content scrolls the other way. Null specs: default effects (snap) and
        // the spline decay (fling).
        std::shared_ptr<SearchBarScrollBehavior> enterAlwaysSearchBarScrollBehavior(std::shared_ptr<SearchBarScrollState> scrollState = nullptr,
                                                                                   std::function<bool()> canScroll = {},
                                                                                   anim::SpecPtr snapAnimationSpec = nullptr,
                                                                                   std::shared_ptr<const anim::DecaySpec> flingAnimationSpec = nullptr,
                                                                                   bool reverseLayout = false);

        SearchBarColors colors();
        SearchBarColors colors(ui::Color containerColor, ui::Color dividerColor, TextFieldColors inputFieldColors);
        // The contained bar: its expanded colour while expanded, with a field in the collapsed colour.
        SearchBarColors containedColors(const SearchBarState& state);
        AppBarWithSearchColors appBarWithSearchColors();
        // A transparent field over the bar's container (TextFieldDefaults.colors with the search
        // bar's tokens).
        TextFieldColors inputFieldColors();

        // The input field for a SearchBarState: expands the bar when focused by touch, tapped,
        // typed in, or on Down from a keyboard (Down in an expanded bar moves focus into its
        // results); Search runs onSearch with the text. Focus clears when the bar collapses.
        void InputField(foundation::TextFieldState& textFieldState, std::shared_ptr<SearchBarState> searchBarState, std::function<void(std::string)> onSearch,
                        const ui::Modifier& modifier = {}, const SearchBarInputFieldOptions& options = {});
        // The input field for the expanded-flag search bars: focus asks to expand.
        void InputField(foundation::TextFieldState& state, std::function<void(std::string)> onSearch, bool expanded, std::function<void(bool)> onExpandedChange,
                        const ui::Modifier& modifier = {}, const SearchBarInputFieldOptions& options = {});
    }

    struct SearchBarOptions {
        foundation::ShapePtr shape;                  // null: inputFieldShape
        std::optional<SearchBarColors> colors;       // unset: colors()
        std::optional<ui::Dp> tonalElevation, shadowElevation;
    };
    // The collapsed search bar: the input field on its container. It records where it sits so
    // the expanded bar can grow out of it.
    void SearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier = {},
                   const SearchBarOptions& options = {});

    struct AppBarWithSearchOptions {
        std::function<void()> navigationIcon, actions;
        foundation::ShapePtr shape;
        std::optional<AppBarWithSearchColors> colors;   // unset: appBarWithSearchColors()
        std::optional<ui::Dp> tonalElevation, shadowElevation;
        foundation::PaddingValues contentPadding = SearchBarDefaults::AppBarContentPadding;
        std::optional<foundation::WindowInsets> windowInsets;   // unset: SearchBarDefaults::windowInsets
        std::shared_ptr<SearchBarScrollBehavior> scrollBehavior;
    };
    // A top app bar holding a collapsed search bar between a navigation icon and actions; its
    // colour changes when content scrolls under it, and a scroll behaviour can hide it.
    void AppBarWithSearch(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier = {},
                          const AppBarWithSearchOptions& options = {});
    // TopSearchBar (renamed AppBarWithSearch in Compose): the bar on a transparent app bar.
    void TopSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier = {},
                      const SearchBarOptions& options = {}, std::optional<foundation::WindowInsets> windowInsets = std::nullopt,
                      std::shared_ptr<SearchBarScrollBehavior> scrollBehavior = nullptr);

    struct ExpandedFullScreenSearchBarOptions {
        foundation::ShapePtr collapsedShape;              // null: inputFieldShape
        std::optional<SearchBarColors> colors;            // unset: colors() (contained: containedColors(state))
        std::optional<ui::Dp> tonalElevation, shadowElevation;
        std::function<foundation::WindowInsets()> windowInsets;   // null: fullScreenWindowInsets
        foundation::DialogProperties properties;
    };
    // While the state is expanded: a window where the bar grows from the collapsed one to fill
    // the screen, the field focused and the content below a divider.
    void ExpandedFullScreenSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                     const ExpandedFullScreenSearchBarOptions& options, std::function<void()> content);
    // The contained variant: the field keeps its pill inside a fading full-screen container.
    void ExpandedFullScreenContainedSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                              const ExpandedFullScreenSearchBarOptions& options, std::function<void()> content);

    struct ExpandedDockedSearchBarOptions {
        foundation::ShapePtr shape;                   // null: dockedShape
        std::optional<SearchBarColors> colors;
        std::optional<ui::Dp> tonalElevation, shadowElevation;
        foundation::PopupProperties properties = [] {
            foundation::PopupProperties p;
            p.focusable = true;
            p.clippingEnabled = false;
            return p;
        }();
        // With a gap (ExpandedDockedSearchBarWithGap): the results drop in a separate card.
        foundation::ShapePtr dropdownShape;           // null: dockedDropdownShape
        std::optional<ui::Dp> dropdownGapSize;        // unset: dockedDropdownGapSize
        std::optional<ui::Color> dropdownScrimColor;  // unset: dockedDropdownScrimColor
    };
    // While the state is expanded: a popup over the collapsed bar that drops down to show the
    // results under the field (up to two thirds of the window).
    void ExpandedDockedSearchBar(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                 const ExpandedDockedSearchBarOptions& options, std::function<void()> content);
    // The results in their own card below a gap, over a scrim (up to half the window).
    void ExpandedDockedSearchBarWithGap(std::shared_ptr<SearchBarState> state, std::function<void()> inputField, const ui::Modifier& modifier,
                                        const ExpandedDockedSearchBarOptions& options, std::function<void()> content);

    struct ExpandableSearchBarOptions {
        foundation::ShapePtr shape;                   // null: inputFieldShape (DockedSearchBar: dockedShape)
        std::optional<SearchBarColors> colors;
        std::optional<ui::Dp> tonalElevation, shadowElevation;
        std::optional<foundation::WindowInsets> windowInsets;   // SearchBar only; unset: windowInsets()
    };
    // The expanded-flag search bar (deprecated in Compose): grows in place to fill its space,
    // following the predictive back gesture; content shows below a divider while expanded.
    void SearchBar(std::function<void()> inputField, bool expanded, std::function<void(bool)> onExpandedChange, const ui::Modifier& modifier,
                   const ExpandableSearchBarOptions& options, std::function<void()> content);
    // The expanded-flag docked search bar: the results drop down below the field.
    void DockedSearchBar(std::function<void()> inputField, bool expanded, std::function<void(bool)> onExpandedChange, const ui::Modifier& modifier,
                         const ExpandableSearchBarOptions& options, std::function<void()> content);

    namespace internal {
        inline constexpr ui::Dp SearchBarMinWidth{ 360 }, SearchBarMaxWidth{ 720 }, SearchBarVerticalPadding{ 8 }, SearchBarAsTopBarPadding{ 8 },
            AppBarWithSearchVerticalPadding{ 4 }, DockedExpandedTableMinHeight{ 240 };
    }

}
