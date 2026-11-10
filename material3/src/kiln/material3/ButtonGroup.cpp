#include "kiln/material3/ButtonGroup.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/material3/Button.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/Menu.h"
#include "kiln/material3/Shapes.h"
#include "kiln/material3/Text.h"
#include "kiln/material3/Tooltip.h"
#include "kiln/material3/internal/GetString.h"
#include "kiln/material3/internal/Icons.h"
#include "kiln/material3/tokens/ButtonGroupSmallTokens.h"
#include "kiln/material3/tokens/ConnectedButtonGroupSmallTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/material3/tokens/ShapeTokens.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <set>

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using namespace tokens;

    namespace internal {
        struct ButtonGroupItem {
            virtual ~ButtonGroupItem() = default;
            virtual void ButtonGroupContent() = 0;
            virtual void MenuContent(ButtonGroupMenuState& state) = 0;
        };
    }

    namespace {
        constexpr std::int64_t MaxWaitTimeMillis = 1000;

        // ButtonGroupParentData: in ParentData::extra.
        struct ButtonGroupParentData {
            float weight = 0;
            std::shared_ptr<anim::Animatable> pressed;   // null: never pressed (0)
            std::optional<AlignmentV> alignment;
            Dp compressionLimit{ 0 };
        };
        ButtonGroupParentData& DataIn(ParentData& data) {
            if (!std::any_cast<ButtonGroupParentData>(&data.extra)) data.extra = ButtonGroupParentData{};
            return *std::any_cast<ButtonGroupParentData>(&data.extra);
        }
        ButtonGroupParentData DataOf(const Measurable& m) {
            const auto* d = std::any_cast<ButtonGroupParentData>(&m.GetParentData().extra);
            return d ? *d : ButtonGroupParentData{};
        }
        float PressedOf(const ButtonGroupParentData& d) { return d.pressed ? d.pressed->Float() : 0.0f; }

        // ButtonGroupElement (weight).
        struct WeightParams {
            float weight;
            bool operator==(const WeightParams&) const = default;
        };
        struct WeightNode final : ModifierNode, ParentDataModifier {
            WeightParams params;
            explicit WeightNode(WeightParams p) : params(p) {}
            ParentDataModifier* AsParentData() override { return this; }
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            void ModifyParentData(ParentData& data) const override { DataIn(data).weight = params.weight; }
        };

        // VerticalAlignElement.
        struct AlignParams {
            AlignmentV alignment;
            bool operator==(const AlignParams& o) const { return alignment.bias == o.alignment.bias; }
        };
        struct AlignNode final : ModifierNode, ParentDataModifier {
            AlignParams params;
            explicit AlignNode(AlignParams p) : params(p) {}
            ParentDataModifier* AsParentData() override { return this; }
            void OnParamsChanged() { if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure(); }
            void ModifyParentData(ParentData& data) const override { DataIn(data).alignment = params.alignment; }
        };

        // EnlargeOnPressElement: the pressed fraction animates to 1 while any press is down, and
        // back to 0 once it has passed 0.75 (or a second went by) after the last release.
        struct EnlargeParams {
            std::shared_ptr<MutableInteractionSource> source;
            anim::SpecPtr spec;
            std::optional<Dp> compressionLimit;
            bool operator==(const EnlargeParams& o) const {
                return source == o.source && spec == o.spec && compressionLimit.has_value() == o.compressionLimit.has_value() &&
                       (!compressionLimit || compressionLimit->value == o.compressionLimit->value);
            }
        };
        struct EnlargeNode final : ModifierNode, ParentDataModifier {
            EnlargeParams params;
            std::shared_ptr<anim::Animatable> pressed = std::make_shared<anim::Animatable>(0.0f);
            std::shared_ptr<MutableInteractionSource> collecting;
            std::uint64_t token = 0;
            std::set<std::uint64_t> presses;
            bool lastPressed = false;
            rt::Job pressJob, latestJob;
            explicit EnlargeNode(EnlargeParams p) : params(std::move(p)) {}
            ParentDataModifier* AsParentData() override { return this; }
            void OnAttach() override { LaunchCollection(); }
            void OnDetach() override { StopCollection(); }
            void OnParamsChanged() {
                if (params.source != collecting) LaunchCollection();
                if (Owner() && Owner()->Parent()) Owner()->Parent()->RequestRemeasure();
            }
            void StopCollection() {
                if (collecting) collecting->Unsubscribe(token);
                collecting = nullptr;
                presses.clear();
                lastPressed = false;
                pressJob.Cancel();
                latestJob.Cancel();
            }
            void LaunchCollection() {
                StopCollection();
                collecting = params.source;
                if (!collecting) return;
                token = collecting->Subscribe([this](const Interaction& i) {
                    if (i.kind != Interaction::Kind::Press) return;
                    if (i.phase == Interaction::Phase::Start) presses.insert(i.id);
                    else presses.erase(i.id);
                    const bool isPressed = !presses.empty();
                    if (isPressed == lastPressed) return;   // distinctUntilChanged
                    lastPressed = isPressed;
                    latestJob.Cancel();                      // collectLatest
                    if (isPressed) pressJob = rt::Launch(rt::Dispatcher::Main(), Grow(pressed, params.spec));
                    else latestJob = rt::Launch(rt::Dispatcher::Main(), Shrink(pressed, params.spec));
                });
            }
            static rt::Task<> Grow(std::shared_ptr<anim::Animatable> a, anim::SpecPtr spec) { co_await a->AnimateTo(1.0f, std::move(spec)); }
            static rt::Task<> Shrink(std::shared_ptr<anim::Animatable> a, anim::SpecPtr spec) {
                // waitUntil { value > 0.75 }, for a second at most.
                const std::int64_t start = co_await rt::NextFrame();
                while (!(a->Value()[0] > 0.75f)) {
                    const std::int64_t now = co_await rt::NextFrame();
                    if ((now - start) / 1'000'000 > MaxWaitTimeMillis) break;
                }
                co_await a->AnimateTo(0.0f, std::move(spec));
            }
            void ModifyParentData(ParentData& data) const override {
                auto& d = DataIn(data);
                d.pressed = pressed;
                // ButtonDefaults.ContentPadding's end (the same at either end).
                d.compressionLimit = params.compressionLimit ? *params.compressionLimit : ButtonDefaults::ContentPadding.end;
            }
        };

        // ButtonGroupMeasurePolicy: the items first, the overflow content last.
        struct OverflowState {
            rt::State<int> totalItemCount{ 0 }, visibleItemCount{ 0 };
        };
        class ButtonGroupMeasurePolicy final : public MeasurePolicy {
        public:
            std::shared_ptr<OverflowState> overflowState;
            Arrangement horizontalArrangement;
            AlignmentV verticalAlignment = AlignmentV::Top();
            float expandedRatio = ButtonGroupDefaults::ExpandedRatio;

            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> children, const Constraints& constraints) override {
                if (children.empty()) return s.Layout(constraints.minWidth, constraints.minHeight);
                const auto content = children.first(children.size() - 1);
                Measurable* overflow = children.back();
                if (overflowState->totalItemCount.Peek() != int(content.size())) overflowState->totalItemCount.Set(int(content.size()));
                const int arrangementSpacingInt = s.RoundToPx(horizontalArrangement.Spacing());
                const std::int64_t arrangementSpacingPx = arrangementSpacingInt;
                const int size = int(content.size());
                float totalWeight = 0;
                int fixedSpace = 0, weightChildrenCount = 0;
                std::vector<std::optional<Constraints>> childrenConstraints(size);
                std::vector<ButtonGroupParentData> configs(size);
                for (int i = 0; i < size; ++i) configs[i] = DataOf(*content[i]);
                const int mainAxisMin = constraints.minWidth, mainAxisMax = constraints.maxWidth;
                // The unweighted children first, at their maximum intrinsic widths.
                int spaceAfterLastNoWeight = 0;
                for (int i = 0; i < size; ++i) {
                    const float weight = configs[i].weight;
                    if (weight > 0) {
                        totalWeight += weight;
                        ++weightChildrenCount;
                    } else {
                        const int remaining = mainAxisMax - fixedSpace;
                        const int desiredWidth = content[i]->MaxIntrinsicWidth(constraints.maxHeight);
                        Constraints c = constraints;
                        c.minWidth = 0;
                        c.maxWidth = std::max(desiredWidth, 0);
                        childrenConstraints[i] = c;
                        spaceAfterLastNoWeight = std::min(arrangementSpacingInt, std::max(remaining - desiredWidth, 0));
                        fixedSpace += desiredWidth + spaceAfterLastNoWeight;
                    }
                }
                int weightedSpace = 0;
                if (weightChildrenCount == 0) {
                    // fixedSpace holds a spacing after the last unweighted child.
                    fixedSpace -= spaceAfterLastNoWeight;
                } else {
                    // The rest share what is left by weight.
                    const int targetSpace = mainAxisMax != Constraints::Infinity ? mainAxisMax : mainAxisMin;
                    const std::int64_t arrangementSpacingTotal = arrangementSpacingPx * (weightChildrenCount - 1);
                    const std::int64_t remainingToTarget = std::max<std::int64_t>(targetSpace - fixedSpace - arrangementSpacingTotal, 0);
                    const float weightUnitSpace = float(remainingToTarget) / totalWeight;
                    std::int64_t remainder = remainingToTarget;
                    for (int i = 0; i < size; ++i) remainder -= RoundJava(weightUnitSpace * configs[i].weight);
                    for (int i = 0; i < size; ++i) {
                        if (!childrenConstraints[i]) {
                            // The rounding loss or gain goes one pixel each to the first children.
                            const int remainderUnit = remainder > 0 ? 1 : remainder < 0 ? -1 : 0;
                            remainder -= remainderUnit;
                            const int childMainAxisSize = std::max(0, RoundJava(weightUnitSpace * configs[i].weight) + remainderUnit);
                            Constraints c = constraints;
                            c.minWidth = childMainAxisSize != Constraints::Infinity ? childMainAxisSize : 0;
                            c.maxWidth = childMainAxisSize;
                            childrenConstraints[i] = c;
                            weightedSpace += childMainAxisSize;
                        }
                        weightedSpace = int(std::clamp<std::int64_t>(weightedSpace + arrangementSpacingTotal, 0,
                                                                     std::max(0, mainAxisMax - fixedSpace)));
                    }
                }
                int remainingSpace = mainAxisMax;
                int mainSpace = 0;        // the shown items, the spacing and the overflow
                int shownItemSpace = 0;   // the shown items and their spacing
                std::vector<int> widths(size);
                for (int i = 0; i < size; ++i) widths[i] = (childrenConstraints[i] ? *childrenConstraints[i] : constraints).maxWidth;
                std::int64_t desiredWidth = std::int64_t(arrangementSpacingInt) * (size - 1);
                for (int w : widths) desiredWidth += w;
                int lastItem = 0;
                Placeable* overflowPlaceable = nullptr;
                if (desiredWidth <= mainAxisMax) {
                    // Everything fits.
                    lastItem = size;
                    mainSpace = int(desiredWidth);
                } else {
                    // Room for the overflow indicator first, then as many items as fit.
                    const int overflowWidth = overflow->MaxIntrinsicWidth(constraints.maxHeight);
                    remainingSpace -= overflowWidth;
                    mainSpace += overflowWidth;
                    while (lastItem < size && widths[lastItem] <= remainingSpace) {
                        mainSpace += widths[lastItem];
                        shownItemSpace += widths[lastItem];
                        remainingSpace -= widths[lastItem++] + arrangementSpacingInt;
                    }
                    mainSpace += arrangementSpacingInt * lastItem;
                    shownItemSpace += arrangementSpacingInt * lastItem;
                    // A fixed incoming width (fillMaxWidth) may be wider than the indicator's room.
                    Constraints c = constraints;
                    c.minWidth = 0;
                    c.maxWidth = std::max(remainingSpace + overflowWidth, 0);
                    overflowPlaceable = &overflow->Measure(c);
                }
                if (overflowState->visibleItemCount.Peek() != lastItem) overflowState->visibleItemCount.Set(lastItem);
                if (size > 1) {
                    // A pressed item widens, taking the room from its neighbours.
                    for (int index = 0; index < lastItem; ++index) {
                        const float value = PressedOf(configs[index]);
                        if (value == 0) continue;
                        int actualGrowth = 0;
                        if (index >= 1 && index < lastItem - 1) {
                            const int targetGrowth = RoundJava(value * std::min({ expandedRatio * widths[index] / 2.0f,
                                                                                  s.ToPx(configs[index - 1].compressionLimit),
                                                                                  s.ToPx(configs[index + 1].compressionLimit) }));
                            const int growthLeft = std::min(targetGrowth, widths[index - 1]);
                            const int growthRight = std::min(targetGrowth, widths[index + 1]);
                            widths[index - 1] -= growthLeft;
                            widths[index + 1] -= growthRight;
                            actualGrowth = growthLeft + growthRight;
                        } else if (index == 0) {
                            if (index + 1 >= size) continue;
                            const int targetGrowth = RoundJava(value * std::min(expandedRatio * widths[index], s.ToPx(configs[index + 1].compressionLimit)));
                            const int growthRight = std::min(targetGrowth, widths[index + 1]);
                            widths[index + 1] -= growthRight;
                            actualGrowth = growthRight;
                        } else {
                            const int targetGrowth = RoundJava(value * std::min(expandedRatio * widths[index], s.ToPx(configs[index - 1].compressionLimit)));
                            const int growthLeft = std::min(targetGrowth, widths[index - 1]);
                            widths[index - 1] -= growthLeft;
                            actualGrowth = growthLeft;
                        }
                        widths[index] += actualGrowth;
                    }
                }
                std::vector<Placeable*> placeables;
                for (int index = 0; index < lastItem; ++index) {
                    Constraints c = childrenConstraints[index] ? *childrenConstraints[index] : constraints;
                    c.minWidth = widths[index];
                    c.maxWidth = widths[index];
                    placeables.push_back(&content[index]->Measure(c));
                }
                const int mainAxisLayoutSize = std::max(std::max(mainSpace, 0), mainAxisMin);
                std::vector<int> positions(lastItem);
                horizontalArrangement.Arrange(s, mainAxisLayoutSize, std::span<const int>(widths.data(), lastItem), s.Direction(), positions);
                int height = constraints.minHeight;
                if (!placeables.empty()) {
                    height = 0;
                    for (Placeable* p : placeables) height = std::max(height, p->Height());
                }
                std::vector<std::optional<AlignmentV>> alignments(lastItem);
                for (int i = 0; i < lastItem; ++i) alignments[i] = configs[i].alignment;
                const AlignmentV vertical = verticalAlignment;
                return s.Layout(mainAxisLayoutSize, height, [=](PlacementScope& place) {
                    for (std::size_t i = 0; i < placeables.size(); ++i) {
                        const AlignmentV a = alignments[i] ? *alignments[i] : vertical;
                        place.Place(*placeables[i], positions[i], a.Align(placeables[i]->Height(), height));
                    }
                    if (overflowPlaceable) place.PlaceRelative(*overflowPlaceable, shownItemSpace, vertical.Align(overflowPlaceable->Height(), height));
                });
            }
        };

        TextOptions LabelOptions() {
            TextOptions o;
            o.maxLines = 1;
            o.softWrap = false;
            o.overflow = text::TextOverflow::Visible;
            return o;
        }
        void Label(const std::function<void()>& icon, const std::string& label) {
            if (icon) {
                icon();
                Spacer(SizeMod({}, ButtonDefaults::IconSpacing, ButtonDefaults::IconSpacing));
            }
            Text(label, {}, LabelOptions());
        }

        Modifier ItemModifier(const std::shared_ptr<MutableInteractionSource>& source, const anim::SpecPtr& spec, Dp compressionLimit, float weight) {
            Modifier m = Modifier{}.With<EnlargeNode>(EnlargeParams{ source, spec, compressionLimit });
            if (!std::isnan(weight)) m = m.With<WeightNode>(WeightParams{ std::min(weight, FLT_MAX) });
            return m;
        }

        class ClickableItem final : public internal::ButtonGroupItem {
        public:
            ClickableItem(std::function<void()> onClick, std::function<void()> icon, float weight, anim::SpecPtr spec, bool enabled, std::string label)
                : m_OnClick(std::move(onClick)), m_Icon(std::move(icon)), m_Weight(weight), m_Spec(std::move(spec)), m_Enabled(enabled),
                  m_Label(std::move(label)) {}
            void ButtonGroupContent() override {
                const auto source = RememberInteractionSource();
                const PaddingValues padding = m_Icon ? ButtonDefaults::ButtonWithIconContentPadding : ButtonDefaults::ContentPadding;
                ButtonOptions o;
                o.enabled = m_Enabled;
                o.contentPadding = padding;
                o.interactionSource = source;
                Button(m_OnClick, ItemModifier(source, m_Spec, padding.end, m_Weight), o, [icon = m_Icon, label = m_Label] { Label(icon, label); });
            }
            void MenuContent(ButtonGroupMenuState& state) override {
                DropdownMenuItemOptions o;
                o.enabled = m_Enabled;
                o.leadingIcon = m_Icon;
                DropdownMenuItem([label = m_Label] { Text(label); }, [onClick = m_OnClick, &state] { onClick(); state.Dismiss(); }, {}, o);
            }

        private:
            std::function<void()> m_OnClick, m_Icon;
            float m_Weight;
            anim::SpecPtr m_Spec;
            bool m_Enabled;
            std::string m_Label;
        };

        class ToggleableItem final : public internal::ButtonGroupItem {
        public:
            ToggleableItem(bool checked, std::function<void(bool)> onCheckedChange, float weight, anim::SpecPtr spec, std::function<void()> icon,
                           bool enabled, std::string label)
                : m_Checked(checked), m_OnCheckedChange(std::move(onCheckedChange)), m_Weight(weight), m_Spec(std::move(spec)), m_Icon(std::move(icon)),
                  m_Enabled(enabled), m_Label(std::move(label)) {}
            void ButtonGroupContent() override {
                const auto source = RememberInteractionSource();
                const PaddingValues padding = m_Icon ? ButtonDefaults::ButtonWithIconContentPadding : ButtonDefaults::ContentPadding;
                ToggleButtonOptions o;
                o.enabled = m_Enabled;
                o.contentPadding = padding;
                o.interactionSource = source;
                ToggleButton(m_Checked, m_OnCheckedChange, ItemModifier(source, m_Spec, padding.end, m_Weight), o,
                             [icon = m_Icon, label = m_Label] { Label(icon, label); });
            }
            void MenuContent(ButtonGroupMenuState& state) override {
                DropdownMenuItemOptions o;
                o.enabled = m_Enabled;
                o.leadingIcon = m_Icon;
                DropdownMenuItem([label = m_Label] { Text(label); },
                                 [checked = m_Checked, change = m_OnCheckedChange, &state] { change(!checked); state.Dismiss(); }, {}, o);
            }

        private:
            bool m_Checked;
            std::function<void(bool)> m_OnCheckedChange;
            float m_Weight;
            anim::SpecPtr m_Spec;
            std::function<void()> m_Icon;
            bool m_Enabled;
            std::string m_Label;
        };

        class CustomItem final : public internal::ButtonGroupItem {
        public:
            CustomItem(std::function<void()> content, std::function<void(ButtonGroupMenuState&)> menu)
                : m_Content(std::move(content)), m_Menu(std::move(menu)) {}
            void ButtonGroupContent() override { if (m_Content) m_Content(); }
            void MenuContent(ButtonGroupMenuState& state) override { if (m_Menu) m_Menu(state); }

        private:
            std::function<void()> m_Content;
            std::function<void(ButtonGroupMenuState&)> m_Menu;
        };

        // Cached so morph state keyed on the shapes survives recomposition.
        struct ConnectedShapes {
            ShapePtr leading, leadingPressed, trailing, trailingPressed, checked, middlePressed;
        };
        const ConnectedShapes& Connected() {
            static const ConnectedShapes shapes = [] {
                const CornerSize full = ShapeDefaults::CornerFull();
                const CornerSize inner = ConnectedButtonGroupSmallTokens::InnerCornerCornerSize;
                const CornerSize pressed = ConnectedButtonGroupSmallTokens::PressedInnerCornerCornerSize;
                ConnectedShapes s;
                s.leading = RoundedCorner(full, inner, inner, full);
                s.leadingPressed = RoundedCorner(full, pressed, pressed, full);
                s.trailing = RoundedCorner(inner, full, full, inner);
                s.trailingPressed = RoundedCorner(pressed, full, full, pressed);
                s.checked = RoundedCorner(full);
                s.middlePressed = RoundedCorner(pressed);
                return s;
            }();
            return shapes;
        }
    }

    // --- ButtonGroupScope --------------------------------------------------------------------

    Modifier ButtonGroupScope::Weight(Modifier m, float weight) const {
        // require(weight > 0); infinity becomes the largest float.
        if (!(weight > 0)) return m;
        return m.With<WeightNode>(WeightParams{ std::min(weight, FLT_MAX) });
    }
    Modifier ButtonGroupScope::AnimateWidth(Modifier m, std::shared_ptr<MutableInteractionSource> source, std::optional<Dp> compressionLimit) const {
        return m.With<EnlargeNode>(EnlargeParams{ std::move(source), m_Spec, compressionLimit });
    }
    Modifier ButtonGroupScope::Align(Modifier m, AlignmentV alignment) const { return m.With<AlignNode>(AlignParams{ alignment }); }

    void ButtonGroupScope::ClickableItem(std::function<void()> onClick, std::string label, std::function<void()> icon, float weight, bool enabled) {
        // require(weight > 0 || weight is NaN)
        if (!(weight > 0 || std::isnan(weight))) weight = NAN;
        m_Items.push_back(std::make_shared<class ClickableItem>(std::move(onClick), std::move(icon), weight, m_Spec, enabled, std::move(label)));
    }
    void ButtonGroupScope::ToggleableItem(bool checked, std::string label, std::function<void(bool)> onCheckedChange, std::function<void()> icon,
                                          float weight, bool enabled) {
        if (!(weight > 0 || std::isnan(weight))) weight = NAN;
        m_Items.push_back(std::make_shared<class ToggleableItem>(checked, std::move(onCheckedChange), weight, m_Spec, std::move(icon), enabled,
                                                                 std::move(label)));
    }
    void ButtonGroupScope::CustomItem(std::function<void()> buttonGroupContent, std::function<void(ButtonGroupMenuState&)> menuContent) {
        m_Items.push_back(std::make_shared<class CustomItem>(std::move(buttonGroupContent), std::move(menuContent)));
    }

    // --- ButtonGroupDefaults -------------------------------------------------------------------

    namespace ButtonGroupDefaults {
        Arrangement HorizontalArrangement() { return Arrangement::SpacedBy(ButtonGroupSmallTokens::BetweenSpace); }
        const Dp ConnectedSpaceBetween = ConnectedButtonGroupSmallTokens::BetweenSpace;
        ShapePtr connectedLeadingButtonShape() { return Connected().leading; }
        ShapePtr connectedLeadingButtonPressShape() { return Connected().leadingPressed; }
        ShapePtr connectedTrailingButtonShape() { return Connected().trailing; }
        ShapePtr connectedTrailingButtonPressShape() { return Connected().trailingPressed; }
        ShapePtr connectedButtonCheckedShape() { return Connected().checked; }
        ShapePtr connectedMiddleButtonPressShape() { return Connected().middlePressed; }
        ToggleButtonShapes connectedLeadingButtonShapes(ShapePtr shape, ShapePtr pressedShape, ShapePtr checkedShape) {
            return { shape ? shape : connectedLeadingButtonShape(), pressedShape ? pressedShape : connectedLeadingButtonPressShape(),
                     checkedShape ? checkedShape : connectedButtonCheckedShape() };
        }
        ToggleButtonShapes connectedMiddleButtonShapes(ShapePtr shape, ShapePtr pressedShape, ShapePtr checkedShape) {
            return { shape ? shape : ShapePtr(ShapeDefaults::Small()), pressedShape ? pressedShape : connectedMiddleButtonPressShape(),
                     checkedShape ? checkedShape : connectedButtonCheckedShape() };
        }
        ToggleButtonShapes connectedTrailingButtonShapes(ShapePtr shape, ShapePtr pressedShape, ShapePtr checkedShape) {
            return { shape ? shape : connectedTrailingButtonShape(), pressedShape ? pressedShape : connectedTrailingButtonPressShape(),
                     checkedShape ? checkedShape : connectedButtonCheckedShape() };
        }

        void OverflowIndicator(ButtonGroupMenuState& menuState, const Modifier& modifier, const OverflowIndicatorOptions& options) {
            const std::string contentDescription = GetString(Strings::ButtonGroupMoreOptions);
            IconButtonOptions o;
            o.enabled = options.enabled;
            o.shape = options.shape ? options.shape : IconButtonDefaults::filledShape();
            o.colors = options.colors ? *options.colors : IconButtonDefaults::filledIconButtonColors();
            o.interactionSource = options.interactionSource;
            auto* state = &menuState;
            TooltipBox(
                TooltipDefaults::rememberTooltipPositionProvider(TooltipAnchorPosition::Above),
                [contentDescription](const TooltipScope& scope) {
                    internal::PlainTooltipInternal(scope, contentDescription, {}, {}, [contentDescription] { Text(contentDescription); });
                },
                RememberTooltipState(), {}, {}, [=] {
                    FilledIconButton([state] { if (state->IsShowing()) state->Dismiss(); else state->Show(); }, modifier, o,
                                     [contentDescription] { Icon(internal::Icons::Filled::MoreVert(), contentDescription); });
                });
        }
    }

    // --- ButtonGroup ---------------------------------------------------------------------------

    void ButtonGroup(std::function<void(ButtonGroupMenuState&)> overflowIndicator, const Modifier& modifier,
                     std::function<void(ButtonGroupScope&)> content, const ButtonGroupOptions& options) {
        auto& c = *rt::Composition::Current();
        auto scope = std::make_shared<ButtonGroupScope>(Value(MotionSchemeKeyTokens::FastSpatial));
        if (content) content(*scope);
        auto& menuState = c.Remember<std::shared_ptr<ButtonGroupMenuState>>([] { return std::make_shared<ButtonGroupMenuState>(); });
        auto& overflowState = c.Remember<std::shared_ptr<OverflowState>>([] { return std::make_shared<OverflowState>(); });
        // remember(horizontalArrangement, verticalAlignment, expandedRatio): a new policy
        // measures again.
        const Arrangement arrangement = options.horizontalArrangement ? *options.horizontalArrangement : ButtonGroupDefaults::HorizontalArrangement();
        auto& policy = c.Remember<std::shared_ptr<ButtonGroupMeasurePolicy>>([] { return std::shared_ptr<ButtonGroupMeasurePolicy>(); });
        if (!policy || !(policy->horizontalArrangement == arrangement) || policy->verticalAlignment.bias != options.verticalAlignment.bias ||
            policy->expandedRatio != options.expandedRatio) {
            policy = std::make_shared<ButtonGroupMeasurePolicy>();
            policy->overflowState = overflowState;
            policy->horizontalArrangement = arrangement;
            policy->verticalAlignment = options.verticalAlignment;
            policy->expandedRatio = options.expandedRatio;
        }
        auto menu = menuState;
        auto overflow = overflowState;
        Layout(modifier, policy, [scope, menu, overflow, overflowIndicator = std::move(overflowIndicator)] {
            for (const auto& item : scope->Items()) item->ButtonGroupContent();
            Box({}, Alignment::TopStart(), false, [=] {
                if (overflowIndicator) overflowIndicator(*menu);
                DropdownMenu(menu->IsShowing(), [menu] { menu->Dismiss(); }, {}, {}, [=] {
                    const int total = std::min<int>(overflow->totalItemCount.Get(), int(scope->Items().size()));
                    for (int i = overflow->visibleItemCount.Get(); i < total; ++i) scope->Items()[i]->MenuContent(*menu);
                });
            });
        });
    }

}
