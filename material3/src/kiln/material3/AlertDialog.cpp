#include "kiln/material3/AlertDialog.h"
#include <kiln/Function.h>
#include "kiln/material3/internal/AlertDialogInternal.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Flow.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Surface.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/PrecisionPointer.h"
#include "kiln/material3/tokens/DialogTokens.h"
#include "kiln/ui/Locals.h"

#include <algorithm>

namespace kiln::material3 {
    using namespace foundation;
    using ui::Color;
    using ui::Dp;
    namespace T = tokens::DialogTokens;

    namespace {
        constexpr Dp DialogMinWidth{ 280 }, DialogMaxWidth{ 560 }, ButtonsMainAxisSpacing{ 8 }, ButtonsCrossAxisSpacing{ 8 };
        Dp DialogPadding() { return internal::ShouldUsePrecisionPointerComponentSizing() ? Dp(20) : Dp(24); }
        Dp TextPadding() { return internal::ShouldUsePrecisionPointerComponentSizing() ? Dp(16) : Dp(24); }

        // ProvideContentColorTextStyle
        void ProvideColorAndStyle(Color color, const text::TextStyle& style, const kiln::Function<void()>& content) {
            rt::Composition::Current()->Provide(LocalContentColor, color, [=] { ProvideTextStyle(style, content); });
        }

        ui::LayoutDirection Flip(ui::LayoutDirection d) { return d == ui::LayoutDirection::Ltr ? ui::LayoutDirection::Rtl : ui::LayoutDirection::Ltr; }

        // AlertDialogFlowRow: the confirm button comes after the dismiss one in a row, before it
        // once they wrap into a column (the row runs in the flipped direction).
        void AlertDialogFlowRow(Dp mainAxisSpacing, Dp crossAxisSpacing, kiln::Function<void()> content) {
            auto& c = *rt::Composition::Current();
            const ui::LayoutDirection original = c.Read(ui::LocalLayoutDirection);
            c.Provide(ui::LocalLayoutDirection, Flip(original), [=] {
                FlowRow({}, Arrangement::SpacedBy(mainAxisSpacing), Arrangement::SpacedBy(crossAxisSpacing), ui::AlignmentV::Top(),
                        [original, content] { rt::Composition::Current()->Provide(ui::LocalLayoutDirection, original, content); });
            });
        }

        struct ContentParams {
            kiln::Function<void()> buttons, icon, title, text;
            ShapePtr shape;
            Color containerColor;
            Dp tonalElevation;
            Color buttonContentColor, iconContentColor, titleContentColor, textContentColor;
        };

        void AlertDialogContent(const ui::Modifier& modifier, const ContentParams& p) {
            SurfaceOptions so;
            so.shape = p.shape;
            so.color = p.containerColor;
            so.tonalElevation = p.tonalElevation;
            Surface(modifier, so, [p] {
                Column(Padding({}, DialogPadding()), Arrangement::Top(), ui::AlignmentH::Start(), [p] {
                    auto& c = *rt::Composition::Current();
                    if (p.icon) {
                        c.Provide(LocalContentColor, p.iconContentColor, [=] {
                            Box(AlignInColumn(Padding({}, PaddingValues{ Dp(0), Dp(0), Dp(0), Dp(16) }), ui::AlignmentH::CenterHorizontally()),
                                Alignment::TopStart(), false, p.icon);
                        });
                    }
                    if (p.title) {
                        text::TextStyle style = Value(T::HeadlineFont);
                        if (internal::ShouldUsePrecisionPointerComponentSizing()) {
                            style = MaterialTheme::typography().headlineSmall;
                            style.span.fontSize = text::Sp(20);
                            style.paragraph.lineHeight = text::Sp(26);
                        }
                        ProvideColorAndStyle(p.titleContentColor, style, [&] {
                            // Centred when there is an icon.
                            Box(AlignInColumn(Padding({}, PaddingValues{ Dp(0), Dp(0), Dp(0), Dp(16) }),
                                              p.icon ? ui::AlignmentH::CenterHorizontally() : ui::AlignmentH::Start()),
                                Alignment::TopStart(), false, p.title);
                        });
                    }
                    if (p.text) {
                        ProvideColorAndStyle(p.textContentColor, Value(T::SupportingTextFont), [&] {
                            Box(AlignInColumn(Padding(Weight({}, 1.0f, false), PaddingValues{ Dp(0), Dp(0), Dp(0), TextPadding() }), ui::AlignmentH::Start()),
                                Alignment::TopStart(), false, p.text);
                        });
                    }
                    Box(AlignInColumn({}, ui::AlignmentH::End()), Alignment::TopStart(), false,
                        [p] { ProvideColorAndStyle(p.buttonContentColor, Value(T::ActionLabelTextFont), p.buttons); });
                });
            });
        }
    }

    namespace AlertDialogDefaults {
        ShapePtr shape() { return Value(T::ContainerShape); }
        Color containerColor() { return Value(T::ContainerColor); }
        Color iconContentColor() { return Value(T::IconColor); }
        Color titleContentColor() { return Value(T::HeadlineColor); }
        Color textContentColor() { return Value(T::SupportingTextColor); }
    }

    void BasicAlertDialog(kiln::Function<void()> onDismissRequest, const ui::Modifier& modifier, const DialogProperties& properties,
                          kiln::Function<void()> content) {
        Dialog(std::move(onDismissRequest), properties, [modifier, content = std::move(content)] {
            const std::string pane = GetString(Strings::Dialog);
            Box(Semantics(SizeIn(modifier, DialogMinWidth, Dp::Unspecified(), DialogMaxWidth, Dp::Unspecified()),
                          [pane](ui::SemanticsConfiguration& c) { c.paneTitle = pane; }),
                Alignment::TopStart(), true, content);
        });
    }

    void AlertDialog(kiln::Function<void()> onDismissRequest, kiln::Function<void()> confirmButton, const ui::Modifier& modifier, const AlertDialogOptions& o) {
        ContentParams p;
        p.icon = o.icon;
        p.title = o.title;
        p.text = o.text;
        p.shape = o.shape ? o.shape : AlertDialogDefaults::shape();
        p.containerColor = o.containerColor.value_or(AlertDialogDefaults::containerColor());
        p.tonalElevation = o.tonalElevation;
        // TextButtons keep their own colours; other buttons take this one.
        p.buttonContentColor = Value(T::ActionLabelTextColor);
        p.iconContentColor = o.iconContentColor.value_or(AlertDialogDefaults::iconContentColor());
        p.titleContentColor = o.titleContentColor.value_or(AlertDialogDefaults::titleContentColor());
        p.textContentColor = o.textContentColor.value_or(AlertDialogDefaults::textContentColor());
        const Dp mics = rt::Composition::Current()->Read(LocalMinimumInteractiveComponentSize);
        const float fromMics = (mics.Specified() ? mics.value : 0.0f) - ButtonDefaults::MinHeight().value;
        const Dp crossSpacing(std::clamp(ButtonsCrossAxisSpacing.value - fromMics, 0.0f, ButtonsCrossAxisSpacing.value));
        const auto dismiss = o.dismissButton;
        p.buttons = [confirmButton, dismiss, crossSpacing] {
            AlertDialogFlowRow(ButtonsMainAxisSpacing, crossSpacing, [confirmButton, dismiss] {
                confirmButton();
                if (dismiss) dismiss();
            });
        };
        BasicAlertDialog(std::move(onDismissRequest), modifier, o.properties, [p] { AlertDialogContent({}, p); });
    }

    void internal::AlertDialogFlowRow(Dp mainAxisSpacing, Dp crossAxisSpacing, kiln::Function<void()> content) {
        kiln::material3::AlertDialogFlowRow(mainAxisSpacing, crossAxisSpacing, std::move(content));   // the file-local one
    }

}
