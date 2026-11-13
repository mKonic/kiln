#include "kiln/foundation/Popup.h"
#include <kiln/Function.h>

#include "kiln/foundation/Back.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

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
        // and its layer in the owner. Where the owner has a PopupHost, the popup is a window of
        // its own instead: the content has its own owner, and the window is placed on the screen
        // (updateParentBounds / updatePosition).
        struct PopupState {
            const bool windowed;
            std::unique_ptr<LayoutNode> holder = std::make_unique<LayoutNode>();
            std::unique_ptr<ui::Owner> windowOwner;
            std::unique_ptr<rt::Composition> composition;
            ui::PopupLayer layer;
            ui::Owner* owner = nullptr;

            // The window: its platform half, the anchor's bounds on the screen, where it went.
            std::unique_ptr<ui::PlatformPopup> window;
            std::optional<ui::IntRect> parentBounds;
            std::optional<ui::IntRect> placed;
            kiln::Function<void()> onWindowMoved;
            // Sets the window's content (kept so a re-shown window gets it again).
            kiln::Function<void()> applyContent;
            std::shared_ptr<bool> alive = std::make_shared<bool>(true);

            // The popup window's own focus: the top focusable popup has it.
            struct PopupWindowInfo final : ui::WindowInfo {
                const PopupState* s = nullptr;
                bool IsWindowFocused() const override {
                    if (s->windowed) return s->windowOwner->IsWindowFocused();
                    return s->owner && s->owner->IsWindowFocused() && s->owner->TopFocusablePopup() == &s->layer;
                }
                // The window the popup shows over.
                ui::IntSize ContainerSize() const override {
                    if (s->windowed) return s->windowOwner->ObservedWindowSize();
                    return s->owner ? s->owner->ObservedWindowSize() : ui::IntSize{};
                }
            } windowInfo;

            explicit PopupState(bool windowed_) : windowed(windowed_) {
                layer.node = holder.get();
                windowInfo.s = this;
                holder->SetMeasurePolicy(std::make_shared<SimpleStackPolicy>());
                if (windowed) {
                    windowOwner = std::make_unique<ui::Owner>();
                    composition = std::make_unique<rt::Composition>(windowOwner->Root(), rt::Dispatcher::Main());
                    onWindowMoved = [this] { UpdateParentBounds(); };
                    // A new content size: placed again (onSizeChanged { updatePosition() }), after
                    // the pass that measured it.
                    windowOwner->SetOnContentSizeChanged([this, a = std::weak_ptr<bool>(alive)](ui::IntSize) {
                        rt::Dispatcher::Main().Post([this, a] { if (!a.expired()) UpdatePosition(); });
                    });
                } else {
                    composition = std::make_unique<rt::Composition>(*holder, rt::Dispatcher::Main());
                }
            }
            ~PopupState() {
                if (owner && !windowed) owner->RemovePopup(&layer);
                if (owner && windowed) owner->RemoveWindowMovedListener(&onWindowMoved);
                // PopupLayout.dispose: the content goes first, while its window's hosts exist.
                composition.reset();
                window.reset();
                windowOwner.reset();
                holder.reset();
            }

            ui::PopupWindowProperties WindowProperties() const {
                ui::PopupWindowProperties w;
                const ui::PopupProperties& p = layer.properties;
                w.focusable = p.focusable;
                w.dismissOnBackPress = p.dismissOnBackPress;
                w.dismissOnClickOutside = p.dismissOnClickOutside;
                w.secure = ui::ShouldApplySecureFlag(p.securePolicy, owner && owner->FlagSecure());
                w.clippingEnabled = p.clippingEnabled;
                w.scrimAlpha = p.scrimAlpha;
                w.direction = layer.direction;
                return w;
            }

            // show: the window, once there is an owner to ask for it.
            void Show() {
                if (window || !owner || !owner->PopupWindows().create) return;
                window = owner->PopupWindows().create(*windowOwner, WindowProperties(), [a = std::weak_ptr<bool>(alive), this] {
                    if (a.expired()) return;
                    if (layer.onDismissRequest) { auto f = layer.onDismissRequest; f(); }
                });
                if (applyContent) applyContent();
            }

            // dismiss: the content goes first, while the window's hosts still exist
            // (disposeComposition, then dismiss), then the window.
            void Hide() {
                if (!window) return;
                composition->SetContent([] {});
                composition->Recompose();
                window.reset();
                parentBounds.reset();
                placed.reset();
            }

            // updateParentBounds: the anchor on the screen; placed again when it moved.
            void UpdateParentBounds() {
                const auto anchor = layer.anchorBounds ? layer.anchorBounds() : std::nullopt;
                if (!anchor || !owner) return;
                const ui::PopupHost& host = owner->PopupWindows();
                const ui::IntOffset o = host.windowOrigin ? host.windowOrigin() : ui::IntOffset{};
                const ui::IntRect r{ anchor->left + o.x, anchor->top + o.y, anchor->right + o.x, anchor->bottom + o.y };
                if (parentBounds && *parentBounds == r) return;
                parentBounds = r;
                UpdatePosition();
            }

            // updatePosition: the content measured up to the display bounds
            // (internalOnMeasure), the provider asked with the anchor and that frame, and the
            // window moved there. Until both the anchor and the content size are known the
            // window is not shown.
            void UpdatePosition() {
                if (!window || !parentBounds || !layer.positionProvider || !owner) return;
                const ui::PopupHost& host = owner->PopupWindows();
                const bool clip = layer.properties.clippingEnabled;
                const ui::IntSize ws = owner->WindowSize();
                const ui::IntOffset o = host.windowOrigin ? host.windowOrigin() : ui::IntOffset{};
                const ui::IntRect d = host.displayBounds ? host.displayBounds(clip) : ui::IntRect{ o.x, o.y, o.x + ws.width, o.y + ws.height };
                const int dw = std::max(0, d.right - d.left), dh = std::max(0, d.bottom - d.top);
                const Constraints c{ 0, dw, 0, dh };
                windowOwner->SetContentConstraints(c);
                const ui::IntSize size = windowOwner->MeasureContent(c);
                // The provider sees the anchor and the frame in one space: the frame's.
                const ui::IntRect anchor{ parentBounds->left - d.left, parentBounds->top - d.top, parentBounds->right - d.left,
                                          parentBounds->bottom - d.top };
                ui::IntOffset pos = layer.positionProvider->CalculatePosition(anchor, { dw, dh }, layer.direction, size);
                // Gravity.applyDisplay: kept on the display unless FLAG_LAYOUT_NO_LIMITS.
                if (clip) {
                    pos.x = std::max(0, std::min(pos.x, dw - size.width));
                    pos.y = std::max(0, std::min(pos.y, dh - size.height));
                }
                // At least a pixel: a window with no size gets no surface, so its content (still
                // empty, or growing in) would never be laid out again to report its real size.
                const ui::IntRect r{ d.left + pos.x, d.top + pos.y, d.left + pos.x + std::max(1, size.width), d.top + pos.y + std::max(1, size.height) };
                layer.bounds = ui::IntRect{ r.left - o.x, r.top - o.y, r.right - o.x, r.bottom - o.y };
                if (placed && *placed == r) return;
                placed = r;
                window->Place(r);
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
        struct PopupAnchorNode final : ModifierNode, ui::GlobalPositionAwareModifier {
            AnchorParams params;
            explicit PopupAnchorNode(AnchorParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            // onGloballyPositioned { updateParentLayoutCoordinates(parent) } for a window.
            void OnGloballyPositioned() override {
                if (params.state->windowed) params.state->UpdateParentBounds();
            }
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
                if (s.windowed) {
                    s.owner->AddWindowMovedListener(&s.onWindowMoved);
                    s.Show();
                    return;
                }
                s.owner->AddPopup(&s.layer);
            }
            void OnDetach() override {
                PopupState& s = *params.state;
                if (s.owner && s.windowed) {
                    s.owner->RemoveWindowMovedListener(&s.onWindowMoved);
                    s.Hide();
                } else if (s.owner) s.owner->RemovePopup(&s.layer);
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
            ui::Owner* parent = c.Read(ui::LocalOwner);
            const bool windowed = parent && parent->PopupWindows().create;
            auto& state = c.Remember<std::shared_ptr<PopupState>>([windowed] { return std::make_shared<PopupState>(windowed); });
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
                if (changed && s.owner && !s.windowed) s.owner->UpdatePopup(&s.layer);
                if (s.windowed) {
                    ui::Owner& o = *s.windowOwner;
                    if (s.owner) {
                        if (!(o.GetDensity() == s.owner->GetDensity())) o.SetDensity(s.owner->GetDensity());
                        if (o.Text() != s.owner->Text()) o.SetText(s.owner->Text());
                    }
                    if (o.Direction() != direction) o.SetDirection(direction);
                    if (s.window && changed) s.window->Update(s.WindowProperties());
                    if (changed) { s.placed.reset(); s.UpdatePosition(); }
                }

                // The popup root: popup() semantics, and the scrim behind it over the window.
                Modifier root = Semantics(Modifier{}, [](ui::SemanticsConfiguration& cfg) { cfg.isPopup = true; });
                // A window dims what is behind it itself (FLAG_DIM_BEHIND).
                if (!s.windowed && !std::isnan(properties.scrimAlpha) && properties.scrimAlpha > 0) {
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
                if (s.windowed) {
                    // The window's content: its owner and insets, then SimpleStack(popup()).
                    s.applyContent = [st = &s, content] {
                    st->composition->SetContent([st, content] {
                        auto* cc = rt::Composition::Current();
                        ui::Owner* o = st->windowOwner.get();
                        cc->Provide(LocalIsInPopupLayout, true, [cc, st, o, content] {
                        cc->Provide(ui::LocalOwner, o, [cc, st, o, content] {
                        cc->Provide(ui::LocalWindowInsets, &o->Insets(), [cc, st, content] {
                        cc->Provide(ui::LocalWindowInfo, static_cast<const ui::WindowInfo*>(&st->windowInfo), [content] {
                            Layout(Semantics(Modifier{}, [](ui::SemanticsConfiguration& cfg) { cfg.isPopup = true; }),
                                   std::make_shared<SimpleStackPolicy>(), content);
                        });
                        });
                        });
                        });
                    });
                    };
                    // Only while the window is up: without one the content would have no hosts.
                    if (s.window) s.applyContent();
                } else {
                    s.composition->SetContent([st = &s, content] {
                        auto* cc = rt::Composition::Current();
                        cc->Provide(LocalIsInPopupLayout, true, [cc, st, content] {
                            cc->Provide(ui::LocalWindowInfo, static_cast<const ui::WindowInfo*>(&st->windowInfo), content);
                        });
                    });
                }
                s.composition->Recompose();
                // The content is composed now: the window gets its real size before it first shows
                // (rather than a pixel until the content's own layout reports it).
                if (s.windowed) {
                    if (!s.parentBounds) s.UpdateParentBounds();
                    s.UpdatePosition();
                }
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
