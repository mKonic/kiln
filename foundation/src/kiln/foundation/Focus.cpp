#include "kiln/foundation/Focus.h"

#include "kiln/runtime/Composition.h"

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
            bool operator==(const FocusParams& o) const { return f == o.f && cb == o.cb; }
        };
        struct FocusNode final : FocusTargetNode {
            FocusParams params;
            explicit FocusNode(FocusParams p) : params(std::move(p)) { Apply(); }
            void OnParamsChanged() { Apply(); }
            void Apply() {
                focusability = params.f;
                auto cb = params.cb;
                onFocusChange = [cb](FocusState, FocusState now) { if (cb && *cb) (*cb)(now); };
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
            void OnAttach() override { params.r->Add(this); }
            void OnDetach() override { params.r->Remove(this); FocusTargetNode::OnDetach(); }
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

    Modifier Focusable(Modifier m, Focusability f, std::function<void(FocusState)> cb) {
        return m.With<FocusNode>(FocusParams{ f, Latest(std::move(cb)) });
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
