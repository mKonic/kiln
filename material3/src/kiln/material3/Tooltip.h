#pragma once

// Tooltip.kt and internal/BasicTooltip.kt: a TooltipBox shows a plain or rich tooltip in a popup
// next to its anchor on long press, mouse hover, keyboard focus or TooltipState::Show. The popup
// scales from 0.8 and fades in; a caret can point at the anchor.

#include "kiln/foundation/Layouts.h"
#include <kiln/Function.h>
#include "kiln/foundation/MutatorMutex.h"
#include "kiln/foundation/Popup.h"
#include "kiln/foundation/Shapes.h"
#include "kiln/foundation/Transition.h"
#include "kiln/runtime/Task.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::material3 {

    namespace BasicTooltipDefaults {
        // The mutex every tooltip shares by default: showing one hides the others.
        const std::shared_ptr<foundation::MutatorMutex>& GlobalMutatorMutex();
        inline constexpr std::int64_t TooltipDuration = 1500;
    }

    namespace internal { class TooltipCore; }

    // TooltipState: whether the tooltip shows. A handle: copies share one state.
    class TooltipState {
    public:
        explicit TooltipState(std::shared_ptr<internal::TooltipCore> core) : m_Core(std::move(core)) {}
        // The visibility the TooltipBox animates (true while shown).
        foundation::MutableTransitionState<bool>& Transition() const;
        bool IsVisible() const;   // observable
        // Persistent tooltips stay until dismissed; others hide after TooltipDuration unless
        // shown with UserInput priority (mouse hover).
        bool IsPersistent() const;
        // Shows the tooltip and suspends until it is dismissed, times out or a show of the same
        // or a higher priority on the same mutex takes over.
        rt::Task<> Show(foundation::MutatePriority priority = foundation::MutatePriority::Default) const;
        void Dismiss() const;
        // Called by the TooltipBox when it leaves: ends a running show.
        void OnDispose() const;
        bool operator==(const TooltipState& o) const { return m_Core == o.m_Core; }

    private:
        std::shared_ptr<internal::TooltipCore> m_Core;
    };

    // TooltipState(...) / rememberTooltipState(...).
    TooltipState MakeTooltipState(bool initialIsVisible = false, bool isPersistent = true,
                                  std::shared_ptr<foundation::MutatorMutex> mutatorMutex = BasicTooltipDefaults::GlobalMutatorMutex());
    TooltipState RememberTooltipState(bool initialIsVisible = false, bool isPersistent = false,
                                      std::shared_ptr<foundation::MutatorMutex> mutatorMutex = BasicTooltipDefaults::GlobalMutatorMutex(), rt::Key key = {});

    enum class TooltipAnchorPosition : std::uint8_t { Above = 1, Below, Left, Right, Start, End };

    // TooltipScope: what tooltip content may ask of its box.
    class TooltipScope {
    public:
        virtual ~TooltipScope() = default;
        // The anchor's coordinates (nullopt before it was placed).
        virtual std::optional<foundation::LayoutCoordinates> ObtainAnchorBounds() const = 0;
        virtual std::shared_ptr<const ui::PopupPositionProvider> ObtainPositionProvider() const = 0;
    };

    struct RichTooltipColors {
        ui::Color containerColor, contentColor, titleContentColor, actionContentColor;
        // Unspecified arguments keep this one's colours.
        RichTooltipColors Copy(ui::Color containerColor = ui::Color::Unspecified(), ui::Color contentColor = ui::Color::Unspecified(),
                               ui::Color titleContentColor = ui::Color::Unspecified(),
                               ui::Color actionContentColor = ui::Color::Unspecified()) const;
        bool operator==(const RichTooltipColors&) const = default;
    };

    // The default caret: a triangle `caretSize` wide and tall with its tip at the origin's
    // height below (the tooltip turns it toward the anchor).
    class DefaultTooltipCaretShape final : public gfx::Shape {
    public:
        explicit DefaultTooltipCaretShape(ui::DpSize caretSize);
        ui::DpSize CaretSize() const { return m_CaretSize; }
        gfx::Outline CreateOutline(ui::Size size, ui::LayoutDirection dir, const ui::Density& density) const override;

    private:
        ui::DpSize m_CaretSize;
    };

    namespace TooltipDefaults {
        foundation::ShapePtr plainTooltipContainerShape();
        ui::Color plainTooltipContainerColor();
        ui::Color plainTooltipContentColor();
        foundation::ShapePtr richTooltipContainerShape();
        inline constexpr ui::DpSize caretSize{ ui::Dp(16), ui::Dp(8) };
        inline constexpr ui::Dp plainTooltipMaxWidth{ 200 };
        inline constexpr ui::Dp richTooltipMaxWidth{ 320 };
        foundation::ShapePtr caretShape(ui::DpSize caretSize = TooltipDefaults::caretSize);
        RichTooltipColors richTooltipColors();
        RichTooltipColors richTooltipColors(ui::Color containerColor, ui::Color contentColor = ui::Color::Unspecified(),
                                            ui::Color titleContentColor = ui::Color::Unspecified(),
                                            ui::Color actionContentColor = ui::Color::Unspecified());
        inline constexpr ui::Dp SpacingBetweenTooltipAndAnchor{ 4 };
        // Above the anchor (below when it does not fit), with `positioning` choosing the side.
        std::shared_ptr<const ui::PopupPositionProvider> rememberTooltipPositionProvider(
            TooltipAnchorPosition positioning, ui::Dp spacingBetweenTooltipAndAnchor = SpacingBetweenTooltipAndAnchor);
        // Deprecated forms: centred above (or below) the anchor; the rich one start-aligned.
        std::shared_ptr<const ui::PopupPositionProvider> rememberTooltipPositionProvider(ui::Dp spacingBetweenTooltipAndAnchor = SpacingBetweenTooltipAndAnchor);
        std::shared_ptr<const ui::PopupPositionProvider> rememberPlainTooltipPositionProvider(ui::Dp spacingBetweenTooltipAndAnchor = SpacingBetweenTooltipAndAnchor);
        std::shared_ptr<const ui::PopupPositionProvider> rememberRichTooltipPositionProvider(ui::Dp spacingBetweenTooltipAndAnchor = SpacingBetweenTooltipAndAnchor);
    }

    struct TooltipBoxOptions {
        kiln::Function<void()> onDismissRequest;   // null: a click outside or Back dismisses
        bool focusable = false;
        bool enableUserInput = true;               // long press, hover and focus show it
        bool hasAction = false;                    // keyboard and accessibility users can reach it
    };
    void TooltipBox(std::shared_ptr<const ui::PopupPositionProvider> positionProvider, kiln::Function<void(const TooltipScope&)> tooltip,
                    const TooltipState& state, const ui::Modifier& modifier, const TooltipBoxOptions& options, kiln::Function<void()> content);

    struct PlainTooltipOptions {
        foundation::ShapePtr caretShape;           // null: no caret
        ui::Dp maxWidth = TooltipDefaults::plainTooltipMaxWidth;
        foundation::ShapePtr shape;                // null: plainTooltipContainerShape
        std::optional<ui::Color> contentColor, containerColor;
        ui::Dp tonalElevation{ 0 }, shadowElevation{ 0 };
    };
    void PlainTooltip(const TooltipScope& scope, const ui::Modifier& modifier, const PlainTooltipOptions& options, kiln::Function<void()> content);

    struct RichTooltipOptions {
        kiln::Function<void()> title, action;
        foundation::ShapePtr caretShape;
        ui::Dp maxWidth = TooltipDefaults::richTooltipMaxWidth;
        foundation::ShapePtr shape;                // null: richTooltipContainerShape
        std::optional<RichTooltipColors> colors;
        ui::Dp tonalElevation{ 0 };
        std::optional<ui::Dp> shadowElevation;     // unset: RichTooltipTokens.ContainerElevation
    };
    void RichTooltip(const TooltipScope& scope, const ui::Modifier& modifier, const RichTooltipOptions& options, kiln::Function<void()> text);

    namespace internal {
        // BasicTooltipBox: the popup, anchor gestures, semantics and keys without the animation.
        void BasicTooltipBox(std::shared_ptr<const ui::PopupPositionProvider> positionProvider, kiln::Function<void()> tooltip,
                             const TooltipState& state, const ui::Modifier& modifier, const TooltipBoxOptions& options,
                             kiln::Function<void()> content);
        // rememberBasicTooltipState: visibility without a transition (Label's tooltips).
        TooltipState RememberBasicTooltipState(bool initialIsVisible = false, bool isPersistent = true,
                                               std::shared_ptr<foundation::MutatorMutex> mutatorMutex = BasicTooltipDefaults::GlobalMutatorMutex(), rt::Key key = {});
        TooltipState MakeBasicTooltipState(bool initialIsVisible = false, bool isPersistent = true,
                                           std::shared_ptr<foundation::MutatorMutex> mutatorMutex = BasicTooltipDefaults::GlobalMutatorMutex());
        // LabelStateImpl: a persistent label's state, always visible (show and dismiss do nothing).
        TooltipState MakeLabelState();
        // PlainTooltipInternal: a plain tooltip that announces `tooltipText` as its pane title.
        void PlainTooltipInternal(const TooltipScope& scope, const std::string& tooltipText, const ui::Modifier& modifier,
                                  const PlainTooltipOptions& options, kiln::Function<void()> content);
        // caretX: where along the tooltip's width the caret sits for an above / below tooltip.
        float CaretX(float tooltipWidth, ui::Rect anchorBounds, float tooltipLeft);
    }

}
