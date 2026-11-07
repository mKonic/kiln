#include "kiln/foundation/Dialog.h"

#include "kiln/foundation/Back.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    namespace {
        // DialogLayout: the children at the origin, as big as the biggest (the constraints'
        // minimum without any).
        struct DialogLayoutPolicy final : MeasurePolicy {
            MeasureResult Measure(MeasureScope& s, std::span<Measurable* const> ch, const Constraints& c) override {
                std::vector<Placeable*> ps;
                int w = 0, h = 0;
                for (Measurable* m : ch) {
                    Placeable& p = m->Measure(c);
                    w = std::max(w, p.Width());
                    h = std::max(h, p.Height());
                    ps.push_back(&p);
                }
                if (ch.empty()) { w = c.minWidth; h = c.minHeight; }
                return s.Layout(w, h, [ps](PlacementScope& place) { for (Placeable* p : ps) place.PlaceRelative(*p, 0, 0); });
            }
        };

        // DialogWrapper: the content's owner and composition, and the platform window.
        struct DialogState {
            std::unique_ptr<ui::Owner> owner = std::make_unique<ui::Owner>();
            std::unique_ptr<rt::Composition> composition;
            std::unique_ptr<ui::PlatformDialog> window;
            std::function<void()> onDismissRequest;
            DialogProperties properties;

            struct WindowInfoImpl final : ui::WindowInfo {
                const DialogState* s = nullptr;
                bool IsWindowFocused() const override { return s->owner->IsContentWindowFocused(); }
            } windowInfo;
            struct WindowImpl final : ui::Window {
                DialogState* s = nullptr;
                bool IsFlagSecure() const override { return s->owner->FlagSecure(); }
                void SetFlagSecure(bool secure) override { s->owner->SetFlagSecure(secure); }
            } windowImpl;

            DialogState() {
                windowInfo.s = this;
                windowImpl.s = this;
                composition = std::make_unique<rt::Composition>(owner->Root(), rt::Dispatcher::Main());
            }
            ~DialogState() {
                // DialogWrapper.dispose: the content goes first, while its window's hosts still
                // exist; then the window is dismissed.
                composition.reset();
                window.reset();
                owner.reset();
            }
            void Dismiss() const { if (onDismissRequest) { auto f = onDismissRequest; f(); } }
        };

        ui::DialogWindowProperties WindowProperties(const DialogProperties& p, ui::Owner* parent, const Density& d, LayoutDirection dir) {
            ui::DialogWindowProperties w;
            w.dismissOnBackPress = p.dismissOnBackPress;
            w.dismissOnClickOutside = p.dismissOnClickOutside;
            // setSecurePolicy: against the window the dialog was opened from.
            w.secure = ui::ShouldApplySecureFlag(p.securePolicy, parent && parent->FlagSecure());
            w.usePlatformDefaultWidth = p.usePlatformDefaultWidth;
            w.decorFitsSystemWindows = p.decorFitsSystemWindows;
            w.windowTitle = p.windowTitle;
            w.scrimAlpha = p.scrimAlpha;
            w.blurBehindRadiusPx = p.blurBehindRadius.Specified() ? d.RoundToPx(p.blurBehindRadius) : -1;
            w.backgroundBlurRadiusPx = p.backgroundBlurRadius.Specified() ? d.RoundToPx(p.backgroundBlurRadius) : -1;
            w.direction = dir;
            return w;
        }
    }

    void Dialog(std::function<void()> onDismissRequest, DialogProperties properties, std::function<void()> content, Key key) {
        rt::Composition::Current()->Child([onDismissRequest, properties, content] {
            auto& c = *rt::Composition::Current();
            ui::Owner* parent = c.Read(ui::LocalOwner);
            auto& state = c.Remember<std::shared_ptr<DialogState>>([] { return std::make_shared<DialogState>(); });
            const LayoutDirection direction = c.Read(ui::LocalLayoutDirection);
            ui::Owner& o = *state->owner;
            if (parent) {
                if (!(o.GetDensity() == parent->GetDensity())) o.SetDensity(parent->GetDensity());
                if (o.Text() != parent->Text()) o.SetText(parent->Text());
            }
            if (o.Direction() != direction) o.SetDirection(direction);
            const Density density = o.GetDensity();
            rt::Scope* parentScope = c.CurrentScope();
            // Before the window is made: it opens with these.
            if (!state->window) {
                state->properties = properties;
                state->onDismissRequest = onDismissRequest;
            }

            // The dialog window's own back callback (onBackPressedDispatcher.addCallback): before
            // the content's, so handlers inside the dialog go first; it holds Back while shown.
            BackHandler(true, [state] { if (state->properties.dismissOnBackPress) state->Dismiss(); });

            // show / dismiss.
            c.DisposableEffect(rt::Keys(state.get()), [state, parent, density, direction] {
                if (parent && parent->Dialogs().create)
                    state->window = parent->Dialogs().create(*state->owner, WindowProperties(state->properties, parent, density, direction),
                                                             [s = state.get()] { s->Dismiss(); });
                return std::function<void()>([state] { state->window.reset(); });
            });

            // updateParameters, then the content.
            c.SideEffect([state, onDismissRequest, properties, parent, density, direction, parentScope, content] {
                DialogState& s = *state;
                s.onDismissRequest = onDismissRequest;
                s.properties = properties;
                if (s.window) s.window->Update(WindowProperties(properties, parent, density, direction));
                ui::Owner* owner = s.owner.get();
                s.composition->SetParentContext(parentScope);
                s.composition->SetContent([owner, st = &s, content] {
                    auto* cc = rt::Composition::Current();
                    cc->Provide(ui::LocalOwner, owner, [cc, owner, st, content] {
                    cc->Provide(ui::LocalWindowInfo, static_cast<const ui::WindowInfo*>(&st->windowInfo), [cc, owner, st, content] {
                    cc->Provide(ui::LocalWindowInsets, &owner->Insets(), [cc, st, content] {
                    cc->Provide(ui::LocalWindow, static_cast<ui::Window*>(&st->windowImpl), [content] {
                        // DialogLayout(Modifier.semantics { dialog() }).
                        Layout(Semantics(Modifier{}, [](ui::SemanticsConfiguration& cfg) { cfg.isDialog = true; }),
                               std::make_shared<DialogLayoutPolicy>(), content);
                    });
                    });
                    });
                    });
                });
                s.composition->Recompose();
            });
        }, key);
    }

}
