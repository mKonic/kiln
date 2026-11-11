#include "kiln/material3/ExposedDropdownMenu.h"

#include "kiln/foundation/Back.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Insets.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/Transition.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Menu.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/TextFieldDefaults.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/Helpers.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/material3/tokens/FilledAutocompleteTokens.h"
#include "kiln/material3/tokens/OutlinedAutocompleteTokens.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {
    using namespace foundation;
    using ui::Color;
    using ui::Dp;
    using ui::IntRect;
    using ui::IntSize;
    using AnchorType = ExposedDropdownMenuAnchorType;

    namespace internal {
        struct ExposedDropdownMenuBoxState {
            bool expanded = false;
            std::function<void(bool)> onExpandedChange;
            rt::State<AnchorType> anchorType{ AnchorType::PrimaryNotEditable };
            rt::State<bool> alwaysFocusable{ false };
            rt::State<int> anchorWidth{ 0 }, menuMaxHeight{ 0 };
            ui::FocusRequester focusRequester;
            ui::Owner* owner = nullptr;
            std::string expandedDescription, collapsedDescription, toggleDescription;
        };

        int ExposedDropdownMenuMaxHeight(IntRect windowBounds, std::optional<ui::Rect> anchor, int verticalMargin) {
            if (!anchor) return 0;
            const float top = float(windowBounds.top + verticalMargin), bottom = float(windowBounds.bottom - verticalMargin);
            int available;
            if (anchor->top > float(windowBounds.bottom) || anchor->bottom < float(windowBounds.top)) {
                available = int(bottom - top);
            } else {
                available = int(std::floor(std::max(anchor->top - top, bottom - anchor->bottom) + 0.5f));
            }
            return std::max(available, 0);
        }
    }

    namespace {
        // hasGreaterOrEqualPriorityThan
        bool HasPriority(AnchorType a, AnchorType b) {
            return a == AnchorType::SecondaryEditable ? b == AnchorType::SecondaryEditable : true;
        }

        // Android key codes.
        constexpr int kEnter = 66, kNumPadEnter = 160, kDpadCenter = 23, kSpace = 62, kTab = 61, kDpadDown = 20, kDpadUp = 19, kNumPad2 = 146,
                      kNumPad8 = 152;
        bool IsEnterMinusSpacebar(const ui::KeyEvent& e) { return e.keyCode == kDpadCenter || e.keyCode == kEnter || e.keyCode == kNumPadEnter; }
        bool IsClick(const ui::KeyEvent& e) { return e.type == ui::KeyEventType::KeyUp && (IsEnterMinusSpacebar(e) || e.keyCode == kSpace); }

        // The visible window: the window less the keyboard (getWindowVisibleDisplayFrame).
        IntRect VisibleWindowBounds(ui::Owner* owner, const ui::Density& density) {
            if (!owner) return {};
            const IntSize w = owner->WindowSize();
            const InsetsContext context{ &owner->Insets(), density, ui::LayoutDirection::Ltr };
            const int ime = WindowInsets::Ime().Bottom(context);
            return IntRect{ 0, 0, w.width, std::max(0, w.height - ime) };
        }

        // ExposedDropdownMenuAnchorNode: the anchor's type, recorded when it attaches.
        struct AnchorParams {
            std::shared_ptr<internal::ExposedDropdownMenuBoxState> state;
            AnchorType type;
            bool operator==(const AnchorParams&) const = default;
        };
        struct AnchorNode final : ui::ModifierNode {
            AnchorParams params;
            explicit AnchorNode(AnchorParams p) : params(std::move(p)) {}
            void OnAttach() override {
                auto& s = *params.state;
                rt::Observation::Untracked([&] {
                    if (HasPriority(params.type, s.anchorType.Peek())) s.anchorType.Set(params.type);
                });
            }
            void OnParamsChanged() {}
        };

        rt::Task<> ToggleGestures(PointerInputScope& scope, std::shared_ptr<internal::ExposedDropdownMenuBoxState> state, AnchorType type) {
            for (;;) {
                auto down = co_await AwaitFirstDown(scope, true, ui::PointerEventPass::Initial);
                if (!down) continue;
                // A trailing icon button keeps the press from the field it sits in.
                if (type == AnchorType::SecondaryEditable) down->Consume();
                auto up = co_await WaitForUpOrCancellation(scope, ui::PointerEventPass::Initial);
                if (up) {
                    state->anchorType.Set(type);
                    if (state->onExpandedChange) state->onExpandedChange(!state->expanded);
                }
            }
        }

        // Modifier.expandable
        Modifier Expandable(Modifier m, std::shared_ptr<internal::ExposedDropdownMenuBoxState> state, AnchorType type) {
            const auto toggle = [state, type] {
                state->anchorType.Set(type);
                if (state->onExpandedChange) state->onExpandedChange(!state->expanded);
            };
            m = PointerInput(std::move(m), rt::Keys(reinterpret_cast<std::uintptr_t>(state.get()), int(type), 7301),
                             [state, type](PointerInputScope& s) { return ToggleGestures(s, state, type); });
            m = OnKeyEvent(std::move(m), {}, [state, type, toggle](const ui::KeyEvent& e) {
                if (IsClick(e)) {
                    if (type != AnchorType::PrimaryEditable) {
                        toggle();
                    } else if (IsEnterMinusSpacebar(e)) {
                        toggle();
                        return true;
                    }
                }
                if (type == AnchorType::PrimaryEditable && state->expanded) {
                    if (e.keyCode == kTab || e.keyCode == kDpadDown || e.keyCode == kNumPad2 || e.keyCode == kDpadUp || e.keyCode == kNumPad8) {
                        state->alwaysFocusable.Set(true);
                        return true;
                    }
                }
                state->alwaysFocusable.Set(false);
                return false;
            });
            const bool expanded = state->expanded;
            return Semantics(std::move(m), [state, type, toggle, expanded](ui::SemanticsConfiguration& c) {
                if (type == AnchorType::SecondaryEditable) {
                    c.role = ui::Role::Button;
                    c.stateDescription = expanded ? state->expandedDescription : state->collapsedDescription;
                    c.contentDescription = { state->toggleDescription };
                } else {
                    c.role = ui::Role::DropdownList;
                }
                c.onClick = [state, type, toggle] {
                    if (type == AnchorType::PrimaryEditable) state->focusRequester.RequestFocus();
                    toggle();
                    if (type == AnchorType::PrimaryEditable && state->owner) state->owner->ShowSoftKeyboard();
                    return true;
                };
            });
        }

        // ExposedDropdownMenuPositionProvider
        class PositionProvider final : public ui::PopupPositionProvider {
        public:
            PositionProvider(ui::Density density, int topWindowInsets, const rt::State<int>* keyboardSignal)
                : m_TopInsets(topWindowInsets), m_KeyboardSignal(keyboardSignal), m_VerticalMargin(density.RoundToPx(internal::MenuVerticalMargin)) {}
            ui::IntOffset CalculatePosition(IntRect anchor, IntSize window, ui::LayoutDirection dir, IntSize menu) const override {
                if (m_KeyboardSignal) (void)m_KeyboardSignal->Get();
                const IntSize w{ window.width, window.height + m_TopInsets };
                namespace P = internal::MenuPosition;
                const int centerX = (anchor.left + anchor.right) / 2, centerY = (anchor.top + anchor.bottom) / 2;
                const P::Horizontal* xs[] = { &P::startToAnchorStart, &P::endToAnchorEnd, centerX < w.width / 2 ? &P::leftToWindowLeft : &P::rightToWindowRight };
                int x = 0;
                for (int i = 0; i < 3; ++i) {
                    const int c = (*xs[i])(anchor, w, menu.width, dir);
                    if (i == 2 || (c >= 0 && c + menu.width <= w.width)) { x = c; break; }
                }
                const P::Vertical* ys[] = { &P::topToAnchorBottom, &P::bottomToAnchorTop, centerY < w.height / 2 ? &P::topToWindowTop : &P::bottomToWindowBottom };
                int y = 0;
                for (int i = 0; i < 3; ++i) {
                    int c = (*ys[i])(anchor, w, menu.height);
                    if (i == 2) {
                        c = menu.height >= w.height - 2 * m_VerticalMargin ? ui::AlignmentV::CenterVertically().Align(menu.height, w.height)
                                                                           : std::clamp(c, m_VerticalMargin, std::max(m_VerticalMargin, w.height - m_VerticalMargin - menu.height));
                    }
                    if (i == 2 || (c >= 0 && c + menu.height <= w.height)) { y = c; break; }
                }
                const internal::TransformOrigin origin = internal::CalculateTransformOrigin(anchor, IntRect{ x, y, x + menu.width, y + menu.height });
                if (!(m_TransformOrigin.Peek() == origin)) m_TransformOrigin.Set(origin);
                return { x, y };
            }
            internal::TransformOrigin GetTransformOrigin() const { return m_TransformOrigin.Get(); }

        private:
            int m_TopInsets;
            const rt::State<int>* m_KeyboardSignal;
            int m_VerticalMargin;
            mutable rt::State<internal::TransformOrigin> m_TransformOrigin{ internal::TransformOrigin{} };
        };
    }

    std::shared_ptr<const ui::PopupPositionProvider> internal::MakeExposedDropdownMenuPositionProvider(ui::Density density, int topWindowInsets) {
        return std::make_shared<const PositionProvider>(density, topWindowInsets, nullptr);
    }

    // --- scope ----------------------------------------------------------------------------------

    ui::Modifier ExposedDropdownMenuBoxScope::MenuAnchor(ui::Modifier m, AnchorType type, bool enabled) const {
        m = FocusRequesterModifier(std::move(m), m_State->focusRequester);
        m = m.With<AnchorNode>(AnchorParams{ m_State, type });
        if (enabled) m = Expandable(std::move(m), m_State, type);
        return m;
    }

    ui::Modifier ExposedDropdownMenuBoxScope::ExposedDropdownSize(ui::Modifier m, bool matchAnchorWidth) const {
        auto state = m_State;
        return LayoutMod(std::move(m), [state, matchAnchorWidth](ui::MeasureScope& s, ui::Measurable& measurable, const ui::Constraints& c) {
            const int menuWidth = c.ConstrainWidth(state->anchorWidth.Get());
            ui::Constraints menu = c;
            menu.maxHeight = c.ConstrainHeight(state->menuMaxHeight.Get());
            menu.minHeight = std::min(menu.minHeight, menu.maxHeight);
            if (matchAnchorWidth) menu.minWidth = menu.maxWidth = menuWidth;
            ui::Placeable& p = measurable.Measure(menu);
            return s.Layout(p.Width(), p.Height(), [&p](ui::PlacementScope& place) { place.Place(p, 0, 0); });
        });
    }

    AnchorType ExposedDropdownMenuBoxScope::GetAnchorType() const { return m_State->anchorType.Get(); }
    bool ExposedDropdownMenuBoxScope::AlwaysFocusable() const { return m_State->alwaysFocusable.Get(); }

    void ExposedDropdownMenuBox(bool expanded, std::function<void(bool)> onExpandedChange, const ui::Modifier& modifier,
                                std::function<void(const ExposedDropdownMenuBoxScope&)> content) {
        auto& c = *rt::Composition::Current();
        auto state = c.Remember<std::shared_ptr<internal::ExposedDropdownMenuBoxState>>([] { return std::make_shared<internal::ExposedDropdownMenuBoxState>(); });
        state->expanded = expanded;
        state->onExpandedChange = onExpandedChange;
        state->owner = c.Read(ui::LocalOwner);
        state->expandedDescription = GetString(Strings::MenuExpanded);
        state->collapsedDescription = GetString(Strings::MenuCollapsed);
        state->toggleDescription = GetString(Strings::ToggleDropdownMenu);
        const ui::Density density = c.Read(ui::LocalDensity);
        const int verticalMargin = density.RoundToPx(internal::MenuVerticalMargin);
        const ExposedDropdownMenuBoxScope scope(state);

        Box(OnGloballyPositioned(modifier, [state, density, verticalMargin](const LayoutCoordinates& coords) {
                state->anchorWidth.Set(coords.GetSize().width);
                const ui::Offset p = coords.PositionInRoot();
                const ui::Rect anchor{ p.x, p.y, p.x + float(coords.GetSize().width), p.y + float(coords.GetSize().height) };
                state->menuMaxHeight.Set(internal::ExposedDropdownMenuMaxHeight(VisibleWindowBounds(state->owner, density), anchor, verticalMargin));
            }),
            Alignment::TopStart(), false, [content, scope] { content(scope); });
        if (expanded) {
            // OnPlatformWindowBoundsChange: the keyboard changes the room for the menu.
            (void)WindowInsets::Ime();
        }
        c.SideEffect([state, expanded] {
            if (expanded) state->focusRequester.RequestFocus();
        });
        BackHandler(expanded, [state] {
            if (state->onExpandedChange) state->onExpandedChange(false);
        });
    }

    void ExposedDropdownMenu(const ExposedDropdownMenuBoxScope& scope, bool expanded, std::function<void()> onDismissRequest,
                             const ui::Modifier& modifier, const ExposedDropdownMenuOptions& o, std::function<void()> content) {
        auto& c = *rt::Composition::Current();
        auto& keyboardSignal = c.RememberState(0);
        const ui::Density density = c.Read(ui::LocalDensity);
        ui::Owner* owner = c.Read(ui::LocalOwner);
        const int topWindowInsets =
            owner ? WindowInsets::StatusBars().Top(InsetsContext{ &owner->Insets(), density, ui::LayoutDirection::Ltr }) : 0;
        auto& expandedState = c.RememberNew<MutableTransitionState<bool>>({}, {}, false);
        expandedState.SetTargetState(expanded);
        ScrollState& scrollState = o.scrollState ? *o.scrollState : RememberScrollState();
        if (!(expandedState.CurrentState() || expandedState.TargetState())) return;

        auto provider = c.Remember<std::shared_ptr<const PositionProvider>>(
            [&] { return std::make_shared<const PositionProvider>(density, topWindowInsets, &keyboardSignal); },
            rt::Keys(density.density, density.fontScale, topWindowInsets));
        // popupPropertiesForAnchorType: an editable anchor keeps the keyboard (the popup takes
        // no focus) unless arrow keys moved into the menu; with accessibility services on, a
        // secondary anchor's menu takes focus.
        const auto manager = c.Read(ui::LocalAccessibilityManager);
        const bool a11y = manager && manager->ServiceState().enabled;
        const AnchorType type = scope.GetAnchorType();
        const bool imeRequired = type == AnchorType::PrimaryEditable || (type == AnchorType::SecondaryEditable && !a11y);
        ui::PopupProperties properties;
        properties.focusable = !(imeRequired && !scope.AlwaysFocusable());

        const ShapePtr shape = o.shape ? o.shape : MenuDefaults::shape();
        const Color containerColor = o.containerColor ? *o.containerColor : MenuDefaults::containerColor();
        const Dp tonal = o.tonalElevation, shadow = o.shadowElevation ? *o.shadowElevation : MenuDefaults::ShadowElevation;
        const auto border = o.border;
        const ui::Modifier menuModifier = scope.ExposedDropdownSize(modifier, o.matchAnchorWidth);
        auto* state = &expandedState;
        auto* scroll = &scrollState;
        foundation::Popup(provider, std::move(onDismissRequest), properties, [=, content = std::move(content)] {
            internal::DropdownMenuContent(menuModifier, *state, [provider] { return provider->GetTransformOrigin(); }, *scroll, shape, containerColor, tonal,
                                          shadow, border, content);
        });
    }

    // --- defaults -------------------------------------------------------------------------------

    void ExposedDropdownMenuDefaults::TrailingIcon(bool expanded, const ui::Modifier& modifier) {
        Icon(internal::Icons::Filled::ArrowDropDown(), std::nullopt, Rotate(modifier, expanded ? 180.0f : 0.0f));
    }

    // The text, icon, label, placeholder and affix colours both autocomplete token sets share.
#define KILN_AUTOCOMPLETE_COLORS(T, c)                                                                                   \
    do {                                                                                                                  \
        const auto disabled = [](tokens::ColorToken t, float alpha) { return Value(t).WithAlpha(alpha); };                 \
        c.focusedTextColor = Value(T::FieldFocusInputTextColor);                                                          \
        c.unfocusedTextColor = Value(T::FieldInputTextColor);                                                             \
        c.disabledTextColor = disabled(T::FieldDisabledInputTextColor, T::FieldDisabledInputTextOpacity);                 \
        c.errorTextColor = Value(T::FieldErrorInputTextColor);                                                            \
        c.cursorColor = Value(T::TextFieldCaretColor);                                                                    \
        c.errorCursorColor = Value(T::TextFieldErrorFocusCaretColor);                                                     \
        c.focusedLeadingIconColor = Value(T::TextFieldFocusLeadingIconColor);                                             \
        c.unfocusedLeadingIconColor = Value(T::TextFieldLeadingIconColor);                                                \
        c.disabledLeadingIconColor = disabled(T::TextFieldDisabledLeadingIconColor, T::TextFieldDisabledLeadingIconOpacity); \
        c.errorLeadingIconColor = Value(T::TextFieldErrorLeadingIconColor);                                               \
        c.focusedTrailingIconColor = Value(T::TextFieldFocusTrailingIconColor);                                           \
        c.unfocusedTrailingIconColor = Value(T::TextFieldTrailingIconColor);                                              \
        c.disabledTrailingIconColor = disabled(T::TextFieldDisabledTrailingIconColor, T::TextFieldDisabledTrailingIconOpacity); \
        c.errorTrailingIconColor = Value(T::TextFieldErrorTrailingIconColor);                                             \
        c.focusedLabelColor = Value(T::FieldFocusLabelTextColor);                                                         \
        c.unfocusedLabelColor = Value(T::FieldLabelTextColor);                                                            \
        c.disabledLabelColor = Value(T::FieldDisabledLabelTextColor);                                                     \
        c.errorLabelColor = Value(T::FieldErrorLabelTextColor);                                                           \
        const Color supporting = Value(T::FieldSupportingTextColor);                                                      \
        const Color disabledSupporting = disabled(T::FieldDisabledSupportingTextColor, T::FieldDisabledSupportingTextOpacity); \
        c.focusedPlaceholderColor = c.unfocusedPlaceholderColor = c.errorPlaceholderColor = supporting;                    \
        c.disabledPlaceholderColor = disabledSupporting;                                                                  \
        c.focusedPrefixColor = c.unfocusedPrefixColor = c.errorPrefixColor = supporting;                                   \
        c.disabledPrefixColor = disabledSupporting;                                                                       \
        c.focusedSuffixColor = c.unfocusedSuffixColor = c.errorSuffixColor = supporting;                                   \
        c.disabledSuffixColor = disabledSupporting;                                                                       \
    } while (false)

    TextFieldColors ExposedDropdownMenuDefaults::textFieldColors() {
        namespace T = tokens::FilledAutocompleteTokens;
        TextFieldColors c = TextFieldDefaults::colors();
        KILN_AUTOCOMPLETE_COLORS(T, c);
        c.focusedContainerColor = c.unfocusedContainerColor = c.disabledContainerColor = c.errorContainerColor = Value(T::TextFieldContainerColor);
        c.focusedIndicatorColor = Value(T::TextFieldFocusActiveIndicatorColor);
        c.unfocusedIndicatorColor = Value(T::TextFieldActiveIndicatorColor);
        c.disabledIndicatorColor = Value(T::TextFieldDisabledActiveIndicatorColor).WithAlpha(T::TextFieldDisabledActiveIndicatorOpacity);
        c.errorIndicatorColor = Value(T::TextFieldErrorActiveIndicatorColor);
        return c;
    }

    TextFieldColors ExposedDropdownMenuDefaults::outlinedTextFieldColors() {
        namespace T = tokens::OutlinedAutocompleteTokens;
        TextFieldColors c = OutlinedTextFieldDefaults::colors();
        KILN_AUTOCOMPLETE_COLORS(T, c);
        c.focusedContainerColor = c.unfocusedContainerColor = c.disabledContainerColor = c.errorContainerColor = Color::Transparent();
        c.focusedIndicatorColor = Value(T::TextFieldFocusOutlineColor);
        c.unfocusedIndicatorColor = Value(T::TextFieldOutlineColor);
        c.disabledIndicatorColor = Value(T::TextFieldDisabledOutlineColor).WithAlpha(T::TextFieldDisabledOutlineOpacity);
        c.errorIndicatorColor = Value(T::TextFieldErrorOutlineColor);
        return c;
    }

    PaddingValues ExposedDropdownMenuDefaults::ItemContentPadding() { return { Dp(16), Dp(0), Dp(16), Dp(0) }; }

}
