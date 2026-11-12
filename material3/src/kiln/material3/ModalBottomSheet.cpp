#include "kiln/material3/ModalBottomSheet.h"

#include "kiln/foundation/Back.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Scrim.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        struct DismissIfHidden {
            std::shared_ptr<SheetState> state;
            std::function<void()> onDismiss;
            ~DismissIfHidden() { if (onDismiss && !state->IsVisible()) onDismiss(); }
        };
        rt::Task<> HideThenDismiss(std::shared_ptr<SheetState> state, std::function<void()> onDismiss) {
            DismissIfHidden guard{ state, std::move(onDismiss) };
            co_await state->Hide();
        }
        // settleToDismiss's hide: reports the dismissal however it ends.
        rt::Task<> HideThenAlwaysDismiss(std::shared_ptr<SheetState> state, std::function<void()> onDismiss) {
            struct Always {
                std::function<void()> f;
                ~Always() { if (f) f(); }
            } always{ std::move(onDismiss) };
            co_await state->Hide();
        }
        rt::Task<> PartialExpandSheet(std::shared_ptr<SheetState> state) { co_await state->PartialExpand(); }
        rt::Task<> AnimateAlpha(std::shared_ptr<anim::Animatable> a, float target, anim::SpecPtr spec) {
            co_await a->AnimateTo(anim::Vec{ target, 0, 0, 0 }, std::move(spec));
        }
        // ComponentDialog.cancel() does nothing: Back that the sheet does not take is held.
        rt::Task<> HoldBack(BackProgress& events) {
            while (co_await events.Next()) {}
        }
    }

    std::shared_ptr<SheetState> RememberModalBottomSheetState(bool skipPartiallyExpanded, std::function<bool(SheetValue)> confirmValueChange, rt::Key key) {
        return internal::RememberSheetState(SheetValues{ true, !skipPartiallyExpanded }, std::move(confirmValueChange), SheetValue::Hidden,
                                            BottomSheetDefaults::PositionalThreshold, BottomSheetDefaults::VelocityThreshold, false, key);
    }

    namespace internal {
        WindowInsets SheetWindowInsets(std::shared_ptr<SheetState> state) {
            const void* identity = state.get();
            return WindowInsets::FromFunction(
                [state](const InsetsContext&, int side) {
                    if (side != 1) return 0;
                    const float offset = state->Anchored().Offset();
                    return std::isnan(offset) ? 0 : std::max(0, int(offset));
                },
                identity);
        }
    }

    void ModalBottomSheet(std::function<void()> onDismissRequest, const ui::Modifier& modifier, const ModalBottomSheetOptions& o,
                          std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        const auto state = o.sheetState ? o.sheetState : RememberBottomSheetState(SheetValue::Hidden);
        const auto scope = internal::RememberCoroutineScope();
        const ModalBottomSheetProperties properties = o.properties;
        const auto animateToDismiss = [state, scope, onDismissRequest] {
            if (state->Confirm(SheetValue::Hidden)) scope->Launch(HideThenDismiss(state, onDismissRequest));
        };
        const auto settleToDismiss = [state, scope, onDismissRequest] {
            if (state->CurrentValue() == SheetValue::Expanded && state->HasPartiallyExpandedState()) scope->Launch(PartialExpandSheet(state));
            else scope->Launch(HideThenAlwaysDismiss(state, onDismissRequest));
        };

        const Color container = o.containerColor.value_or(BottomSheetDefaults::ContainerColor());
        const Color contentColor = o.contentColor.value_or(ContentColorFor(container));
        const Color scrimColor = o.scrimColor.value_or(BottomSheetDefaults::ScrimColor());
        BottomSheetOptions sheet;
        sheet.state = state;
        sheet.onDismissRequest = onDismissRequest;
        sheet.maxWidth = o.sheetMaxWidth;
        sheet.gesturesEnabled = o.sheetGesturesEnabled;
        sheet.backHandlerEnabled = properties.shouldDismissOnBackPress;
        sheet.shape = o.shape;
        sheet.containerColor = container;
        sheet.contentColor = contentColor;
        sheet.tonalElevation = o.tonalElevation;
        sheet.dragHandle = o.dragHandle;
        sheet.contentWindowInsets = o.contentWindowInsets ? o.contentWindowInsets : [] { return BottomSheetDefaults::modalWindowInsets(); };

        // ModalBottomSheetDialog: edge to edge, full size, no dim of its own.
        DialogProperties dialog;
        dialog.dismissOnBackPress = properties.shouldDismissOnBackPress;
        dialog.dismissOnClickOutside = false;
        dialog.securePolicy = properties.securePolicy;
        dialog.usePlatformDefaultWidth = false;
        dialog.decorFitsSystemWindows = false;
        dialog.scrimAlpha = 0;
        // Color.isDark: not transparent and luminance at most 0.5.
        const bool darkContent = !(contentColor == Color::Transparent()) && Luminance(contentColor) <= 0.5f;
        const bool lightStatusBars = properties.isAppearanceLightStatusBars.value_or(darkContent);
        const bool lightNavigationBars = properties.isAppearanceLightNavigationBars.value_or(darkContent);
        Dialog(settleToDismiss, dialog, [](BackProgress& events) { return HoldBack(events); }, [=] {
            // updateParameters: the sheet window's bar icons follow its content.
            if (ui::Owner* window = rt::Composition::Current()->Read(ui::LocalOwner))
                rt::Composition::Current()->SideEffect([window, lightStatusBars, lightNavigationBars] { window->SetSystemBarsAppearance(lightStatusBars, lightNavigationBars); });
            ui::Modifier box = FillMax(ui::Modifier{}, Axis::Both);
            box = Semantics(std::move(box), [](ui::SemanticsConfiguration& s) { s.isTraversalGroup = true; });
            Box(box, ui::Alignment::TopStart(), false, [=] {
                auto& cc = *rt::Composition::Current();
                const WindowInsets sheetInsets = cc.Remember<WindowInsets>([&] { return internal::SheetWindowInsets(state); }, rt::Keys(state.get()));
                const auto alpha = cc.Remember<std::shared_ptr<anim::Animatable>>([] { return std::make_shared<anim::Animatable>(0.0f); });
                const anim::SpecPtr effects = Value(MotionSchemeKeyTokens::DefaultEffects);
                // isScrimVisible (derivedStateOf): read in its own scope, so a drag recomposes only this.
                cc.Child([state, alpha, effects] {
                    const bool visible = state->TargetValue() != SheetValue::Hidden;
                    rt::Composition::Current()->LaunchedEffect(rt::Keys(visible), [alpha, visible, effects]() -> rt::Task<> {
                        return AnimateAlpha(alpha, visible ? 1.0f : 0.0f, effects);
                    });
                });
                ScrimOptions scrim;
                scrim.onClick = properties.shouldDismissOnClickOutside ? std::function<void()>(animateToDismiss) : nullptr;
                scrim.alpha = [alpha] { return alpha->Float(); };
                scrim.color = scrimColor;
                Scrim(GetString(Strings::CloseSheet), {}, scrim);
                ui::Modifier m = AlignInBox(modifier, ui::Alignment::TopCenter());
                m = ConsumeWindowInsets(std::move(m), sheetInsets);
                BottomSheet(m, sheet, content);
            });
        });

        if (state->HasExpandedState()) {
            c.LaunchedEffect(rt::Keys(state.get()), [state]() -> rt::Task<> {
                const bool shouldShow = !ComposeMaterial3Flags::isModalBottomSheetStateRestorationFixEnabled ||
                                        state->Anchored().TargetValue() == SheetValue::Hidden;
                if (shouldShow) co_await state->Show();
            });
        }
    }

}
