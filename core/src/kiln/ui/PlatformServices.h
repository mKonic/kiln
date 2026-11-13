#pragma once

// The platform services Compose hands content through CompositionLocals (CompositionLocals.kt):
// focus, input mode, clipboard, URIs, the text toolbar, view configuration and autofill. Each
// local is unprovided by default; its Current*() reads the provided one, else the window
// owner's (as Compose's defaults come from the AndroidComposeView).

#include "kiln/runtime/Composition.h"
#include <kiln/Function.h>
#include "kiln/runtime/Task.h"
#include "kiln/ui/Clip.h"
#include "kiln/ui/Focus.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Pointer.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::ui {

    class AutofillManager;

    // FocusManager: clears focus or moves it in a direction.
    class FocusManager {
    public:
        virtual ~FocusManager() = default;
        virtual void ClearFocus(bool force = false) = 0;
        virtual bool MoveFocus(FocusDirection direction) = 0;
    };
    inline rt::Local<std::shared_ptr<FocusManager>> LocalFocusManager{ nullptr };
    std::shared_ptr<FocusManager> CurrentFocusManager();

    // InputModeManager: touch or keyboard (observable); requestInputMode switches when allowed.
    class InputModeManager {
    public:
        virtual ~InputModeManager() = default;
        virtual InputMode GetInputMode() const = 0;
        virtual bool RequestInputMode(InputMode mode) = 0;
    };
    inline rt::Local<std::shared_ptr<InputModeManager>> LocalInputModeManager{ nullptr };
    std::shared_ptr<InputModeManager> CurrentInputModeManager();

    // Clipboard: the primary clip (getClipEntry / setClipEntry; null clears it).
    class Clipboard {
    public:
        virtual ~Clipboard() = default;
        virtual rt::Task<std::optional<ClipEntry>> GetClipEntry() = 0;
        virtual rt::Task<> SetClipEntry(std::optional<ClipEntry> entry) = 0;
    };
    inline rt::Local<std::shared_ptr<Clipboard>> LocalClipboard{ nullptr };
    std::shared_ptr<Clipboard> CurrentClipboard();

    // UriHandler: opens a URI with the platform.
    class UriHandler {
    public:
        virtual ~UriHandler() = default;
        virtual void OpenUri(const std::string& uri) = 0;
    };
    inline rt::Local<std::shared_ptr<UriHandler>> LocalUriHandler{ nullptr };
    std::shared_ptr<UriHandler> CurrentUriHandler();

    // TextToolbar: the floating copy / paste / cut / select all (/ autofill) menu over `rect`
    // (root pixels); a null action leaves its item out.
    enum class TextToolbarStatus : std::uint8_t { Shown, Hidden };
    class TextToolbar {
    public:
        virtual ~TextToolbar() = default;
        virtual void ShowMenu(Rect rect, kiln::Function<void()> onCopyRequested = {}, kiln::Function<void()> onPasteRequested = {},
                              kiln::Function<void()> onCutRequested = {}, kiln::Function<void()> onSelectAllRequested = {},
                              kiln::Function<void()> onAutofillRequested = {}) = 0;
        virtual void Hide() = 0;
        virtual TextToolbarStatus Status() const = 0;
    };
    inline rt::Local<std::shared_ptr<TextToolbar>> LocalTextToolbar{ nullptr };
    std::shared_ptr<TextToolbar> CurrentTextToolbar();

    // LocalViewConfiguration: timeouts, slops and fling limits.
    inline rt::Local<const ViewConfiguration*> LocalViewConfiguration{ nullptr };
    const ViewConfiguration& CurrentViewConfiguration();

    // LocalAutofillManager: null where autofill is unsupported.
    inline rt::Local<AutofillManager*> LocalAutofillManager{ nullptr };
    AutofillManager* CurrentAutofillManager();

    // LocalCursorBlinkEnabled: whether text field cursors blink (tests turn it off).
    inline rt::Local<bool> LocalCursorBlinkEnabled{ true };
    // LocalInspectionMode: true in a tool's preview (no live services).
    inline rt::Local<bool> LocalInspectionMode{ false };

    // LocalLocaleList / LocalLocale: the content's locales (BCP 47, most preferred first),
    // from the configuration unless provided.
    inline rt::Local<std::optional<std::vector<std::string>>> LocalLocaleList{ std::nullopt };
    std::vector<std::string> CurrentLocaleList();
    std::string CurrentLocale();

}
