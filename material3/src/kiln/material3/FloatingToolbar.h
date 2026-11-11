#pragma once

// FloatingToolbar.kt: horizontal and vertical floating toolbars, alone or beside a floating
// action button. Expanded, a toolbar shows its leading and trailing content (with a FAB, its
// whole content); collapsed, only the rest (or only the FAB, which grows). A scroll behaviour
// slides the toolbar off an edge of its parent as content scrolls, and dragging it does the
// same. Touch exploration keeps it expanded and in place; accessibility can still collapse it.

#include "kiln/animation/Decay.h"
#include "kiln/animation/Spec.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/material3/FloatingActionButton.h"
#include "kiln/runtime/State.h"
#include "kiln/ui/NestedScroll.h"

#include <cfloat>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    struct FloatingToolbarColors {
        ui::Color toolbarContainerColor, toolbarContentColor, fabContainerColor, fabContentColor;
        // Unspecified arguments keep this one's colours.
        FloatingToolbarColors Copy(ui::Color toolbarContainerColor = ui::Color::Unspecified(),
                                   ui::Color toolbarContentColor = ui::Color::Unspecified(),
                                   ui::Color fabContainerColor = ui::Color::Unspecified(),
                                   ui::Color fabContentColor = ui::Color::Unspecified()) const;
        bool operator==(const FloatingToolbarColors&) const = default;
    };

    // The edge a toolbar leaves through as content scrolls.
    enum class FloatingToolbarExitDirection : std::uint8_t { Bottom, Top, Start, End };
    enum class FloatingToolbarHorizontalFabPosition : std::uint8_t { Start, End };
    enum class FloatingToolbarVerticalFabPosition : std::uint8_t { Top, Bottom };

    // How far a scroll behaviour has moved a toolbar towards its exit edge (pixels, negative).
    // Reading them subscribes.
    class FloatingToolbarState {
    public:
        FloatingToolbarState(float initialOffsetLimit, float initialOffset, float initialContentOffset);
        // How far the toolbar can move (negative): to just past its parent's edge.
        float OffsetLimit() const { return m_OffsetLimit.Get(); }
        void SetOffsetLimit(float limit) { m_OffsetLimit.Set(limit); }
        // Between OffsetLimit and 0.
        float Offset() const { return m_Offset.Get(); }
        void SetOffset(float offset);
        // The content's total scroll since it was at its start.
        float ContentOffset() const { return m_ContentOffset.Get(); }
        void SetContentOffset(float offset) { m_ContentOffset.Set(offset); }

    private:
        rt::State<float> m_OffsetLimit;
        rt::State<float> m_Offset;
        rt::State<float> m_ContentOffset;
    };
    std::shared_ptr<FloatingToolbarState> RememberFloatingToolbarState(float initialOffsetLimit = -FLT_MAX, float initialOffset = 0,
                                                                       float initialContentOffset = 0, rt::Key key = {});

    // Connects a toolbar to the content's nested scroll (pass NestedScrollConnection() to
    // foundation::NestedScroll on a parent of the scrolling content, and the behaviour to the
    // toolbar).
    class FloatingToolbarScrollBehavior {
    public:
        virtual ~FloatingToolbarScrollBehavior() = default;
        virtual FloatingToolbarExitDirection ExitDirection() const = 0;
        virtual FloatingToolbarState& State() const = 0;
        virtual anim::SpecPtr SnapAnimationSpec() const = 0;
        virtual std::shared_ptr<const anim::DecaySpec> FlingAnimationSpec() const = 0;
        virtual std::shared_ptr<ui::NestedScrollConnection> NestedScrollConnection() const = 0;
        // floatingScrollBehaviorModifier: moves the toolbar by the state's offset, lets a drag
        // move it, and sets the limit from where it sits in its parent. The toolbars apply it;
        // call it during composition.
        virtual ui::Modifier FloatingScrollBehavior(ui::Modifier m) const = 0;
    };

    namespace FloatingToolbarDefaults {
        inline constexpr ui::Dp ContainerSize{ 64 };
        inline constexpr ui::Dp ContainerExpandedElevation{ 0 }, ContainerCollapsedElevation{ 0 };
        inline constexpr ui::Dp ContainerExpandedElevationWithFab{ 1 }, ContainerCollapsedElevationWithFab{ 0 };
        foundation::ShapePtr ContainerShape();
        inline constexpr foundation::PaddingValues ContentPadding{ ui::Dp(8), ui::Dp(8), ui::Dp(8), ui::Dp(8) };
        // The space to leave between a toolbar and the screen's edge.
        inline constexpr ui::Dp ScreenOffset{ 16 };
        // How far content scrolls before FloatingToolbarVerticalNestedScroll expands or collapses.
        inline constexpr ui::Dp ScrollDistanceThreshold{ 40 };
        // The FAB beside a toolbar: this size expanded, the larger one collapsed.
        inline constexpr ui::Dp FabSizeMin{ 56 }, FabSizeMax{ 80 };
        inline constexpr ui::Dp ToolbarToFabGap{ 8 };
        anim::SpecPtr animationSpec();

        // The toolbar slides off `exitDirection` as content scrolls up and back as it scrolls down.
        std::shared_ptr<FloatingToolbarScrollBehavior> exitAlwaysScrollBehavior(
            FloatingToolbarExitDirection exitDirection, std::shared_ptr<FloatingToolbarState> state = nullptr,
            std::optional<anim::SpecPtr> snapAnimationSpec = std::nullopt,
            std::optional<std::shared_ptr<const anim::DecaySpec>> flingAnimationSpec = std::nullopt);

        // How leading and trailing content appears and goes.
        foundation::EnterTransition horizontalEnterTransition(ui::AlignmentH expandFrom);
        foundation::EnterTransition verticalEnterTransition(ui::AlignmentV expandFrom);
        foundation::ExitTransition horizontalExitTransition(ui::AlignmentH shrinkTowards);
        foundation::ExitTransition verticalExitTransition(ui::AlignmentV shrinkTowards);

        FloatingToolbarColors standardFloatingToolbarColors();
        FloatingToolbarColors standardFloatingToolbarColors(ui::Color toolbarContainerColor, ui::Color toolbarContentColor = ui::Color::Unspecified(),
                                                            ui::Color fabContainerColor = ui::Color::Unspecified(),
                                                            ui::Color fabContentColor = ui::Color::Unspecified());
        FloatingToolbarColors vibrantFloatingToolbarColors();
        FloatingToolbarColors vibrantFloatingToolbarColors(ui::Color toolbarContainerColor, ui::Color toolbarContentColor = ui::Color::Unspecified(),
                                                           ui::Color fabContainerColor = ui::Color::Unspecified(),
                                                           ui::Color fabContentColor = ui::Color::Unspecified());

        // The FABs to put beside a toolbar: they fill the size the toolbar gives them. Unset
        // options: the colour set's FAB colours and the toolbar FAB's elevation.
        void VibrantFloatingActionButton(std::function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                         std::function<void()> content);
        void StandardFloatingActionButton(std::function<void()> onClick, const ui::Modifier& modifier, const FloatingActionButtonOptions& options,
                                          std::function<void()> content);

        // Expands a toolbar when content scrolls down by the threshold, collapses it when it
        // scrolls up by it (put it on a parent of the scrolling content; call during composition).
        ui::Modifier FloatingToolbarVerticalNestedScroll(ui::Modifier m, bool expanded, std::function<void()> onExpand,
                                                         std::function<void()> onCollapse,
                                                         ui::Dp expandScrollDistanceThreshold = ScrollDistanceThreshold,
                                                         ui::Dp collapseScrollDistanceThreshold = ScrollDistanceThreshold,
                                                         bool reverseLayout = false);
    }

    struct FloatingToolbarOptions {
        std::optional<FloatingToolbarColors> colors;   // unset: the standard colours
        foundation::PaddingValues contentPadding = FloatingToolbarDefaults::ContentPadding;
        std::shared_ptr<FloatingToolbarScrollBehavior> scrollBehavior;
        foundation::ShapePtr shape;                  // null: FloatingToolbarDefaults::ContainerShape
        std::function<void()> leadingContent;        // shown while expanded, before the content
        std::function<void()> trailingContent;       // shown while expanded, after it
        ui::Dp expandedShadowElevation = FloatingToolbarDefaults::ContainerExpandedElevation;
        ui::Dp collapsedShadowElevation = FloatingToolbarDefaults::ContainerCollapsedElevation;
    };
    // The content in a row; leading and trailing content only while expanded.
    void HorizontalFloatingToolbar(bool expanded, const ui::Modifier& modifier, const FloatingToolbarOptions& options,
                                   std::function<void()> content);
    // The content in a column.
    void VerticalFloatingToolbar(bool expanded, const ui::Modifier& modifier, const FloatingToolbarOptions& options,
                                 std::function<void()> content);

    struct FloatingToolbarWithFabOptions {
        std::optional<FloatingToolbarColors> colors;
        foundation::PaddingValues contentPadding = FloatingToolbarDefaults::ContentPadding;
        std::shared_ptr<FloatingToolbarScrollBehavior> scrollBehavior;
        foundation::ShapePtr shape;
        FloatingToolbarHorizontalFabPosition horizontalFabPosition = FloatingToolbarHorizontalFabPosition::End;   // horizontal toolbars
        FloatingToolbarVerticalFabPosition verticalFabPosition = FloatingToolbarVerticalFabPosition::Bottom;       // vertical toolbars
        anim::SpecPtr animationSpec;                 // null: FloatingToolbarDefaults::animationSpec
        ui::Dp expandedShadowElevation = FloatingToolbarDefaults::ContainerExpandedElevationWithFab;
        ui::Dp collapsedShadowElevation = FloatingToolbarDefaults::ContainerCollapsedElevationWithFab;
    };
    // A toolbar that unrolls from beside a FAB (typically StandardFloatingActionButton or
    // VibrantFloatingActionButton) while expanded; collapsed, only the FAB shows.
    void HorizontalFloatingToolbar(bool expanded, std::function<void()> floatingActionButton, const ui::Modifier& modifier,
                                   const FloatingToolbarWithFabOptions& options, std::function<void()> content);
    void VerticalFloatingToolbar(bool expanded, std::function<void()> floatingActionButton, const ui::Modifier& modifier,
                                 const FloatingToolbarWithFabOptions& options, std::function<void()> content);

}
