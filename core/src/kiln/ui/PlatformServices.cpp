#include "kiln/ui/PlatformServices.h"
#include <kiln/Function.h>

#include "kiln/ui/Configuration.h"
#include "kiln/ui/LayoutNode.h"

namespace kiln::ui {

    namespace {
        Owner* CurrentOwner() {
            auto* c = rt::Composition::Current();
            return c ? c->Read(LocalOwner) : nullptr;
        }
        template<typename T>
        std::shared_ptr<T> Provided(const rt::Local<std::shared_ptr<T>>& local) {
            auto* c = rt::Composition::Current();
            return c ? c->Read(local) : nullptr;
        }

        struct OwnerFocusManager final : FocusManager {
            Owner* owner;
            explicit OwnerFocusManager(Owner* o) : owner(o) {}
            void ClearFocus(bool force) override { owner->ClearFocus(force); }
            bool MoveFocus(FocusDirection d) override { return owner->MoveFocus(d); }
        };
        struct OwnerInputModeManager final : InputModeManager {
            Owner* owner;
            explicit OwnerInputModeManager(Owner* o) : owner(o) {}
            InputMode GetInputMode() const override { return owner->GetInputMode(); }
            // requestInputMode: keyboard mode always; touch mode only from a touch (the
            // framework's requestTouchMode is not available to apps).
            bool RequestInputMode(InputMode mode) override {
                if (mode == owner->GetInputMode()) return true;
                if (mode == InputMode::Touch) return false;
                owner->SetInputMode(mode);
                return true;
            }
        };
        struct OwnerClipboard final : Clipboard {
            Owner* owner;
            explicit OwnerClipboard(Owner* o) : owner(o) {}
            rt::Task<std::optional<ClipEntry>> GetClipEntry() override {
                auto clip = owner->ClipboardClip();
                co_return clip ? std::optional<ClipEntry>(std::move(clip->first)) : std::nullopt;
            }
            rt::Task<> SetClipEntry(std::optional<ClipEntry> entry) override {
                owner->SetClipboardClip(entry);
                co_return;
            }
        };
        struct OwnerUriHandler final : UriHandler {
            Owner* owner;
            explicit OwnerUriHandler(Owner* o) : owner(o) {}
            // AndroidUriHandler throws when nothing can open it; kiln reports nothing.
            void OpenUri(const std::string& uri) override { owner->OpenUri(uri); }
        };
        // The legacy TextToolbar on the owner's toolbar session: one client with the asked items.
        struct OwnerTextToolbar final : TextToolbar, TextToolbarClient {
            Owner* owner;
            Rect rect;
            std::vector<std::pair<TextToolbarItemId, kiln::Function<void()>>> items;
            explicit OwnerTextToolbar(Owner* o) : owner(o) {}
            ~OwnerTextToolbar() override { Hide(); }
            void ShowMenu(Rect r, kiln::Function<void()> copy, kiln::Function<void()> paste, kiln::Function<void()> cut,
                          kiln::Function<void()> selectAll, kiln::Function<void()> autofill) override {
                rect = r;
                items.clear();
                // The order TextToolbarHelperMethods adds them in.
                if (copy) items.push_back({ TextToolbarItemId::Copy, std::move(copy) });
                if (paste) items.push_back({ TextToolbarItemId::Paste, std::move(paste) });
                if (cut) items.push_back({ TextToolbarItemId::Cut, std::move(cut) });
                if (selectAll) items.push_back({ TextToolbarItemId::SelectAll, std::move(selectAll) });
                if (autofill) items.push_back({ TextToolbarItemId::Autofill, std::move(autofill) });
                if (owner->CurrentTextToolbarClient() == this) owner->HideTextToolbar(this);
                owner->ShowTextToolbar(this);
            }
            void Hide() override { if (owner && owner->CurrentTextToolbarClient() == this) owner->HideTextToolbar(this); }
            TextToolbarStatus Status() const override {
                return owner && owner->CurrentTextToolbarClient() == this ? TextToolbarStatus::Shown : TextToolbarStatus::Hidden;
            }
            Rect ContentBounds() override { return rect; }
            std::vector<TextToolbarEntry> Entries() override {
                std::vector<TextToolbarEntry> out;
                for (const auto& [id, fn] : items) { TextToolbarEntry e; e.id = id; out.push_back(e); }
                return out;
            }
            void OnEntryClicked(std::size_t index) override {
                if (index >= items.size()) return;
                auto fn = items[index].second;
                Hide();
                fn();
            }
            void OnClosed() override {}
        };

        template<typename T, typename Impl>
        std::shared_ptr<T> OwnerService(Owner* owner, const char* tag) {
            // One per owner, kept with it.
            auto& slot = owner->Service(tag);
            if (!slot) slot = std::make_shared<Impl>(owner);
            return std::static_pointer_cast<T>(std::static_pointer_cast<Impl>(slot));
        }
    }

    std::shared_ptr<FocusManager> CurrentFocusManager() {
        if (auto p = Provided(LocalFocusManager)) return p;
        Owner* o = CurrentOwner();
        return o ? OwnerService<FocusManager, OwnerFocusManager>(o, "focus") : nullptr;
    }
    std::shared_ptr<InputModeManager> CurrentInputModeManager() {
        if (auto p = Provided(LocalInputModeManager)) return p;
        Owner* o = CurrentOwner();
        return o ? OwnerService<InputModeManager, OwnerInputModeManager>(o, "inputMode") : nullptr;
    }
    std::shared_ptr<Clipboard> CurrentClipboard() {
        if (auto p = Provided(LocalClipboard)) return p;
        Owner* o = CurrentOwner();
        return o ? OwnerService<Clipboard, OwnerClipboard>(o, "clipboard") : nullptr;
    }
    std::shared_ptr<UriHandler> CurrentUriHandler() {
        if (auto p = Provided(LocalUriHandler)) return p;
        Owner* o = CurrentOwner();
        return o ? OwnerService<UriHandler, OwnerUriHandler>(o, "uri") : nullptr;
    }
    std::shared_ptr<TextToolbar> CurrentTextToolbar() {
        if (auto p = Provided(LocalTextToolbar)) return p;
        Owner* o = CurrentOwner();
        return o ? OwnerService<TextToolbar, OwnerTextToolbar>(o, "textToolbar") : nullptr;
    }
    const ViewConfiguration& CurrentViewConfiguration() {
        static const ViewConfiguration fallback;
        auto* c = rt::Composition::Current();
        if (c) {
            if (const ViewConfiguration* v = c->Read(LocalViewConfiguration)) return *v;
            if (Owner* o = c->Read(LocalOwner)) return o->Config();
        }
        return fallback;
    }
    AutofillManager* CurrentAutofillManager() {
        auto* c = rt::Composition::Current();
        if (!c) return nullptr;
        if (AutofillManager* a = c->Read(LocalAutofillManager)) return a;
        Owner* o = c->Read(LocalOwner);
        return o ? o->Autofill() : nullptr;
    }
    std::vector<std::string> CurrentLocaleList() {
        auto* c = rt::Composition::Current();
        if (c) {
            if (const auto& provided = c->Read(LocalLocaleList)) return *provided;
            const auto& locales = c->Read(LocalConfiguration).locales;
            if (!locales.empty()) return locales;
        }
        return PlatformLocaleList();
    }
    std::string CurrentLocale() {
        const auto list = CurrentLocaleList();
        return list.empty() ? std::string("en-US") : list.front();
    }

}
