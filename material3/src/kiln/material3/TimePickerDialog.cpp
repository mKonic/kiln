#include "kiln/material3/TimePickerDialog.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/IconButton.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/Tooltip.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/material3/tokens/DialogTokens.h"
#include "kiln/material3/tokens/ShapeKeyTokens.h"

#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Dp;

    namespace {
        constexpr Dp ClockDialMinContainerSize{ 200 };

        void ToggleWithTooltip(std::function<void()> onClick, const gfx::vector::ImageVectorPtr& icon, std::string description,
                               const ui::Modifier& modifier) {
            const auto provider = TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above);
            const TooltipState state = RememberTooltipState();
            TooltipBox(provider, [description](const TooltipScope& scope) {
                internal::PlainTooltipInternal(scope, description, {}, {}, [description] { Text(description); });
            }, state, {}, {}, [=] {
                IconButton(onClick, modifier, {}, [icon, description] { Icon(icon, description, {}, MaterialTheme::colorScheme().onSurfaceVariant); });
            });
        }

        // The content may be landscape (wider than tall, and at least a dial tall).
        bool IsLandscape(const ui::Placeable& content, const ui::MeasureScope& s) {
            return content.Width() > content.Height() && float(content.Height()) >= std::trunc(s.ToPx(ClockDialMinContainerSize));
        }

        ui::Placeable* Child(std::span<ui::Measurable* const> children, const char* id, const ui::Constraints& c) {
            for (auto* m : children) {
                const std::string* tag = LayoutIdOf(*m);
                if (tag && *tag == id) return &m->Measure(c);
            }
            return nullptr;
        }

        // The buttons: the mode toggle at the start, dismiss and confirm at the end.
        std::function<void()> Actions(std::function<void()> modeToggleButton, std::function<void()> dismissButton, std::function<void()> confirmButton) {
            return [=] {
                Row(FillMax({}, Axis::Horizontal), Arrangement::SpacedBy(Dp(8)), ui::AlignmentV::Top(), [=] {
                    if (modeToggleButton) modeToggleButton();
                    Spacer(Weight({}, 1));
                    if (dismissButton) dismissButton();
                    confirmButton();
                });
            };
        }

        void TimePickerCustomLayout(std::function<void()> title, std::function<void()> actions, std::function<void()> content) {
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                const int contentPadding = s.RoundToPx(Dp(24));
                const int landMaxDialogHeight = s.RoundToPx(Dp(384)), landTitleTopPadding = s.RoundToPx(Dp(24)), landContentTopPadding = s.RoundToPx(Dp(16));
                const int landContentActionsPadding = s.RoundToPx(Dp(4)), landActionsBottomPadding = s.RoundToPx(Dp(8));
                const int portTitleTopPadding = s.RoundToPx(Dp(24)), portActionsBottomPadding = s.RoundToPx(Dp(24));
                ui::Placeable* content = Child(children, "timePickerContent", { c.minWidth, c.maxWidth, 0, c.maxHeight });
                const bool landscape = IsLandscape(*content, s);
                const int width = content->Width() + contentPadding * 2;
                ui::Placeable* actions = Child(children, "actions", { 0, content->Width(), 0, c.maxHeight });
                ui::Placeable* title = Child(children, "title", { 0, content->Width(), 0, c.maxHeight });
                int height;
                if (landscape) {
                    const int total = content->Height() + actions->Height() + landActionsBottomPadding + landContentTopPadding + landContentActionsPadding;
                    height = c.HasBoundedHeight() ? c.maxHeight : total;
                } else {
                    height = portTitleTopPadding + title->Height() + content->Height() + actions->Height() + portActionsBottomPadding;
                }
                const int bottomAdjust = height >= landMaxDialogHeight ? s.RoundToPx(Dp(16)) : 0;
                return s.Layout(width, height, [=](ui::PlacementScope& p) {
                    if (landscape) {
                        const int contentHeight = landContentTopPadding + content->Height() + landContentActionsPadding + actions->Height() + landActionsBottomPadding;
                        const int remaining = height - contentHeight;
                        p.Place(*title, landTitleTopPadding, landTitleTopPadding);
                        const int contentY = landContentTopPadding + remaining / 2;
                        p.Place(*content, contentPadding, contentY);
                        p.Place(*actions, contentPadding, contentY + content->Height() + landContentActionsPadding - bottomAdjust + remaining / 2);
                    } else {
                        p.Place(*title, landTitleTopPadding, portTitleTopPadding);
                        const int contentY = portTitleTopPadding + title->Height();
                        p.Place(*content, (width - content->Width()) / 2, contentY);
                        p.Place(*actions, (width - actions->Width()) / 2, contentY + content->Height());
                    }
                });
            });
            Layout({}, policy, [=] {
                Box(LayoutId({}, "title"), ui::Alignment::TopStart(), false, title);
                Box(LayoutId({}, "actions"), ui::Alignment::TopStart(), false, actions);
                Column(LayoutId({}, "timePickerContent"), content);
            });
        }

        void VibrantTimePickerCustomLayout(std::function<void()> actions, std::function<void()> content) {
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                const int landContentTopPadding = s.RoundToPx(Dp(12)), landContentActionsPadding = s.RoundToPx(Dp(24)), landActionsBottomPadding = s.RoundToPx(Dp(12));
                const int portTitleTopPadding = s.RoundToPx(Dp(12)), portActionsBottomPadding = s.RoundToPx(Dp(12)), portContentActionsPadding = s.RoundToPx(Dp(12));
                ui::Placeable* content = Child(children, "timePickerContent", { c.minWidth, c.maxWidth, 0, c.maxHeight });
                const bool landscape = IsLandscape(*content, s);
                const int contentPadding = s.RoundToPx(Dp(landscape ? 24 : 12));
                const int width = content->Width() + contentPadding * 2;
                ui::Placeable* actions = Child(children, "actions", { 0, content->Width(), 0, c.maxHeight });
                int height;
                if (landscape) {
                    const int total = content->Height() + actions->Height() + landActionsBottomPadding + landContentTopPadding + landContentActionsPadding;
                    height = c.HasBoundedHeight() ? c.maxHeight : total;
                } else {
                    height = portTitleTopPadding + content->Height() + actions->Height() + portActionsBottomPadding + portContentActionsPadding;
                }
                return s.Layout(width, height, [=](ui::PlacementScope& p) {
                    if (landscape) {
                        const int contentHeight = landContentTopPadding + content->Height() + landContentActionsPadding + actions->Height() + landActionsBottomPadding;
                        const int remaining = height - contentHeight;
                        const int contentY = landContentTopPadding + remaining / 2;
                        p.Place(*content, contentPadding, contentY);
                        p.Place(*actions, contentPadding, contentY + content->Height() + landContentActionsPadding);
                    } else {
                        const int contentY = portTitleTopPadding;
                        p.Place(*content, (width - content->Width()) / 2, contentY);
                        p.Place(*actions, (width - actions->Width()) / 2, contentY + content->Height() + portContentActionsPadding);
                    }
                });
            });
            Layout({}, policy, [=] {
                Box(LayoutId({}, "actions"), ui::Alignment::TopStart(), false, actions);
                Column(LayoutId({}, "timePickerContent"), content);
            });
        }
    }

    namespace TimePickerDialogDefaults {
        ui::Color containerColor() { return Value(DialogTokens::ContainerColor); }
        ShapePtr shape() { return Value(DialogTokens::ContainerShape); }
        ui::Color vibrantContainerColor() { return MaterialTheme::colorScheme().surfaceContainer; }
        ShapePtr vibrantShape() { return Value(ShapeKeyTokens::CornerExtraLarge); }

        void DisplayModeToggle(std::function<void()> onDisplayModeChange, TimePickerDisplayMode displayMode, const ui::Modifier& modifier) {
            const bool picker = displayMode == TimePickerDisplayMode::Picker;
            ToggleWithTooltip(std::move(onDisplayModeChange), picker ? internal::Icons::Filled::Keyboard() : internal::Icons::Filled::Schedule(),
                              GetString(picker ? Strings::TimePickerToggleKeyboard : Strings::TimePickerToggleTouch), modifier);
        }

        void ScrollDisplayModeToggle(std::function<void()> onDisplayModeChange, TimePickerDisplayMode displayMode, const ui::Modifier& modifier) {
            const bool scroll = displayMode == TimePickerDisplayMode::Scroll;
            ToggleWithTooltip(std::move(onDisplayModeChange), scroll ? internal::Icons::Filled::Keyboard() : internal::Icons::Filled::SwipeVertical(),
                              GetString(scroll ? Strings::TimePickerToggleKeyboard : Strings::TimePickerToggleScroll), modifier);
        }

        void Title(TimePickerDisplayMode displayMode, const ui::Modifier& modifier) {
            const Strings id = displayMode == TimePickerDisplayMode::Input    ? Strings::TimeInputDialogTitle
                               : displayMode == TimePickerDisplayMode::Scroll ? Strings::TimeScrollDialogTitle
                                                                              : Strings::TimePickerDialogTitle;
            TextOptions o;
            o.style = MaterialTheme::typography().labelMedium;
            Text(GetString(id), Padding(modifier, { Dp(0), Dp(0), Dp(0), Dp(20) }), o);
        }
    }

    void TimePickerDialog(std::function<void()> onDismissRequest, std::function<void()> confirmButton, std::function<void()> title,
                          const ui::Modifier& modifier, const TimePickerDialogOptions& o, std::function<void()> content) {
        const ShapePtr shape = o.shape ? o.shape : TimePickerDialogDefaults::shape();
        const ui::Color container = o.containerColor.value_or(TimePickerDialogDefaults::containerColor());
        auto actions = Actions(o.modeToggleButton, o.dismissButton, std::move(confirmButton));
        Dialog(std::move(onDismissRequest), o.properties, [=] {
            SurfaceOptions so;
            so.shape = shape;
            so.tonalElevation = DialogTokens::ContainerElevation;
            so.color = container;
            Surface(modifier, so, [=] { TimePickerCustomLayout(title, actions, content); });
        });
    }

    void VibrantTimePickerDialog(std::function<void()> onDismissRequest, std::function<void()> confirmButton, const ui::Modifier& modifier,
                                 const TimePickerDialogOptions& o, std::function<void()> content) {
        const ShapePtr shape = o.shape ? o.shape : TimePickerDialogDefaults::vibrantShape();
        const ui::Color container = o.containerColor.value_or(TimePickerDialogDefaults::vibrantContainerColor());
        auto actions = Actions(o.modeToggleButton, o.dismissButton, std::move(confirmButton));
        Dialog(std::move(onDismissRequest), o.properties, [=] {
            SurfaceOptions so;
            so.shape = shape;
            so.tonalElevation = DialogTokens::ContainerElevation;
            so.color = container;
            Surface(modifier, so, [=] { VibrantTimePickerCustomLayout(actions, content); });
        });
    }

}
