#include "kiln/foundation/Focus.h"
#include "kiln/ui/BringIntoView.h"

#include "kiln/runtime/Composition.h"
#include "kiln/ui/Locals.h"
#include "kiln/ui/Semantics.h"

namespace kiln::foundation {

    namespace {
        // Callbacks are refreshed on recomposition through a remembered box (rememberUpdatedState),
        // so the node never compares or captures stale lambdas.
        template<typename F> std::shared_ptr<F> Latest(F f) {
            auto box = std::make_shared<F>();
            if (auto* c = rt::Composition::Current()) box = c->Remember<std::shared_ptr<F>>([&] { return box; });
            *box = std::move(f);
            return box;
        }

        struct FocusParams {
            Focusability f; std::shared_ptr<std::function<void(FocusState)>> cb;
            std::shared_ptr<MutableInteractionSource> source;
            std::shared_ptr<ui::PinnableContainer> pin;   // LocalPinnableContainer where it was composed
            bool operator==(const FocusParams& o) const { return f == o.f && cb == o.cb && source == o.source && pin == o.pin; }
        };
        struct FocusNode final : FocusTargetNode, ui::SemanticsModifier {
            FocusParams params;
            explicit FocusNode(FocusParams p) : params(std::move(p)) { Apply(); }
            // FocusableNode.applySemantics: focused state and the requestFocus action.
            ui::SemanticsModifier* AsSemantics() override { return this; }
            void ApplySemantics(ui::SemanticsConfiguration& c) override {
                c.focused = IsFocused(State());
                c.requestFocus = [this] { return RequestFocus(); };
            }
            void OnParamsChanged() {
                // A new container while focused: move the pin over.
                if (pinned && pinned != params.pin) { pinned->Release(); pinned = nullptr; if (IsFocused(State()) && params.pin) { pinned = params.pin; pinned->Pin(); } }
                Apply();
            }
            std::uint64_t focusInteraction = 0;
            std::shared_ptr<ui::PinnableContainer> pinned;
            void Apply() {
                focusability = params.f;
                auto cb = params.cb;
                onFocusChange = [this, cb](FocusState, FocusState now) {
                    // FocusableNode: FocusInteraction.Focus / Unfocus on the source.
                    if (params.source) {
                        if (IsFocused(now) && !focusInteraction) focusInteraction = params.source->Start(Interaction::Kind::Focus);
                        else if (!IsFocused(now) && focusInteraction) { params.source->End(Interaction::Kind::Focus, focusInteraction); focusInteraction = 0; }
                    }
                    // FocusableNode: focused content pins its lazy item (it stays composed when
                    // scrolled away) and asks its scroll containers to show it.
                    if (IsFocused(now) && !pinned && params.pin) { pinned = params.pin; pinned->Pin(); }
                    else if (!IsFocused(now) && pinned) { pinned->Release(); pinned = nullptr; }
                    if (IsFocused(now) && Owner()) ui::BringIntoView(*Owner(), std::nullopt, this);
                    if (cb && *cb) (*cb)(now);
                };
            }
            void OnDetach() override {
                if (params.source && focusInteraction) { params.source->End(Interaction::Kind::Focus, focusInteraction); focusInteraction = 0; }
                if (pinned) { pinned->Release(); pinned = nullptr; }
                FocusTargetNode::OnDetach();
            }
        };

        struct EventParams {
            std::shared_ptr<std::function<void(FocusState)>> cb;
            bool operator==(const EventParams& o) const { return cb == o.cb; }
        };
        struct EventNode final : ModifierNode, FocusEventModifier {
            EventParams params;
            std::optional<FocusState> last;
            explicit EventNode(EventParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            FocusEventModifier* AsFocusEvent() override { return this; }
            void OnFocusEvent(FocusState s) override {
                if (last == s) return;                      // FocusChangedModifier dedupes
                last = s;
                if (params.cb && *params.cb) (*params.cb)(s);
            }
        };

        struct RequesterParams { FocusRequester* r; bool operator==(const RequesterParams&) const = default; };
        struct RequesterNode final : FocusTargetNode {
            // Not a real target: registers the first target after it with the requester. kiln
            // keeps it simple by being a never-focusable group the requester enters.
            RequesterParams params;
            explicit RequesterNode(RequesterParams p) : params(p) { focusability = Focusability::Never; }
            void OnParamsChanged() {}
            std::weak_ptr<const char> alive;
            void OnAttach() override { alive = params.r->AliveToken(); params.r->Add(this); }
            void OnDetach() override { if (!alive.expired()) params.r->Remove(this); FocusTargetNode::OnDetach(); }
        };

        struct KeyParams {
            std::shared_ptr<std::function<bool(const KeyEvent&)>> onKey, onPreview;
            bool operator==(const KeyParams& o) const { return onKey == o.onKey && onPreview == o.onPreview; }
        };
        struct KeyNode final : ModifierNode, KeyInputModifier {
            KeyParams params;
            explicit KeyNode(KeyParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            KeyInputModifier* AsKeyInput() override { return this; }
            bool OnPreKeyEvent(const KeyEvent& e) override { return params.onPreview && *params.onPreview && (*params.onPreview)(e); }
            bool OnKeyEvent(const KeyEvent& e) override { return params.onKey && *params.onKey && (*params.onKey)(e); }
        };
    }

    Modifier Focusable(Modifier m, Focusability f, std::function<void(FocusState)> cb, std::shared_ptr<MutableInteractionSource> source) {
        auto* c = rt::Composition::Current();
        auto pin = c ? c->Read(ui::LocalPinnableContainer) : nullptr;
        return m.With<FocusNode>(FocusParams{ f, Latest(std::move(cb)), std::move(source), std::move(pin) });
    }
    namespace {
        struct PropsParams {
            std::shared_ptr<std::function<void(ui::FocusProperties&)>> fn;
            bool operator==(const PropsParams& o) const { return fn == o.fn; }
        };
        struct PropsNode final : ModifierNode, ui::FocusPropertiesModifier {
            PropsParams params;
            explicit PropsNode(PropsParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::FocusPropertiesModifier* AsFocusProperties() override { return this; }
            void ApplyFocusProperties(ui::FocusProperties& p) override { if (params.fn && *params.fn) (*params.fn)(p); }
        };
    }

    Modifier FocusPropertiesMod(Modifier m, std::function<void(ui::FocusProperties&)> fn) {
        return m.With<PropsNode>(PropsParams{ Latest(std::move(fn)) });
    }

    Modifier OnFocusChanged(Modifier m, std::function<void(FocusState)> cb) {
        return m.With<EventNode>(EventParams{ Latest(std::move(cb)) });
    }
    Modifier FocusRequesterModifier(Modifier m, FocusRequester& r) {
        return m.With<RequesterNode>(RequesterParams{ &r });
    }
    Modifier OnKeyEvent(Modifier m, std::function<bool(const KeyEvent&)> onKey, std::function<bool(const KeyEvent&)> onPreview) {
        return m.With<KeyNode>(KeyParams{ Latest(std::move(onKey)), Latest(std::move(onPreview)) });
    }

}
