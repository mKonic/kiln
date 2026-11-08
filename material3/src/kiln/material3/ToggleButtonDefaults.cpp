#include "kiln/material3/ToggleButton.h"

#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/ButtonXSmallTokens.h"
#include "kiln/material3/tokens/ButtonSmallTokens.h"
#include "kiln/material3/tokens/ButtonMediumTokens.h"
#include "kiln/material3/tokens/ButtonLargeTokens.h"
#include "kiln/material3/tokens/ButtonXLargeTokens.h"
#include "kiln/material3/tokens/ElevatedButtonTokens.h"
#include "kiln/material3/tokens/FilledButtonTokens.h"
#include "kiln/material3/tokens/TonalButtonTokens.h"
#include "kiln/material3/tokens/OutlinedButtonTokens.h"

namespace kiln::material3 {

    using namespace tokens;
    const ToggleButtonSize ToggleButtonSize::ExtraSmall{ ButtonXSmallTokens::ContainerHeight }, ToggleButtonSize::Small{ ButtonSmallTokens::ContainerHeight },
        ToggleButtonSize::Medium{ ButtonMediumTokens::ContainerHeight }, ToggleButtonSize::Large{ ButtonLargeTokens::ContainerHeight },
        ToggleButtonSize::ExtraLarge{ ButtonXLargeTokens::ContainerHeight };

    namespace ToggleButtonDefaults {
        const ui::Dp MinHeight = ButtonSmallTokens::ContainerHeight, IconSize = ButtonSmallTokens::IconSize, IconSpacing = ButtonSmallTokens::IconLabelSpace;
        foundation::ShapePtr shape() { return Value(ButtonSmallTokens::ContainerShapeRound); }
        foundation::ShapePtr pressedShape() { return Value(ButtonSmallTokens::PressedContainerShape); }
        foundation::ShapePtr checkedShape() { return Value(ButtonSmallTokens::SelectedContainerShapeSquare); }
        ToggleButtonElevation elevation() { return ButtonDefaults::buttonElevation(); }
        foundation::PaddingValues contentPaddingFor(ui::Dp h, bool start, bool end) {
            // Toggle buttons keep the 40 dp small-height boundary even with precision sizing.
            if (h.value < MinHeight.value) return ButtonDefaults::ExtraSmallContentPadding();
            return ButtonDefaults::contentPaddingFor(h, start, end);
        }
        ToggleButtonShapes shapesFor(ui::Dp h) {
            const auto base = ButtonDefaults::shapesFor(h);
            const auto small = ButtonDefaults::MinHeight();
            foundation::ShapePtr checked;
            if (h.value <= (ButtonDefaults::ExtraSmallContainerHeight.value + small.value) / 2)
                checked = Value(ButtonXSmallTokens::ContainerShapeSquare);
            else if (h.value <= (small.value + ButtonDefaults::MediumContainerHeight.value) / 2)
                checked = checkedShape();
            else if (h.value <= (ButtonDefaults::MediumContainerHeight.value + ButtonDefaults::LargeContainerHeight.value) / 2)
                checked = Value(ButtonMediumTokens::ContainerShapeSquare);
            else if (h.value <= (ButtonDefaults::LargeContainerHeight.value + ButtonDefaults::ExtraLargeContainerHeight.value) / 2)
                checked = Value(ButtonLargeTokens::ContainerShapeSquare);
            else checked = Value(ButtonXLargeTokens::ContainerShapeSquare);
            return { base.shape, base.pressedShape, checked };
        }
        ToggleButtonColors colors() {
            return { Value(FilledButtonTokens::UnselectedContainerColor), Value(FilledButtonTokens::UnselectedPressedLabelTextColor),
                     Value(FilledButtonTokens::DisabledContainerColor).WithAlpha(FilledButtonTokens::DisabledContainerOpacity),
                     Value(FilledButtonTokens::DisabledLabelTextColor).WithAlpha(FilledButtonTokens::DisabledLabelTextOpacity),
                     Value(FilledButtonTokens::SelectedContainerColor), Value(FilledButtonTokens::SelectedPressedLabelTextColor) };
        }
    }
    namespace ElevatedToggleButtonDefaults {
        ToggleButtonElevation elevation() { return ButtonDefaults::elevatedButtonElevation(); }
        ToggleButtonColors colors() {
            return { Value(ElevatedButtonTokens::UnselectedContainerColor), Value(ElevatedButtonTokens::UnselectedPressedLabelTextColor),
                     Value(ElevatedButtonTokens::DisabledContainerColor).WithAlpha(ElevatedButtonTokens::DisabledContainerOpacity),
                     Value(ElevatedButtonTokens::DisabledLabelTextColor).WithAlpha(ElevatedButtonTokens::DisabledLabelTextOpacity),
                     Value(ElevatedButtonTokens::SelectedContainerColor), Value(ElevatedButtonTokens::SelectedPressedLabelTextColor) };
        }
    }
    namespace FilledTonalToggleButtonDefaults {
        ToggleButtonElevation elevation() { return ButtonDefaults::filledTonalButtonElevation(); }
        ToggleButtonColors colors() {
            return { Value(TonalButtonTokens::UnselectedContainerColor), Value(TonalButtonTokens::UnselectedLabelTextColor),
                     Value(TonalButtonTokens::DisabledContainerColor).WithAlpha(TonalButtonTokens::DisabledContainerOpacity),
                     Value(TonalButtonTokens::DisabledLabelTextColor).WithAlpha(TonalButtonTokens::DisabledLabelTextOpacity),
                     Value(TonalButtonTokens::SelectedContainerColor), Value(TonalButtonTokens::SelectedLabelTextColor) };
        }
    }
    namespace OutlinedToggleButtonDefaults {
        ToggleButtonColors colors() {
            return { ui::Color::Transparent(), Value(OutlinedButtonTokens::UnselectedLabelTextColor),
                     Value(OutlinedButtonTokens::DisabledOutlineColor).WithAlpha(OutlinedButtonTokens::DisabledContainerOpacity),
                     Value(OutlinedButtonTokens::DisabledLabelTextColor).WithAlpha(OutlinedButtonTokens::DisabledLabelTextOpacity),
                     Value(OutlinedButtonTokens::SelectedContainerColor), Value(OutlinedButtonTokens::SelectedLabelTextColor) };
        }
        std::optional<foundation::BorderStroke> border(bool enabled, bool checked) {
            return checked ? std::nullopt : std::optional(ButtonDefaults::outlinedButtonBorder(enabled));
        }
    }

}
