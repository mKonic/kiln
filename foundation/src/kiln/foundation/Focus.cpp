#include "kiln/foundation/Focus.h"
#include "kiln/runtime/Saveable.h"
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
        // FocusRequesterModifierNode: not a target itself; the requester reaches the targets
        // right below it.
        struct RequesterNode final : ModifierNode {
            RequesterParams params;
            explicit RequesterNode(RequesterParams p) : params(p) {}
            void OnParamsChanged() {}
            std::weak_ptr<const char> alive;
            void OnAttach() override { alive = params.r->AliveToken(); params.r->Add(this); }
            void OnDetach() override { if (!alive.expired()) params.r->Remove(this); }
        };

        struct RestorerParams {
            FocusRequester* fallback;
            rt::SaveableStateRegistry* registry;   // LocalSaveableStateRegistry where it was composed
            bool operator==(const RestorerParams&) const = default;
        };
        // FocusRestorerNode (saveFocusedChild on exit, restoreFocusedChild on enter), a
        // never-focusable group. The child is remembered by its composite key hash, also in the
        // saveable registry ("pfc" + the group's hash), so it survives recreation.
        struct RestorerNode final : FocusTargetNode {
            RestorerParams params;
            std::optional<std::uint64_t> savedHash;
            std::uint64_t providerId = 0;
            std::string providerKey;
            std::weak_ptr<const char> registryAlive;
            explicit RestorerNode(RestorerParams p) : params(p) { focusability = Focusability::Never; }
            void OnParamsChanged() {}
            void OnDetach() override { Unregister(); FocusTargetNode::OnDetach(); }
            std::vector<FocusTargetNode*> Children() const {
                ui::Owner* o = Owner() ? Owner()->GetOwner() : nullptr;
                return o ? o->FocusChildrenOf(*this) : std::vector<FocusTargetNode*>{};
            }
            std::string Key() const { return "pfc" + std::to_string(Owner() ? Owner()->CompositeKeyHash() : 0); }
            void Unregister() {
                if (providerId && params.registry && !registryAlive.expired()) params.registry->Unregister(providerKey, providerId);
                providerId = 0;
            }
            static std::uint64_t HashOf(const FocusTargetNode& c) { return c.Owner() ? c.Owner()->CompositeKeyHash() : 0; }
            bool Save() {
                for (FocusTargetNode* c : Children()) {
                    if (!HasFocus(c->State())) continue;
                    const std::uint64_t hash = HashOf(*c);
                    savedHash = hash;
                    Unregister();
                    if (params.registry) {
                        providerKey = Key();
                        registryAlive = params.registry->AliveToken();
                        providerId = params.registry->RegisterProvider(providerKey, [hash] { return rt::SavedValue(std::int64_t(hash)); });
                    }
                    return true;
                }
                return false;
            }
            bool Restore() {
                // After recreation the hash comes back from the registry.
                if (!savedHash && params.registry)
                    if (auto v = params.registry->ConsumeRestored(Key()))
                        if (auto* i = std::get_if<std::int64_t>(&v->v)) savedHash = std::uint64_t(*i);
                if (!savedHash) return false;
                for (FocusTargetNode* c : Children()) if (HashOf(*c) == *savedHash) return c->RequestFocus();
                return false;
            }
            void ApplyOwnFocusProperties(ui::FocusProperties& p) const override {
                auto* self = const_cast<RestorerNode*>(this);
                p.onExit = [self](FocusEnterExitScope&) { self->Save(); };
                p.onEnter = [self](FocusEnterExitScope& scope) {
                    // Restoring requests focus, which redirects the change; otherwise the fallback.
                    if (!self->Restore() && self->params.fallback) {
                        if (self->params.fallback == ui::FocusProperties::Cancel()) scope.CancelFocusChange();
                        else self->params.fallback->RequestFocus();
                    }
                };
            }
        };

        struct GroupParams { bool operator==(const GroupParams&) const = default; };
        struct GroupNode final : FocusTargetNode {
            GroupParams params;
            explicit GroupNode(GroupParams p) : params(p) { focusability = Focusability::Never; }
            void OnParamsChanged() {}
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

        struct SoftKeyNode final : ModifierNode, SoftKeyboardInterceptionModifier {
            KeyParams params;
            explicit SoftKeyNode(KeyParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            SoftKeyboardInterceptionModifier* AsSoftKeyboardInterception() override { return this; }
            bool OnPreInterceptKeyBeforeSoftKeyboard(const KeyEvent& e) override { return params.onPreview && *params.onPreview && (*params.onPreview)(e); }
            bool OnInterceptKeyBeforeSoftKeyboard(const KeyEvent& e) override { return params.onKey && *params.onKey && (*params.onKey)(e); }
        };

        struct RotaryParams {
            std::shared_ptr<std::function<bool(const RotaryScrollEvent&)>> on, onPre;
            bool operator==(const RotaryParams& o) const { return on == o.on && onPre == o.onPre; }
        };
        struct RotaryNode final : ModifierNode, RotaryInputModifier {
            RotaryParams params;
            explicit RotaryNode(RotaryParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            RotaryInputModifier* AsRotaryInput() override { return this; }
            bool OnPreRotaryScrollEvent(const RotaryScrollEvent& e) override { return params.onPre && *params.onPre && (*params.onPre)(e); }
            bool OnRotaryScrollEvent(const RotaryScrollEvent& e) override { return params.on && *params.on && (*params.on)(e); }
        };
    }

    namespace {
        struct BarrierParams { bool operator==(const BarrierParams&) const { return true; } };
        // InteractionBarrierNode: registers with the owner while attached; an empty pointer
        // handler, like Compose's.
        struct BarrierNode final : ModifierNode, PointerInputModifier {
            BarrierParams params;
            ui::Owner* owner = nullptr;
            explicit BarrierNode(BarrierParams p) : params(p) {}
            void OnParamsChanged() {}
            PointerInputModifier* AsPointerInput() override { return this; }
            void OnPointerEvent(PointerEvent&, PointerEventPass, IntSize) override {}
            void OnCancelPointerInput() override {}
            void OnAttach() override {
                owner = Owner() ? Owner()->GetOwner() : nullptr;
                if (owner) owner->RegisterInteractionBarrier(Owner());
            }
            void OnDetach() override {
                if (owner) owner->UnregisterInteractionBarrier(Owner());
                owner = nullptr;
            }
        };
    }

    Modifier InteractionBarrier(Modifier m) { return m.With<BarrierNode>(BarrierParams{}); }

    Modifier OnInterceptKeyBeforeSoftKeyboard(Modifier m, std::function<bool(const KeyEvent&)> onKey, std::function<bool(const KeyEvent&)> onPre) {
        return m.With<SoftKeyNode>(KeyParams{ Latest(std::move(onKey)), Latest(std::move(onPre)) });
    }
    Modifier OnRotaryScrollEvent(Modifier m, std::function<bool(const RotaryScrollEvent&)> on, std::function<bool(const RotaryScrollEvent&)> onPre) {
        return m.With<RotaryNode>(RotaryParams{ Latest(std::move(on)), Latest(std::move(onPre)) });
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
    Modifier FocusGroup(Modifier m) { return m.With<GroupNode>(GroupParams{}); }
    Modifier FocusRestorer(Modifier m, FocusRequester* fallback) {
        rt::SaveableStateRegistry* registry = nullptr;
        if (auto* c = rt::Composition::Current()) registry = c->Read(rt::LocalSaveableStateRegistry);
        return m.With<RestorerNode>(RestorerParams{ fallback, registry });
    }
    Modifier OnKeyEvent(Modifier m, std::function<bool(const KeyEvent&)> onKey, std::function<bool(const KeyEvent&)> onPreview) {
        return m.With<KeyNode>(KeyParams{ Latest(std::move(onKey)), Latest(std::move(onPreview)) });
    }

}
