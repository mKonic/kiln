#pragma once

// Platform text input (spec 02 req. 12, 04 req. 4-6): a focused text field starts one session;
// the host turns it into EditorInfo + InputConnection. IME calls arrive synchronously on the main
// thread and edit the field directly, so reads after writes see the new state.

#include <cstdint>
#include <functional>
#include <array>
#include <string>
#include <vector>

#include "kiln/text/AnnotatedString.h"
#include "kiln/ui/Clip.h"
#include "kiln/ui/Geometry.h"

namespace kiln::ui {

    // KeyboardType: the keyboard an IME shows (EditorInfo.inputType classes and variations).
    enum class KeyboardType : std::uint8_t {
        Text, Ascii, Number, Phone, Uri, Email, Password, NumberPassword, Decimal, PasswordVisible,
        PostalAddress, PersonName, EmailSubject, ShortMessage, LongMessage, Filter, Phonetic, DateTime, Date, Time,
        NumberSigned, DecimalSigned, DecimalPassword, NumberPasswordSigned, DecimalPasswordSigned,
    };
    inline bool IsPasswordKeyboard(KeyboardType t) {
        return t == KeyboardType::Password || t == KeyboardType::PasswordVisible || t == KeyboardType::NumberPassword ||
               t == KeyboardType::DecimalPassword || t == KeyboardType::NumberPasswordSigned || t == KeyboardType::DecimalPasswordSigned;
    }
    enum class ImeAction : std::uint8_t { Default, None, Go, Search, Send, Previous, Next, Done };
    enum class Capitalization : std::uint8_t { None, Characters, Words, Sentences };

    struct ImeOptions {
        KeyboardType keyboardType = KeyboardType::Text;
        ImeAction imeAction = ImeAction::Default;
        Capitalization capitalization = Capitalization::None;
        bool autoCorrect = true;
        bool singleLine = false;
        std::vector<std::string> hintLocales;   // BCP 47 tags (EditorInfo.hintLocales)
        // A receiveContent modifier is around the field: the keyboard may commit any content
        // (EditorInfo content MIME types */*, image/*, video/*).
        bool acceptsContent = false;
        // EditorInfoCompat.setStylusHandwritingEnabled (shouldEnableStylusHandwriting).
        bool stylusHandwriting = false;
    };

    // The field's visual text as the IME sees it; selection and composition are min / max.
    struct TextInputSnapshot {
        std::u16string text;
        int selStart = 0, selEnd = 0;
        int compStart = -1, compEnd = -1;
    };

    // CursorAnchorInfo (CursorAnchorInfoBuilder): positions in the text layout node, which
    // `matrix` (row-major 3x3) maps to the root view; the host adds the view's screen position.
    struct CursorAnchorInfo {
        static constexpr int kHasVisibleRegion = 1, kHasInvisibleRegion = 2, kIsRtl = 4;
        std::array<float, 9> matrix{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        int selStart = 0, selEnd = 0;
        bool hasInsertionMarker = false;
        float markerX = 0, markerTop = 0, markerBaseline = 0, markerBottom = 0;
        int markerFlags = 0;
        int composingStart = -1;
        std::u16string composingText;
        struct CharacterBounds {
            int index = 0;
            float left = 0, top = 0, right = 0, bottom = 0;
            int flags = 0;
            bool operator==(const CharacterBounds&) const = default;
        };
        std::vector<CharacterBounds> characterBounds;
        bool hasEditorBounds = false;
        std::array<float, 4> editorBounds{};               // editor and handwriting bounds
        std::vector<std::array<float, 4>> visibleLineBounds;
        bool operator==(const CursorAnchorInfo&) const = default;
    };

    // HandwritingGesture (android.view.inputmethod, API 34): a stylus gesture the keyboard asks the
    // editor to perform. Areas and points are in root (view) pixels.
    struct HandwritingGesture {
        enum class Type : std::uint8_t { Select, Delete, SelectRange, DeleteRange, JoinOrSplit, Insert, RemoveSpace };
        enum class Granularity : std::uint8_t { Character, Word };
        static constexpr int kResultUnknown = 0, kResultSuccess = 1, kResultUnsupported = 2, kResultFailed = 3, kResultCancelled = 4,
                             kResultFallback = 5;
        Type type = Type::Select;
        Granularity granularity = Granularity::Character;
        Rect area, endArea;           // the (start) area, the range gestures' end area
        Offset point, endPoint;       // join or split / insertion point; remove space start and end
        std::u16string textToInsert;
        std::optional<std::u16string> fallbackText;
    };

    // Implemented by a text field while it holds the session (StatelessInputConnection's
    // TextInputSession). Edits between BeginBatchEdit and the outermost EndBatchEdit apply together.
    class TextInputSession {
    public:
        virtual ~TextInputSession() = default;
        virtual ImeOptions Options() const = 0;
        virtual TextInputSnapshot Snapshot() const = 0;
        virtual bool BeginBatchEdit() { return true; }
        // False once the outermost batch ended.
        virtual bool EndBatchEdit() { return false; }
        virtual void CommitText(std::u16string_view text, int newCursorPosition, bool isTextSuggestionSelected = false) = 0;
        // `annotations`: the IME's composing spans (underline, colours, style), null for none.
        virtual void SetComposingText(std::u16string_view text, int newCursorPosition,
                                      const std::vector<text::Range<text::SpanStyle>>* annotations = nullptr,
                                      bool isTextSuggestionSelected = false) = 0;
        virtual void SetComposingRegion(int start, int end) = 0;
        virtual void FinishComposingText() = 0;
        virtual void DeleteSurroundingText(int before, int after) = 0;
        virtual void DeleteSurroundingTextInCodePoints(int before, int after) = 0;
        virtual void SetSelection(int start, int end) = 0;
        virtual void PerformImeAction(ImeAction action) = 0;
        // The IME moved the selection itself: the pointer is no longer what drives the field.
        virtual void UpdateDirectTouchInteraction(bool) {}
        // InputConnection.requestCursorUpdates: CURSOR_UPDATE_IMMEDIATE (1), _MONITOR (2) and the
        // filters (editor bounds 4, character bounds 8, insertion marker 16, visible line bounds
        // 32), which the host fills in for API levels without them.
        virtual void RequestCursorUpdates(int) {}
        // InputConnection.commitContent (Source.Keyboard): true when a receiver took any of it.
        // `permission` is the host's handle on the granted URI permission (0: none).
        // performHandwritingGesture: a kResult* code. previewHandwritingGesture: whether it showed a
        // preview (select and delete gestures); the preview stays until cancelled or replaced.
        virtual int PerformHandwritingGesture(const HandwritingGesture&) { return HandwritingGesture::kResultUnsupported; }
        virtual bool PreviewHandwritingGesture(const HandwritingGesture&) { return false; }
        virtual void CancelHandwritingPreview() {}
        virtual bool CommitContent(const ClipEntry&, const ClipMetadata&, const std::optional<std::string>& /*linkUri*/, int /*permission*/) { return false; }
    };

    // isStylusHandwritingSupported: process-wide (Android 14 / API 34 and later); the host sets it.
    inline bool& StylusHandwritingSupportedFlag() { static bool supported = false; return supported; }
    inline bool IsStylusHandwritingSupported() { return StylusHandwritingSupportedFlag(); }
    inline void SetStylusHandwritingSupported(bool supported) { StylusHandwritingSupportedFlag() = supported; }

    // Host side (the Android view / dev window).
    struct TextInputHost {
        std::function<void(const ImeOptions&, const TextInputSnapshot&)> start;   // restartInput + show keyboard
        std::function<void()> stop;                                               // hide keyboard + restartInput
        // The field changed without a restart: updateExtractedText for a monitoring IME, and
        // updateSelection when the selection or composition moved.
        std::function<void(const TextInputSnapshot&, bool selectionChanged)> update;
        std::function<void()> restartInput;                                       // content changed under a composition
        std::function<void()> showKeyboard;                                       // SoftwareKeyboardController.show
        std::function<void()> hideKeyboard;                                       // SoftwareKeyboardController.hide
        std::function<void(const CursorAnchorInfo&)> cursorAnchorInfo;            // InputMethodManager.updateCursorAnchorInfo
        std::function<void()> startStylusHandwriting;                             // InputMethodManager.startStylusHandwriting
    };

}
