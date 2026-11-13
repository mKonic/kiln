#include "kiln/foundation/Popup.h"
#include <kiln/Function.h>

#include "kiln/foundation/Back.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    namespace {
        // SimpleStack: children at the origin, the popup as big as the biggest.
        struct SimpleStackPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) override {
                std::vector<Placeable*> ps;
                int w = 0, h = 0;
                for (Measurable* m : ch) {
                    Placeable& p = m->Measure(c);
                    w = std::max(w, p.Width());
                    h = std::max(h, p.Height());
                    ps.push_back(&p);
                }
                return s.Layout(w, h, [ps](PlacementScope& place) { for (Placeable* p : ps) place.PlaceRelative(*p, 0, 0); });
            }
        };

        // PopupLayout: the popup's root node, its content composition (a child of the caller's)
        // and its layer in the owner.
        struct PopupState {
            std::unique_ptr<LayoutNode> holder = std::make_unique<LayoutNode>();
            std::unique_ptr<rt::Composition> composition;
            ui::PopupLayer layer;
            ui::Owner* owner = nullptr;

            // The popup window's own focus: the top focusable popup has it.
            struct PopupWindowInfo final : ui::WindowInfo {
                const PopupState* s = nullptr;
                bool IsWindowFocused() const override {
                    return s->owner && s->owner->IsWindowFocused() && s->owner->TopFocusablePopup() == &s->layer;
                }
                // The window the popup shows over.
                ui::IntSize ContainerSize() const override { return s->owner ? s->owner->ObservedWindowSize() : ui::IntSize{}; }
            } windowInfo;

            PopupState() {
                layer.node = holder.get();
                windowInfo.s = this;
                holder->SetMeasurePolicy(std::make_shared<SimpleStackPolicy>());
                composition = std::make_unique<rt::Composition>(*holder, rt::Dispatcher::Main());
            }
            ~PopupState() {
                if (owner) owner->RemovePopup(&layer);
                composition.reset();
                holder.reset();
            }
            PopupState(const PopupState&) = delete;
            PopupState& operator=(const PopupState&) = delete;
        };

        // On the zero-size layout in the caller: shows the popup while attached (show / dismiss)
        // and gives it the bounds of the layout the Popup was called in (parentLayoutCoordinates:
        // its inner bounds), in window pixels.
        struct AnchorParams {
            std::shared_ptr<PopupState> state;
            bool operator==(const AnchorParams&) const = default;
        };
        struct PopupAnchorNode final : ModifierNode {
            AnchorParams params;
            explicit PopupAnchorNode(AnchorParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            void OnAttach() override {
                PopupState& s = *params.state;
                s.owner = Owner() ? Owner()->GetOwner() : nullptr;
                if (!s.owner) return;
                s.layer.anchorBounds = [this]() -> std::optional<ui::IntRect> {
                    const LayoutNode* self = Owner();
                    const LayoutNode* parent = self ? self->Parent() : nullptr;
                    if (!parent || (!parent->IsPlaced() && parent->Parent())) return std::nullopt;
                    const Rect r = parent->InnerRectInRoot();
                    const int x = int(std::lround(r.left)), y = int(std::lround(r.top));
                    return ui::IntRect{ x, y, x + int(std::lround(r.Width())), y + int(std::lround(r.Height())) };
                };
                s.owner->AddPopup(&s.layer);
            }
            void OnDetach() override {
                PopupState& s = *params.state;
                if (s.owner) s.owner->RemovePopup(&s.layer);
                s.layer.anchorBounds = nullptr;
                s.owner = nullptr;
            }
        };

        struct ZeroSizePolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const>, const Constraints&) override { return s.Layout(0, 0); }
        };
    }

    void Popup(std::shared_ptr<const PopupPositionProvider> popupPositionProvider, kiln::Function<void()> onDismissRequest,
               PopupProperties properties, kiln::Function<void()> content, Key key) {
        rt::Composition::Current()->Child([popupPositionProvider, onDismissRequest, properties, content] {
            auto& c = *rt::Composition::Current();
            auto& state = c.Remember<std::shared_ptr<PopupState>>([] { return std::make_shared<PopupState>(); });
            const LayoutDirection direction = c.Read(ui::LocalLayoutDirection);
            rt::Scope* parentScope = c.CurrentScope();

            // Before it is first shown: the layer has its properties when it is added.
            if (!state->owner) {
                state->layer.properties = properties;
                state->layer.onDismissRequest = onDismissRequest;
                state->layer.positionProvider = popupPositionProvider;
                state->layer.direction = direction;
            }

            // Back gestures dismiss a focusable popup (its window's OnBackInvoked overlay input).
            BackHandler(properties.focusable && properties.dismissOnBackPress, [state] {
                if (state->layer.onDismissRequest) state->layer.onDismissRequest();
            });

            // updateParameters / positionProvider: after the caller's changes apply.
            c.SideEffect([state, popupPositionProvider, onDismissRequest, properties, direction, parentScope, content] {
                PopupState& s = *state;
                const bool changed = !(s.layer.properties == properties) || s.layer.positionProvider != popupPositionProvider ||
                                     s.layer.direction != direction;
                s.layer.properties = properties;
                s.layer.onDismissRequest = onDismissRequest;
                s.layer.positionProvider = popupPositionProvider;
                s.layer.direction = direction;
                if (changed && s.owner) s.owner->UpdatePopup(&s.layer);

                // The popup root: popup() semantics, and the scrim behind it over the window.
                Modifier root = Semantics(Modifier{}, [](ui::SemanticsConfiguration& cfg) { cfg.isPopup = true; });
                if (!std::isnan(properties.scrimAlpha) && properties.scrimAlpha > 0) {
                    root = DrawBehind(std::move(root), [st = &s, alpha = properties.scrimAlpha](ui::DrawScope& d) {
                        if (!st->owner || !st->layer.bounds) return;
                        const ui::IntSize w = st->owner->WindowSize();
                        const std::uint32_t a = std::uint32_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
                        d.DrawRect(Color((a << 24) | 0x000000u), { -float(st->layer.bounds->left), -float(st->layer.bounds->top) },
                                   { float(w.width), float(w.height) });
                    });
                }
                s.holder->SetModifier(root);

                s.composition->SetParentContext(parentScope);
                s.composition->SetContent([st = &s, content] {
                    auto* cc = rt::Composition::Current();
                    cc->Provide(LocalIsInPopupLayout, true, [cc, st, content] {
                        cc->Provide(ui::LocalWindowInfo, static_cast<const ui::WindowInfo*>(&st->windowInfo), content);
                    });
                });
                s.composition->Recompose();
            });

            Layout(Modifier{}.With<PopupAnchorNode>(AnchorParams{ state }), std::make_shared<ZeroSizePolicy>());
        }, key);
    }

    void Popup(Alignment alignment, IntOffset offset, kiln::Function<void()> onDismissRequest, PopupProperties properties,
               kiln::Function<void()> content, Key key) {
        rt::Composition::Current()->Child([alignment, offset, onDismissRequest, properties, content] {
            auto& c = *rt::Composition::Current();
            auto& provider = c.Remember<std::shared_ptr<const PopupPositionProvider>>(
                [&] { return std::make_shared<AlignmentOffsetPositionProvider>(alignment, offset); },
                rt::Keys(alignment.horizontal, alignment.vertical, offset.x, offset.y));
            Popup(provider, onDismissRequest, properties, content);
        }, key);
    }

}
