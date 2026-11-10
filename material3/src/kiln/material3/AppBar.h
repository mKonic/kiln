#pragma once

// AppBar.kt: top app bars (small, centre-aligned, with a subtitle, medium, large, their
// flexible forms and two-row ones) and bottom app bars (with actions and a FAB, any content, or
// flexible). Scroll behaviours read the content's nested scroll to pin, collapse or hide a bar,
// and to change a top bar's colour once content scrolls under it.

#include "kiln/animation/Decay.h"
#include "kiln/animation/Spec.h"
#include "kiln/foundation/Arrangement.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/NestedScroll.h"

#include <cfloat>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    // --- top app bars --------------------------------------------------------------------------

    // The offsets a scroll behaviour applies to a top app bar (pixels). Reading them subscribes.
    class TopAppBarState {
    public:
        TopAppBarState(float initialHeightOffsetLimit, float initialHeightOffset, float initialContentOffset);
        // How far the bar can collapse (negative); set by the bar from its height.
        float HeightOffsetLimit() const { return m_HeightOffsetLimit; }
        void SetHeightOffsetLimit(float limit) { m_HeightOffsetLimit = limit; }
        // Between HeightOffsetLimit and 0.
        float HeightOffset() const { return m_HeightOffset.Get(); }
        void SetHeightOffset(float offset);
        // The content's total scroll since it was at its start.
        float ContentOffset() const { return m_ContentOffset.Get(); }
        void SetContentOffset(float offset) { m_ContentOffset.Set(offset); }
        // 0 expanded .. 1 collapsed.
        float CollapsedFraction() const;
        // How much content scrolled under the bar: 0 none .. 1 at least the bar's height.
        float OverlappedFraction() const;
        std::function<bool()> isScrollingContentAtStart = [] { return true; };

    private:
        float m_HeightOffsetLimit;
        rt::State<float> m_HeightOffset;
        rt::State<float> m_ContentOffset;
    };
    std::shared_ptr<TopAppBarState> RememberTopAppBarState(float initialHeightOffsetLimit = -FLT_MAX, float initialHeightOffset = 0,
                                                           float initialContentOffset = 0);

    // Connects a top app bar to the content's nested scroll (pass NestedScrollConnection() to
    // foundation::NestedScroll on a parent of the scrolling content).
    class TopAppBarScrollBehavior {
    public:
        virtual ~TopAppBarScrollBehavior() = default;
        virtual TopAppBarState& State() const = 0;
        virtual bool IsPinned() const = 0;
        virtual anim::SpecPtr SnapAnimationSpec() const = 0;                         // null: no snap
        virtual std::shared_ptr<const anim::DecaySpec> FlingAnimationSpec() const = 0;   // null: no fling
        virtual std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const = 0;
    };

    struct TopAppBarColors {
        ui::Color containerColor, scrolledContainerColor, navigationIconContentColor, titleContentColor, actionIconContentColor,
            subtitleContentColor;
        // Unspecified arguments keep this one's colours.
        TopAppBarColors Copy(ui::Color containerColor = ui::Color::Unspecified(), ui::Color scrolledContainerColor = ui::Color::Unspecified(),
                             ui::Color navigationIconContentColor = ui::Color::Unspecified(), ui::Color titleContentColor = ui::Color::Unspecified(),
                             ui::Color actionIconContentColor = ui::Color::Unspecified(),
                             ui::Color subtitleContentColor = ui::Color::Unspecified()) const;
        // The container colour as the content scrolls under (FastOutLinearIn between the two).
        ui::Color ContainerColor(float colorTransitionFraction) const;
        bool operator==(const TopAppBarColors&) const = default;
    };

    namespace TopAppBarDefaults {
        TopAppBarColors topAppBarColors();
        TopAppBarColors topAppBarColors(ui::Color containerColor, ui::Color scrolledContainerColor = ui::Color::Unspecified(),
                                        ui::Color navigationIconContentColor = ui::Color::Unspecified(),
                                        ui::Color titleContentColor = ui::Color::Unspecified(), ui::Color actionIconContentColor = ui::Color::Unspecified(),
                                        ui::Color subtitleContentColor = ui::Color::Unspecified());
        inline constexpr foundation::PaddingValues ContentPadding{};
        // The system bars and display cutout at the top and the sides.
        foundation::WindowInsets windowInsets();
        anim::SpecPtr snapAnimationSpec();
        std::shared_ptr<const anim::DecaySpec> flingAnimationSpec();   // the platform's spline at this density

        // The bar stays; its colour follows the content scrolled under it.
        std::shared_ptr<TopAppBarScrollBehavior> pinnedScrollBehavior(std::shared_ptr<TopAppBarState> state = nullptr,
                                                                      std::function<bool()> canScroll = {},
                                                                      std::function<bool()> isScrollingContentAtStart = {});
        // The bar collapses as content scrolls up and comes back as soon as it scrolls down.
        std::shared_ptr<TopAppBarScrollBehavior> enterAlwaysScrollBehavior(std::shared_ptr<TopAppBarState> state = nullptr,
                                                                           std::function<bool()> canScroll = {},
                                                                           std::optional<anim::SpecPtr> snapAnimationSpec = std::nullopt,
                                                                           std::optional<std::shared_ptr<const anim::DecaySpec>> flingAnimationSpec = std::nullopt,
                                                                           std::function<bool()> isScrollingContentAtStart = {});
        // The bar collapses as content scrolls up and expands once the content is back at its top.
        std::shared_ptr<TopAppBarScrollBehavior> exitUntilCollapsedScrollBehavior(
            std::shared_ptr<TopAppBarState> state = nullptr, std::function<bool()> canScroll = {},
            std::optional<anim::SpecPtr> snapAnimationSpec = std::nullopt,
            std::optional<std::shared_ptr<const anim::DecaySpec>> flingAnimationSpec = std::nullopt);

        inline constexpr ui::Dp TopAppBarExpandedHeight{ 64 };
        inline constexpr ui::Dp MediumAppBarCollapsedHeight{ 64 }, MediumAppBarExpandedHeight{ 112 };
        inline constexpr ui::Dp MediumFlexibleAppBarWithoutSubtitleExpandedHeight{ 112 }, MediumFlexibleAppBarWithSubtitleExpandedHeight{ 136 };
        inline constexpr ui::Dp LargeAppBarCollapsedHeight{ 64 }, LargeAppBarExpandedHeight{ 152 };
        inline constexpr ui::Dp LargeFlexibleAppBarWithoutSubtitleExpandedHeight{ 120 }, LargeFlexibleAppBarWithSubtitleExpandedHeight{ 152 };
    }

    // A single-row bar (TopAppBar, CenterAlignedTopAppBar and the one with a subtitle).
    struct TopAppBarOptions {
        std::function<void()> navigationIcon;   // typically an IconButton
        std::function<void()> actions;          // in a row at the end, typically IconButtons
        std::function<void()> subtitle;         // under the title
        foundation::AlignmentH titleHorizontalAlignment = foundation::AlignmentH::Start();
        ui::Dp expandedHeight = TopAppBarDefaults::TopAppBarExpandedHeight;   // Unspecified / infinite: the default
        std::optional<foundation::WindowInsets> windowInsets;                 // unset: TopAppBarDefaults
        std::optional<TopAppBarColors> colors;
        std::shared_ptr<TopAppBarScrollBehavior> scrollBehavior;
        foundation::PaddingValues contentPadding = TopAppBarDefaults::ContentPadding;
    };
    void TopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TopAppBarOptions& options = {});
    // The title centred on the whole bar.
    void CenterAlignedTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TopAppBarOptions& options = {});

    // A two-row bar: the large title in a bottom row that collapses into the top row's small one.
    struct TwoRowsTopAppBarOptions {
        std::function<void()> subtitle;   // flexible bars
        std::function<void()> navigationIcon;
        std::function<void()> actions;
        foundation::AlignmentH titleHorizontalAlignment = foundation::AlignmentH::Start();   // flexible bars
        ui::Dp collapsedHeight = ui::Dp::Unspecified();   // Unspecified: the bar's default
        ui::Dp expandedHeight = ui::Dp::Unspecified();
        std::optional<foundation::WindowInsets> windowInsets;
        std::optional<TopAppBarColors> colors;
        std::shared_ptr<TopAppBarScrollBehavior> scrollBehavior;
    };
    void MediumTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& options = {});
    void MediumFlexibleTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& options = {});
    void LargeTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& options = {});
    void LargeFlexibleTopAppBar(std::function<void()> title, const ui::Modifier& modifier, const TwoRowsTopAppBarOptions& options = {});
    // Title and subtitle given whether they are drawn expanded (bottom row) or collapsed (top row).
    void TwoRowsTopAppBar(std::function<void(bool expanded)> title, const ui::Modifier& modifier, std::function<void(bool expanded)> subtitle,
                          const TwoRowsTopAppBarOptions& options = {});

    // --- bottom app bars -----------------------------------------------------------------------

    class BottomAppBarState {
    public:
        BottomAppBarState(float initialHeightOffsetLimit, float initialHeightOffset, float initialContentOffset);
        float HeightOffsetLimit() const { return m_HeightOffsetLimit.Get(); }
        void SetHeightOffsetLimit(float limit) { m_HeightOffsetLimit.Set(limit); }
        float HeightOffset() const { return m_HeightOffset.Get(); }
        void SetHeightOffset(float offset);
        float ContentOffset() const { return m_ContentOffset.Get(); }
        void SetContentOffset(float offset) { m_ContentOffset.Set(offset); }
        float CollapsedFraction() const;

    private:
        rt::State<float> m_HeightOffsetLimit;
        rt::State<float> m_HeightOffset;
        rt::State<float> m_ContentOffset;
    };
    std::shared_ptr<BottomAppBarState> RememberBottomAppBarState(float initialHeightOffsetLimit = -FLT_MAX, float initialHeightOffset = 0,
                                                                 float initialContentOffset = 0);

    class BottomAppBarScrollBehavior {
    public:
        virtual ~BottomAppBarScrollBehavior() = default;
        virtual BottomAppBarState& State() const = 0;
        virtual bool IsPinned() const = 0;
        virtual anim::SpecPtr SnapAnimationSpec() const = 0;
        virtual std::shared_ptr<const anim::DecaySpec> FlingAnimationSpec() const = 0;
        virtual std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const = 0;
    };

    namespace BottomAppBarDefaults {
        ui::Color containerColor();
        inline constexpr ui::Dp ContainerElevation{ 0 };
        // Padding minus the IconButtons' touch target expansion.
        inline constexpr foundation::PaddingValues ContentPadding{ ui::Dp(4), ui::Dp(4), ui::Dp(4), ui::Dp(0) };
        // The system bars and display cutout at the bottom and the sides.
        foundation::WindowInsets windowInsets();
        ui::Color bottomAppBarFabColor();
        inline constexpr foundation::PaddingValues FlexibleContentPadding{ ui::Dp(16), ui::Dp(0), ui::Dp(16), ui::Dp(0) };
        inline constexpr ui::Dp FlexibleBottomAppBarHeight{ 64 };
        inline foundation::Arrangement FlexibleHorizontalArrangement() { return foundation::Arrangement::SpaceBetween(); }
        inline foundation::Arrangement FlexibleFixedHorizontalArrangement() {
            return foundation::Arrangement::SpacedBy(ui::Dp(32), foundation::AlignmentH::CenterHorizontally());
        }
        anim::SpecPtr snapAnimationSpec();
        std::shared_ptr<const anim::DecaySpec> flingAnimationSpec();
        // The bar hides as content scrolls up and comes back as it scrolls down.
        std::shared_ptr<BottomAppBarScrollBehavior> exitAlwaysScrollBehavior(std::shared_ptr<BottomAppBarState> state = nullptr,
                                                                             std::function<bool()> canScroll = {},
                                                                             std::optional<anim::SpecPtr> snapAnimationSpec = std::nullopt,
                                                                             std::optional<std::shared_ptr<const anim::DecaySpec>> flingAnimationSpec = std::nullopt);
    }

    struct BottomAppBarOptions {
        std::optional<ui::Color> containerColor;   // unset: BottomAppBarDefaults
        std::optional<ui::Color> contentColor;     // unset: contentColorFor(containerColor)
        ui::Dp tonalElevation = BottomAppBarDefaults::ContainerElevation;
        foundation::PaddingValues contentPadding = BottomAppBarDefaults::ContentPadding;
        std::optional<foundation::WindowInsets> windowInsets;
        std::shared_ptr<BottomAppBarScrollBehavior> scrollBehavior;
    };
    // Actions in a row at the start and an optional floating action button at the end.
    void BottomAppBar(std::function<void()> actions, const ui::Modifier& modifier, std::function<void()> floatingActionButton,
                      const BottomAppBarOptions& options = {});
    // Any content, in a row (80 dp tall).
    void BottomAppBar(const ui::Modifier& modifier, const BottomAppBarOptions& options, std::function<void()> content);

    struct FlexibleBottomAppBarOptions {
        std::optional<ui::Color> containerColor;
        std::optional<ui::Color> contentColor;
        foundation::PaddingValues contentPadding = BottomAppBarDefaults::FlexibleContentPadding;
        foundation::Arrangement horizontalArrangement = BottomAppBarDefaults::FlexibleHorizontalArrangement();
        ui::Dp expandedHeight = BottomAppBarDefaults::FlexibleBottomAppBarHeight;   // not positive / finite: the default
        std::optional<foundation::WindowInsets> windowInsets;
        std::shared_ptr<BottomAppBarScrollBehavior> scrollBehavior;
    };
    // A 64 dp bar whose content is spread by the arrangement.
    void FlexibleBottomAppBar(const ui::Modifier& modifier, const FlexibleBottomAppBarOptions& options, std::function<void()> content);

}
