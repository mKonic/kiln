#include "kiln/foundation/text/BasicTextField.h"
#include "kiln/ui/PlatformServices.h"

#include "kiln/TextBackend.h"
#include "kiln/foundation/Focus.h"
#include "kiln/foundation/content/ReceiveContent.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Hover.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Magnifier.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/text/BasicText.h"
#include "kiln/foundation/text/PlatformSelectionBehaviors.h"
#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include "kiln/foundation/text/handwriting/StylusHandwriting.h"
#include "kiln/foundation/text/input/HandwritingGestures.h"
#include "kiln/foundation/text/input/TextFieldHandles.h"
#include "kiln/foundation/text/contextmenu/TextContextMenuProvider.h"
#include "kiln/foundation/text/input/TextFieldKeys.h"
#include "kiln/foundation/text/input/TextFieldSelectionState.h"
#include "kiln/foundation/text/input/TextLayoutState.h"
#include "kiln/ui/ContentType.h"
#include "kiln/ui/LayoutNode.h"
#include "kiln/ui/Locals.h"

#include <cmath>
#include <map>
#include <stdexcept>

namespace kiln::foundation {

    namespace {

        constexpr float kDefaultCursorThicknessDp = 2;
        // TextFieldDelegate: the minimum width is 10 characters of "H".
        constexpr const char* kEmptyTextReplacement = "HHHHHHHHHH";

        struct Field;

        // The IME's handle on the field (platformSpecificTextInputSession): its edits run through
        // ImeEditCommandScope on the transformed state; the state's IME listener reports back.
        class Session final : public ui::TextInputSession {
        public:
            explicit Session(Field& f) : field(f) {}
            Field& field;
            ui::ImeOptions Options() const override;
            ui::TextInputSnapshot Snapshot() const override;
            bool BeginBatchEdit() override;
            bool EndBatchEdit() override;
            void CommitText(std::u16string_view t, int c, bool suggestion) override;
            void SetComposingText(std::u16string_view t, int c, const std::vector<text::Range<text::SpanStyle>>* annotations,
                                  bool suggestion) override;
            void SetComposingRegion(int s, int e) override;
            void FinishComposingText() override;
            void DeleteSurroundingText(int b, int a) override;
            void DeleteSurroundingTextInCodePoints(int b, int a) override;
            void SetSelection(int s, int e) override;
            void PerformImeAction(ui::ImeAction a) override;
            void UpdateDirectTouchInteraction(bool direct) override;
            void RequestCursorUpdates(int mode) override;
            bool CommitContent(const ui::ClipEntry& clip, const ui::ClipMetadata& metadata, const std::optional<std::string>& linkUri,
                               int permission) override;
            int PerformHandwritingGesture(const ui::HandwritingGesture& g) override;
            bool PreviewHandwritingGesture(const ui::HandwritingGesture& g) override;
            void CancelHandwritingPreview() override;
            HandwritingGestureContext GestureContext() const;
        };

        // CursorAnimationState: visible for 500 ms, hidden for 500 ms, restarting visible on
        // every change of the text or selection while focused.
        struct CursorAnimation {
            rt::State<float> alpha{ 0.0f };
            rt::Job job;
            // With blinking off (LocalCursorBlinkEnabled) the cursor stays visible.
            void SnapToVisibleAndAnimate(bool animate = true) {
                job.Cancel();
                alpha.Set(1.0f);
                if (animate) job = rt::Launch(rt::Dispatcher::Main(), Run(this));
            }
            void CancelAndHide() {
                job.Cancel();
                if (alpha.Peek() != 0.0f) alpha.Set(0.0f);
            }
            static rt::Task<> Run(CursorAnimation* self) {
                for (;;) {
                    co_await rt::Delay(500);
                    self->alpha.Set(0.0f);
                    co_await rt::Delay(500);
                    self->alpha.Set(1.0f);
                }
            }
        };

        // What the field's modifiers share; remembered per call site, rebuilt (transformed state,
        // layout state, selection state) when the state or its transformations change.
        struct Field {
            TextFieldState* state = nullptr;
            bool singleLine = false;
            CodepointTransformationPtr codepoint;
            OutputTransformationPtr output;
            std::unique_ptr<TransformedTextFieldState> transformed;
            std::unique_ptr<TextLayoutState> layout;
            // rememberPlatformSelectionBehaviors(EditableText, the style's locales): outlives
            // the selection state that uses it.
            std::unique_ptr<PlatformSelectionBehaviors> behaviors;
            std::unique_ptr<TextFieldSelectionState> selection;
            // The core node's TextContextMenuToolbarHandlerNode (the selection state's
            // ToolbarRequester).
            TextContextMenuToolbarHandler toolbar;
            std::unique_ptr<ImeEditCommandScope> ime;
            TextFieldKeyEventHandler keys;
            Session session{ *this };

            BasicTextFieldOptions options;
            KeyboardOptions keyboard;                      // resolved (the input transformation's under)
            std::shared_ptr<MutableInteractionSource> interaction;
            ui::FocusRequester requester;
            ui::Owner* owner = nullptr;
            bool nodeFocused = false;                      // focusableNode.focusState.isFocused
            bool windowFocused = true;                     // windowInfo.isWindowFocused
            bool inputSession = false;                     // inputSessionJob != null
            bool textInputBlocked = false;                 // under BlockPlatformTextInput
            const ModifierNode* decoratorNode = nullptr;   // for the receiveContent modifier local
            bool acceptsContent = false;                   // a receiver was around when the session started
            // stylusHandwritingTrigger (replay 1): set by a handwriting stroke, replayed to each
            // session after its first frame, cleared when the session goes.
            bool handwritingPending = false;
            rt::Job handwritingJob;
            void TriggerStylusHandwriting() {
                handwritingPending = true;
                if (inputSession) StartHandwritingAfterFrame();
            }
            void StartHandwritingAfterFrame() {
                handwritingJob.Cancel();
                handwritingJob = rt::Launch(rt::Dispatcher::Main(), StartHandwriting(this, alive));
            }
            static rt::Task<> StartHandwriting(Field* f, std::weak_ptr<char> weak) {
                co_await rt::NextFrame{};
                if (weak.expired() || !f->inputSession || !f->handwritingPending || !f->owner) co_return;
                f->owner->StartStylusHandwriting(f->session);
            }
            // The toolbar handler's onShow: the clipboard's state, the selection classified.
            static rt::Task<> OnToolbarShow(Field* f, std::weak_ptr<char> weak) {
                f->selection->UpdateClipboardEntry();
                if (f->behaviors) {
                    const TextFieldCharSequence v = f->transformed->VisualText();
                    co_await f->behaviors->OnShowSelectionToolbar(v.text, v.selection);
                    if (weak.expired()) co_return;
                }
                f->selection->SetTextToolbarShown(true);
            }
            void UpdateBehaviors(const std::optional<std::string>& localeList) {
                std::vector<std::string> locales = SplitLocaleList(localeList);
                if (behaviors && behaviors->GetOwner() == owner && behaviors->Locales() == locales) return;
                if (selection) selection->platformSelectionBehaviors = nullptr;
                behaviors = PlatformSelectionBehaviors::Create(owner, ui::SelectedTextType::EditableText, std::move(locales));
                if (selection) selection->platformSelectionBehaviors = behaviors.get();
            }
            ReceiveContentConfiguration* ReceiveContent() const { return decoratorNode ? FindReceiveContentConfiguration(*decoratorNode) : nullptr; }
            // The selection and composition the IME last heard of (updateSelection on change).
            TextRange imeSelection;
            std::optional<TextRange> imeComposition;
            // CursorAnchorInfoController
            bool monitorEnabled = false;
            bool includeInsertionMarker = false, includeCharacterBounds = false, includeEditorBounds = false, includeLineBounds = false;
            std::optional<ui::CursorAnchorInfo> lastMonitored;
            bool anchorCheckPosted = false;
            int imeListener = -1;
            std::weak_ptr<const char> stateAlive;
            // The core's cursor restarts and the decorator's semantics, by token.
            // autofillHighlightOn, and the text autofill set (the next change turns it off).
            rt::State<bool> autofillHighlight{ false };
            std::optional<std::u16string> autofillHighlightText;
            void SetAutofillHighlight() {
                autofillHighlightText = transformed->UntransformedText().text;
                autofillHighlight.Set(true);
            }
            std::map<int, std::function<void()>> changeObservers;
            int nextObserver = 0;
            int Observe(std::function<void()> fn) { changeObservers.emplace(nextObserver, std::move(fn)); return nextObserver++; }

            ~Field() { DisposeInputSession(); RemoveListener(); }

            bool IsFocused() const { return nodeFocused && windowFocused; }
            bool Editable() const { return options.enabled && !options.readOnly; }

            void Bind(TextFieldState& s, bool single, const CodepointTransformationPtr& cp, const OutputTransformationPtr& out,
                      const InputTransformationPtr& input) {
                if (transformed && state == &s && singleLine == single && codepoint == cp && output == out) return;
                const bool hadSession = inputSession;
                DisposeInputSession();
                RemoveListener();
                state = &s;
                singleLine = single;
                codepoint = cp;
                output = out;
                if (codepoint == nullptr) {
                    // isBasicTextFieldOutputTransformationOnlyEnabled: a single line field turns
                    // line breaks into spaces after the given output transformation.
                    OutputTransformationPtr applied = singleLine ? SingleLineOutputTransformation(output) : output;
                    transformed = std::make_unique<TransformedTextFieldState>(s, input, nullptr, applied);
                } else {
                    transformed = std::make_unique<TransformedTextFieldState>(s, input, codepoint, output);
                }
                layout = std::make_unique<TextLayoutState>();
                selection = std::make_unique<TextFieldSelectionState>(*transformed, *layout);
                selection->owner = owner;
                selection->receiveContentConfiguration = [this] { return ReceiveContent(); };
                selection->platformSelectionBehaviors = behaviors.get();
                selection->toolbarHandler = &toolbar;
                toolbar.onShow = [this] { return OnToolbarShow(this, alive); };
                toolbar.onHide = [this] { selection->SetTextToolbarShown(false); };
                toolbar.computeContentBounds = [this]() -> std::optional<Rect> { return selection->DerivedVisibleContentBounds().value_or(Rect{}); };
                ime = std::make_unique<ImeEditCommandScope>(*transformed);
                stateAlive = s.AliveToken();
                imeListener = s.AddNotifyImeListener([this](const TextFieldCharSequence&, const TextFieldCharSequence&, bool restartIme) {
                    if (inputSession && owner) {
                        const TextFieldCharSequence visual = transformed->VisualText();
                        const bool changed = visual.selection != imeSelection || visual.composition != imeComposition;
                        imeSelection = visual.selection;
                        imeComposition = visual.composition;
                        owner->NotifyTextInputUpdate(session, restartIme, changed);
                    }
                    selection->OnContentChanged();
                    // observeUntransformedTextChanges: the first change after a fill.
                    if (autofillHighlightText && transformed->UntransformedText().text != *autofillHighlightText) {
                        autofillHighlightText.reset();
                        autofillHighlight.Set(false);
                    }
                    for (auto it = changeObservers.begin(); it != changeObservers.end();) {
                        const int token = it->first;
                        it->second();
                        it = changeObservers.upper_bound(token);   // an observer may remove itself
                    }
                });
                if (hadSession && Editable()) StartInputSession(false);
            }
            void RemoveListener() {
                if (imeListener >= 0 && state && !stateAlive.expired()) state->RemoveNotifyImeListener(imeListener);
                imeListener = -1;
            }

            void StartInputSession(bool fromTap) {
                if (textInputBlocked) return;   // the interceptor never starts the request
                if (!fromTap && !keyboard.showKeyboardOnFocus.value_or(true)) return;
                if (!owner) return;
                inputSession = true;
                const TextFieldCharSequence visual = transformed->VisualText();
                imeSelection = visual.selection;
                imeComposition = visual.composition;
                acceptsContent = ReceiveContent() != nullptr;
                monitorEnabled = false;
                lastMonitored.reset();
                owner->StartTextInput(session);   // replaces any session (and shows the keyboard)
                if (handwritingPending) StartHandwritingAfterFrame();
            }
            void DisposeInputSession() {
                if (inputSession && owner) owner->StopTextInput(session);
                inputSession = false;
                monitorEnabled = false;
                handwritingPending = false;   // resetReplayCache
                handwritingJob.Cancel();
            }

            // --- CursorAnchorInfoController -----------------------------------------------
            void RequestCursorUpdates(int mode) {
                // The host resolved the filters for its API level (none before 33: the marker and
                // character bounds; no filter on 33+: everything that level has).
                const bool immediate = mode & 1, monitor = mode & 2;
                const bool marker = mode & 16, chars = mode & 8, editor = mode & 4, lines = mode & 32;
                includeInsertionMarker = marker;
                includeCharacterBounds = chars;
                includeEditorBounds = editor;
                includeLineBounds = lines;
                if (immediate && owner)
                    if (const auto info = CalculateCursorAnchorInfo()) owner->UpdateCursorAnchorInfo(session, *info);
                if (monitor && !monitorEnabled) lastMonitored = CalculateCursorAnchorInfo();   // snapshotFlow's first value is dropped
                monitorEnabled = monitor;
            }
            // The monitor's snapshotFlow: after a layout or move, send what changed.
            void ScheduleCursorAnchorCheck() {
                if (!monitorEnabled || anchorCheckPosted) return;
                anchorCheckPosted = true;
                std::weak_ptr<char> weak = alive;
                rt::Dispatcher::Main().Post([this, weak] {
                    if (weak.expired()) return;
                    anchorCheckPosted = false;
                    if (!monitorEnabled || !owner) return;
                    const auto info = CalculateCursorAnchorInfo();
                    if (!info || info == lastMonitored) return;
                    lastMonitored = info;
                    owner->UpdateCursorAnchorInfo(session, *info);
                });
            }
            std::optional<ui::CursorAnchorInfo> CalculateCursorAnchorInfo() const {
                const auto& tl = layout->textLayoutNodeCoordinates;
                const auto& core = layout->coreNodeCoordinates;
                const auto& deco = layout->decoratorNodeCoordinates;
                if (!tl || !tl->IsAttached() || !core || !core->IsAttached() || !deco || !deco->IsAttached()) return std::nullopt;
                const auto result = layout->PeekLayoutResult();
                if (!result) return std::nullopt;
                const TextFieldCharSequence text = transformed->VisualText();
                auto boundsIn = [&](const selection::Coordinates& c) {
                    const Rect v = selection::VisibleBounds(c);
                    const Offset o = selection::LocalPositionOf(*tl, c, {});
                    return Rect{ v.left + o.x, v.top + o.y, v.right + o.x, v.bottom + o.y };
                };
                const Rect inner = boundsIn(*core), decoration = boundsIn(*deco);
                ui::CursorAnchorInfo info;
                info.matrix = tl->ToRoot();
                info.selStart = text.selection.Min();
                info.selEnd = text.selection.Max();
                auto containsInclusive = [](const Rect& r, float x, float y) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
                const text::TextLayoutResult& l = *result;
                if (includeInsertionMarker && info.selStart >= 0) {
                    const CursorRect c = l.Cursor(info.selStart);
                    const float x = std::clamp(c.x, 0.0f, float(l.Size().width));
                    const bool top = containsInclusive(inner, x, c.top), bottom = containsInclusive(inner, x, c.bottom);
                    int flags = 0;
                    if (top || bottom) flags |= ui::CursorAnchorInfo::kHasVisibleRegion;
                    if (!top || !bottom) flags |= ui::CursorAnchorInfo::kHasInvisibleRegion;
                    if (l.BidiRunDirection(info.selStart) == text::ResolvedTextDirection::Rtl) flags |= ui::CursorAnchorInfo::kIsRtl;
                    info.hasInsertionMarker = true;
                    // Compose passes the cursor's bottom as the baseline.
                    info.markerX = x; info.markerTop = c.top; info.markerBaseline = c.bottom; info.markerBottom = c.bottom;
                    info.markerFlags = flags;
                }
                if (includeCharacterBounds && text.composition) {
                    const int start = text.composition->Min(), end = text.composition->Max();
                    if (start >= 0 && start < end) {
                        info.composingStart = start;
                        info.composingText = text.text.substr(std::size_t(start), std::size_t(end - start));
                        for (int i = start; i < end; ++i) {
                            const RectF b = l.BoundingBox(i);
                            const Rect r{ b.x, b.y, b.x + b.w, b.y + b.h };
                            int flags = 0;
                            if (inner.left < r.right && r.left < inner.right && inner.top < r.bottom && r.top < inner.bottom)
                                flags |= ui::CursorAnchorInfo::kHasVisibleRegion;
                            if (!containsInclusive(inner, r.left, r.top) || !containsInclusive(inner, r.right, r.bottom))
                                flags |= ui::CursorAnchorInfo::kHasInvisibleRegion;
                            if (l.BidiRunDirection(i) == text::ResolvedTextDirection::Rtl) flags |= ui::CursorAnchorInfo::kIsRtl;
                            info.characterBounds.push_back({ i, r.left, r.top, r.right, r.bottom, flags });
                        }
                    }
                }
                if (includeEditorBounds) {
                    info.hasEditorBounds = true;
                    info.editorBounds = { decoration.left, decoration.top, decoration.right, decoration.bottom };
                }
                if (includeLineBounds && inner.right > inner.left && inner.bottom > inner.top) {
                    const int last = std::max(0, l.LineCount() - 1);
                    const int first = std::clamp(l.LineForVerticalPosition(inner.top), 0, last);
                    const int lastVisible = std::clamp(l.LineForVerticalPosition(inner.bottom), 0, last);
                    for (int i = first; i <= lastVisible; ++i)
                        info.visibleLineBounds.push_back({ l.LineLeft(i), l.LineTop(i), l.LineRight(i), l.LineBottom(i) });
                }
                return info;
            }
            std::shared_ptr<char> alive = std::make_shared<char>(0);

            // onIsFocusedUpdated: with isTextFieldWaitWindowFocusForInputSessionEnabled the
            // session starts once both the field and its window have focus.
            void OnIsFocusedUpdated() {
                selection->isWindowAndTextFieldFocused = IsFocused();
                if (IsFocused()) {
                    if (Editable() && !inputSession) StartInputSession(false);
                    selection->StartToolbarAndHandlesVisibilityObserver();
                } else {
                    selection->StopToolbarAndHandlesVisibilityObserver();
                }
            }

            void OnFocusChange(bool focused) {
                if (focused == nodeFocused) return;
                nodeFocused = focused;
                if (focused) {
                    // applyCurrentInputMode
                    if (owner && owner->GetInputMode() != ui::InputMode::Touch) selection->SetDirectTouchInteraction(false);
                } else {
                    DisposeInputSession();
                    // Only losing focus (not window focus) commits the composition.
                    transformed->EditUntransformedTextAsUser([](TextFieldBuffer& b) { b.CommitComposition(); });
                    // Deselect when losing focus, even when read-only.
                    transformed->CollapseSelectionToMax();
                }
                OnIsFocusedUpdated();
            }

            void RequestFocus() { requester.RequestFocus(); }
            void ShowKeyboard() {
                if (inputSession) { if (owner) owner->ShowSoftKeyboard(); }
                else StartInputSession(true);
            }

            bool DefaultKeyboardAction(ui::ImeAction a) {
                switch (a) {
                    case ui::ImeAction::Next: if (owner) owner->MoveFocus(ui::FocusDirection::Next); return true;
                    case ui::ImeAction::Previous: if (owner) owner->MoveFocus(ui::FocusDirection::Previous); return true;
                    case ui::ImeAction::Done: if (owner) owner->HideSoftKeyboard(); return true;
                    default: return false;
                }
            }
            bool OnImeActionPerformed(ui::ImeAction a) {
                if (a == ui::ImeAction::None || a == ui::ImeAction::Default || !options.onKeyboardAction) return DefaultKeyboardAction(a);
                options.onKeyboardAction([this, a] { DefaultKeyboardAction(a); });
                return true;
            }
            ui::ImeAction ImeActionOrDefault() const { return keyboard.imeAction.value_or(ui::ImeAction::Default); }

            // decoratorNodeCoordinates.localBoundingBoxOf(textLayoutNodeCoordinates).height
            float VisibleTextLayoutHeight() const { return layout->VisibleTextLayoutRectInDecorator().Height(); }

            TextFieldKeyEventHandler::Callbacks KeyCallbacks() {
                TextFieldKeyEventHandler::Callbacks cb;
                cb.clipboard = [this](KeyCommand c, bool hardware) {
                    if (c == KeyCommand::Copy) selection->Copy(false);
                    else if (c == KeyCommand::Cut) selection->Cut();
                    else if (c == KeyCommand::Paste) selection->Paste(hardware);
                };
                cb.onSubmit = [this] { return OnImeActionPerformed(ImeActionOrDefault()); };
                cb.deselect = [this] { selection->Deselect(); };
                cb.showKeyboard = [this] { ShowKeyboard(); };
                cb.endDirectTouchInteraction = [this] { selection->SetDirectTouchInteraction(false); };
                return cb;
            }
        };

        ui::ImeOptions Session::Options() const {
            ui::ImeOptions o = field.keyboard.ToImeOptions(field.singleLine);
            o.acceptsContent = field.acceptsContent;
            // shouldEnableStylusHandwriting
            o.stylusHandwriting = ui::IsStylusHandwritingSupported() && !ui::IsPasswordKeyboard(o.keyboardType);
            return o;
        }
        bool Session::CommitContent(const ui::ClipEntry& clip, const ui::ClipMetadata& metadata, const std::optional<std::string>& linkUri,
                                    int permission) {
            ReceiveContentConfiguration* c = field.ReceiveContent();
            if (!c) return false;
            return c->OnCommitContent(std::make_shared<TransferableContent>(clip, metadata, TransferableContent::Source::Keyboard, linkUri, permission));
        }
        ui::TextInputSnapshot Session::Snapshot() const {
            const TextFieldCharSequence v = field.transformed->VisualText();
            ui::TextInputSnapshot s{ v.text, v.selection.Min(), v.selection.Max(), -1, -1 };
            if (v.composition) { s.compStart = v.composition->Min(); s.compEnd = v.composition->Max(); }
            return s;
        }
        bool Session::BeginBatchEdit() { return field.ime->BeginBatchEdit(); }
        bool Session::EndBatchEdit() { return field.ime->EndBatchEdit(); }
        void Session::CommitText(std::u16string_view t, int c, bool suggestion) { field.ime->CommitText(t, c, suggestion); }
        void Session::SetComposingText(std::u16string_view t, int c, const std::vector<text::Range<text::SpanStyle>>* annotations, bool suggestion) {
            field.ime->SetComposingText(t, c, annotations, suggestion);
        }
        void Session::SetComposingRegion(int s, int e) { field.ime->SetComposingRegion(s, e); }
        void Session::FinishComposingText() { field.ime->FinishComposingText(); }
        void Session::DeleteSurroundingText(int b, int a) { field.ime->DeleteSurroundingText(b, a); }
        void Session::DeleteSurroundingTextInCodePoints(int b, int a) { field.ime->DeleteSurroundingTextInCodePoints(b, a); }
        void Session::SetSelection(int s, int e) {
            field.ime->SetSelection(s, e);
            field.selection->SetDirectTouchInteraction(false);
        }
        void Session::PerformImeAction(ui::ImeAction a) { field.OnImeActionPerformed(a); }
        HandwritingGestureContext Session::GestureContext() const {
            ui::Owner* o = field.owner;
            Field* f = &field;
            return { *field.transformed, *field.layout, o ? o->Text() : nullptr, o ? o->Config().handwritingGestureLineMargin : 16.0f,
                     [f] { f->selection->UpdateTextToolbarState(TextToolbarState::Selection); } };
        }
        int Session::PerformHandwritingGesture(const ui::HandwritingGesture& g) { return foundation::PerformHandwritingGesture(GestureContext(), g); }
        bool Session::PreviewHandwritingGesture(const ui::HandwritingGesture& g) { return foundation::PreviewHandwritingGesture(GestureContext(), g); }
        void Session::CancelHandwritingPreview() { foundation::CancelHandwritingPreview(*field.transformed); }
        void Session::UpdateDirectTouchInteraction(bool direct) { field.selection->SetDirectTouchInteraction(direct); }
        void Session::RequestCursorUpdates(int mode) { field.RequestCursorUpdates(mode); }

        ui::Owner* OwnerOf(const ModifierNode& n) { return n.Owner() ? n.Owner()->GetOwner() : nullptr; }

        // --- TextFieldDecoratorModifier -----------------------------------------------------
        // The node part of the decorator: the coordinates, the attach / detach of the session,
        // and the semantics (the focus, key, pointer and scroll parts are their own modifiers in
        // the same chain).
        struct DecoratorParams {
            std::shared_ptr<Field> field;
            bool enabled = true, readOnly = false, isPassword = false;
            ui::ImeAction imeAction = ui::ImeAction::Default;
            InputTransformationPtr filter;
            Color autofillHighlightColor = Color(0x4DFFEB3Bu);
            bool operator==(const DecoratorParams&) const = default;
        };
        // textFieldDragAndDropNode's target: text (or, with a receiveContent around, anything)
        // dropped at the cursor the drag moves.
        struct FieldDropTarget final : ui::DragAndDropTarget {
            std::shared_ptr<Field> field;
            std::uint64_t enter = 0;
            void EmitDragExit() {
                if (enter && field->interaction) field->interaction->End(Interaction::Kind::DragAndDropHover, enter);
                enter = 0;
            }
            void OnEntered(const ui::DragAndDropEvent&) override {
                if (field->interaction) enter = field->interaction->Start(Interaction::Kind::DragAndDropHover);
                if (auto* c = field->ReceiveContent()) c->Listener().OnDragEnter();
            }
            void OnMoved(const ui::DragAndDropEvent& e) override {
                const Offset positionOnTextField = field->layout->FromWindowToDecoration(e.positionInRoot);
                const int cursor = field->layout->GetOffsetForPosition(positionOnTextField);
                if (cursor >= 0) field->transformed->SelectCharsIn(TextRange(cursor));
                field->selection->UpdateHandleDragging(TextFieldHandle::Cursor, positionOnTextField);
            }
            bool OnDrop(const ui::DragAndDropEvent& e) override {
                if (field->ReceiveContent()) DragAndDropRequestPermission(e);
                EmitDragExit();
                field->selection->ClearHandleDragging();
                auto content = TransferableContentOf(e);
                std::optional<std::string> plainText = content->ReadPlainText();
                if (auto* c = field->ReceiveContent()) {
                    const TransferableContentPtr remaining = c->Listener().OnReceive(content);
                    plainText = remaining ? remaining->ReadPlainText() : std::nullopt;
                }
                if (plainText) field->transformed->ReplaceSelectedText(ToUtf16(*plainText));
                return true;
            }
            void OnExited(const ui::DragAndDropEvent&) override {
                EmitDragExit();
                field->selection->ClearHandleDragging();
                if (auto* c = field->ReceiveContent()) c->Listener().OnDragExit();
            }
            void OnEnded(const ui::DragAndDropEvent&) override { EmitDragExit(); }
        };

        struct DecoratorNode final : ui::DragAndDropNode, ui::SemanticsModifier, ui::GlobalPositionAwareModifier, DrawModifier {
            DecoratorParams params;
            FieldDropTarget m_DropTarget;
            explicit DecoratorNode(DecoratorParams p) : params(std::move(p)) {
                // hintMediaTypes: any type with a receiver around, text otherwise.
                onDropTargetValidate = [this](const ui::DragAndDropEvent& e) -> ui::DragAndDropTarget* {
                    m_DropTarget.field = params.field;
                    if (params.field->ReceiveContent()) return &m_DropTarget;
                    ui::ClipMetadata meta{ "", e.mimeTypes };
                    return meta.HasMimeType("text/*") ? &m_DropTarget : nullptr;
                };
            }
            DrawModifier* AsDraw() override { return this; }
            // The autofill highlight over the content (and any background the decoration drew).
            void Draw(ContentDrawScope& s) override {
                s.DrawContent();
                if (params.field->autofillHighlight.Get()) s.DrawRect(params.autofillHighlightColor, { 0, 0 }, s.GetSize());
            }
            ui::SemanticsModifier* AsSemantics() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }

            void OnAttach() override {
                Field& f = *params.field;
                f.owner = DragAndDropNode::OwnerOf();
                f.selection->owner = f.owner;
                f.decoratorNode = this;
                f.UpdateBehaviors(f.options.textStyle.span.localeList);
                // requestAutofillAction (the toolbar's Autofill item).
                auto alive = AliveToken();
                f.selection->requestAutofill = [this, alive] {
                    if (alive.expired() || !Owner() || !Owner()->GetOwner()) return;
                    Owner()->GetOwner()->RequestAutofill(*Owner());
                };
                Observe();
            }
            void OnDetach() override {
                DragAndDropNode::OnDetach();
                Field& f = *params.field;
                f.DisposeInputSession();
                f.selection->StopToolbarAndHandlesVisibilityObserver();
                if (f.decoratorNode == this) {
                    f.decoratorNode = nullptr;
                    f.selection->requestAutofill = nullptr;
                }
                Unobserve();
            }
            void OnParamsChanged() {
                Unobserve();
                Observe();
                InvalidateSemantics();
            }
            void Observe() {
                Field& f = *params.field;
                auto alive = AliveToken();
                m_Observer = f.Observe([this, alive] { if (!alive.expired()) InvalidateSemantics(); });
                m_ObservedField = params.field;
            }
            void Unobserve() {
                if (auto f = m_ObservedField.lock()) f->changeObservers.erase(m_Observer);
                m_Observer = -1;
            }
            void OnGloballyPositioned() override {
                params.field->layout->decoratorNodeCoordinates = selection::Coordinates{ Owner(), this, AliveToken() };
                params.field->ScheduleCursorAnchorCheck();
            }

            void ApplySemantics(ui::SemanticsConfiguration& c) override {
                Field& f = *params.field;
                TransformedTextFieldState& state = *f.transformed;
                c.mergeDescendants = true;   // shouldMergeDescendantSemantics
                // Only password fields announce their untransformed value as the editable text.
                const TextFieldCharSequence text = params.isPassword ? state.UntransformedText() : state.VisualText();
                c.editableText = ToUtf8(text.text);
                c.textSelection = std::make_pair(text.selection.start, text.selection.end);
                if (!params.enabled) c.disabled = true;
                if (params.isPassword) c.password = true;
                const bool editable = params.enabled && !params.readOnly;
                c.isEditable = editable;
                auto field = params.field;
                std::weak_ptr<Field> weak = field;
                // Password or not, inputText tells the untransformed text. The developer sets
                // contentType; the field fills in the rest of autofill's semantics.
                c.inputText = ToUtf8(state.UntransformedText().text);
                c.contentDataType = ui::autofill::kTypeText;
                c.fillableData = ui::FillableData::FromText(text.text);
                c.onFillData = [weak, editable](const ui::FillableData& d) {
                    auto f = weak.lock();
                    if (!f || !editable) return false;
                    if (d.kind == ui::FillableData::Kind::Text) f->transformed->ReplaceAll(ToUtf16(d.text));
                    f->SetAutofillHighlight();
                    return true;
                };
                switch (f.keyboard.keyboardType.value_or(ui::KeyboardType::Text)) {
                    case ui::KeyboardType::Email: c.contentType = ui::ContentType::EmailAddress().hints; break;
                    case ui::KeyboardType::Password:
                    case ui::KeyboardType::NumberPassword: c.contentType = ui::ContentType::Password().hints; break;
                    case ui::KeyboardType::Phone: c.contentType = ui::ContentType::PhoneNumber().hints; break;
                    default: break;
                }
                c.getTextLayoutResult = [weak]() -> std::shared_ptr<const text::TextLayoutResult> {
                    auto f = weak.lock();
                    return f ? f->layout->PeekLayoutResult() : nullptr;
                };
                if (editable) {
                    c.setText = [weak](const std::string& t) {
                        auto f = weak.lock();
                        if (!f || !f->Editable()) return false;
                        f->transformed->ReplaceAll(ToUtf16(t));
                        return true;
                    };
                    c.insertTextAtCursor = [weak](const std::string& t) {
                        auto f = weak.lock();
                        if (!f || !f->Editable()) return false;
                        // Finish composing first: the IME may have set a composition.
                        f->transformed->ReplaceSelectedText(ToUtf16(t), true);
                        return true;
                    };
                }
                const bool enabled = params.enabled;
                c.setSelection = [weak, enabled](int start, int end, bool relativeToOriginal) {
                    auto f = weak.lock();
                    if (!f) return false;
                    // Traversal mode (relativeToOriginal) selects in the original text, TalkBack's
                    // selection is in the transformed text.
                    const TextFieldCharSequence t = relativeToOriginal ? f->transformed->UntransformedText() : f->transformed->VisualText();
                    if (!enabled || std::min(start, end) < 0 || std::max(start, end) > t.Length()) return false;
                    if (start == t.selection.start && end == t.selection.end) return true;
                    f->selection->UpdateTextToolbarState(relativeToOriginal || start == end ? TextToolbarState::None : TextToolbarState::Selection);
                    if (relativeToOriginal) f->transformed->SelectUntransformedCharsIn(TextRange(start, end));
                    else f->transformed->SelectCharsIn(TextRange(start, end));
                    return true;
                };
                const ui::ImeAction action = params.imeAction;
                // EditorInfo.IME_ACTION_*
                static constexpr int kEditorAction[] = { 0, 1, 2, 3, 4, 7, 5, 6 };
                c.imeAction = kEditorAction[int(action)];
                c.onImeAction = [weak, action] {
                    if (auto f = weak.lock()) f->OnImeActionPerformed(action);
                    return true;
                };
                const bool readOnly = params.readOnly;
                c.onClick = [weak, readOnly] {
                    // Still provided when disabled, as the documentation asks.
                    auto f = weak.lock();
                    if (!f) return true;
                    if (!f->IsFocused()) f->RequestFocus();
                    else if (!readOnly && f->owner) f->owner->ShowSoftKeyboard();
                    return true;
                };
                c.onLongClick = [weak] {
                    auto f = weak.lock();
                    if (!f) return true;
                    if (!f->IsFocused()) f->RequestFocus();
                    f->selection->UpdateTextToolbarState(TextToolbarState::Selection);
                    return true;
                };
                if (!text.selection.Collapsed() && !params.isPassword) {
                    c.copyText = [weak] { if (auto f = weak.lock()) f->selection->Copy(); return true; };
                    if (editable) c.cutText = [weak] { if (auto f = weak.lock()) f->selection->Cut(); return true; };
                }
                if (editable) c.pasteText = [weak] { if (auto f = weak.lock()) f->selection->Paste(); return true; };
                if (params.filter) params.filter->ApplySemantics(c);
            }

        private:
            int m_Observer = -1;
            std::weak_ptr<Field> m_ObservedField;
        };

        // --- TextFieldSizeConstrainerNode (textFieldSize) -----------------------------------
        struct SizeParams {
            TextStyle style;
            int minLines = 1, maxLines = 1;
            bool useSingleLineHeightProvider = false, unboundedWidth = false;
            TextLayoutState* provider = nullptr;
            bool operator==(const SizeParams&) const = default;
        };
        struct SizeNode final : ModifierNode, LayoutModifier {
            SizeParams params;
            explicit SizeNode(SizeParams p) : params(std::move(p)) {}
            LayoutModifier* AsLayout() override { return this; }
            void OnParamsChanged() { m_Dirty = true; InvalidateMeasure(); }

            void ComputeDefaultSizeIfNeeded(const ui::Density& d, ui::LayoutDirection dir) {
                ui::Owner* owner = OwnerOf(*this);
                TextBackend* backend = owner ? owner->Text() : nullptr;
                const int generation = owner ? owner->FontGeneration() : 0;
                if (generation != m_Generation || !(d == m_Density) || dir != m_Direction) {
                    m_Generation = generation;
                    m_Density = d;
                    m_Direction = dir;
                    m_Dirty = true;
                }
                if (!m_Dirty || !backend) return;
                const int lines = params.minLines == 1 && (params.maxLines == 1 || params.maxLines == INT_MAX) ? 1 : 3;
                // paragraphForDefaultText: Ltr keeps the legacy behaviour.
                TextStyle style = text::ResolveDefaults(params.style, dir);
                style.paragraph.textDirection = text::TextDirection::Ltr;
                std::string s;
                for (int i = 0; i < lines; ++i) { if (i) s += '\n'; s += kEmptyTextReplacement; }
                text::ParagraphConstraints pc;
                pc.maxLines = lines;
                const text::MultiParagraph p(backend, text::AnnotatedString(s), style, d, dir, pc);
                if (lines == 1) {
                    m_MinLinesHeight = int(std::ceil(p.Height()));
                    m_MaxLinesHeight = params.maxLines == 1 ? m_MinLinesHeight : ui::Constraints::Infinity;
                } else {
                    const float first = p.LineHeight(0), second = p.LineHeight(1), third = p.LineHeight(2);
                    m_MinLinesHeight = HeightFromThreeLines(first, second, third, params.minLines);
                    m_MaxLinesHeight = HeightFromThreeLines(first, second, third, params.maxLines);
                }
                m_MinWidth = int(std::ceil(text::MeasureIntrinsics(backend, text::AnnotatedString(s), style, d, dir).maxWidth));
                m_Dirty = false;
            }
            static int HeightFromThreeLines(float top, float middle, float bottom, int limit) {
                if (limit == 1) return int(std::ceil(top + bottom - middle));
                if (limit == INT_MAX) return ui::Constraints::Infinity;
                return int(std::ceil(top + middle * float(std::max(0, limit - 2)) + bottom * float(std::min(1, limit - 1))));
            }

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                ComputeDefaultSizeIfNeeded(s, s.Direction());
                Constraints computed;
                if (params.useSingleLineHeightProvider) {
                    const float height = params.provider->HeightForSingleLineField();
                    const int heightPx = s.RoundToPx(Dp(height));
                    const int maxWidth = params.unboundedWidth ? ui::Constraints::Infinity : c.maxWidth;
                    computed = { std::min(m_MinWidth, maxWidth), maxWidth, height == 0 ? m_MinLinesHeight : heightPx,
                                 height == 0 ? m_MaxLinesHeight : heightPx };
                } else {
                    computed = { m_MinWidth, ui::Constraints::Infinity, std::clamp(m_MinLinesHeight, c.minHeight, c.maxHeight),
                                 std::clamp(m_MaxLinesHeight, c.minHeight, c.maxHeight) };
                }
                Placeable& p = m.Measure(c.Constrain(computed));
                return s.Layout(p.Width(), p.Height(), [&p](PlacementScope& ps) { ps.PlaceRelative(p, 0, 0); });
            }
            int MinIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override {
                ComputeDefaultSizeIfNeeded(s, s.Direction());
                if (m_MinLinesHeight == m_MaxLinesHeight) return m_MinLinesHeight;
                return std::clamp(LayoutModifier::MinIntrinsicHeight(s, m, width), m_MinLinesHeight, std::max(m_MinLinesHeight, m_MaxLinesHeight));
            }
            int MaxIntrinsicHeight(MeasureScope& s, Measurable& m, int width) override {
                ComputeDefaultSizeIfNeeded(s, s.Direction());
                if (m_MinLinesHeight == m_MaxLinesHeight) return m_MaxLinesHeight;
                return std::clamp(LayoutModifier::MaxIntrinsicHeight(s, m, width), m_MinLinesHeight, std::max(m_MinLinesHeight, m_MaxLinesHeight));
            }

            bool m_Dirty = true;
            int m_MinWidth = -1, m_MinLinesHeight = -1, m_MaxLinesHeight = -1;
            int m_Generation = -1;
            ui::Density m_Density{ -1, -1 };
            ui::LayoutDirection m_Direction = ui::LayoutDirection::Ltr;
        };

        // --- TextFieldCoreModifier ----------------------------------------------------------
        struct CoreParams {
            std::shared_ptr<Field> field;
            bool isFocused = false, isDragHovered = false;
            gfx::Brush cursorBrush;
            Color selectionBackground;
            bool writeable = true;
            ScrollState* scrollState = nullptr;
            Orientation orientation = Orientation::Vertical;
            // Identity of the field's rebuilt parts (a rebuild invalidates measurement).
            TransformedTextFieldState* transformed = nullptr;
            bool isTouchDragInProgress = false;
            bool operator==(const CoreParams& o) const {
                return field == o.field && isFocused == o.isFocused && isDragHovered == o.isDragHovered &&
                       isTouchDragInProgress == o.isTouchDragInProgress && cursorBrush == o.cursorBrush &&
                       selectionBackground == o.selectionBackground && writeable == o.writeable && scrollState == o.scrollState &&
                       orientation == o.orientation && transformed == o.transformed;
            }
        };
        bool IsSpecified(const gfx::Brush& b) { return !(b.kind == gfx::Brush::Kind::Solid && (b.colors.empty() || !b.colors.front().Specified())); }

        struct CoreNode final : ModifierNode, LayoutModifier, DrawModifier, ui::GlobalPositionAwareModifier {
            CoreParams params;
            explicit CoreNode(CoreParams p) : params(std::move(p)), m_Previous(params) {}
            LayoutModifier* AsLayout() override { return this; }
            DrawModifier* AsDraw() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }

            bool ShowCursor() const { return params.writeable && (params.isFocused || params.isDragHovered) && IsSpecified(params.cursorBrush); }

            void OnAttach() override {
                // The field may toggle the inner text field in and out of composition: start the
                // cursor right away when already focused.
                params.field->selection->isWindowAndTextFieldFocused = params.isFocused;
                if (params.isFocused && ShowCursor()) StartCursorJob();
                params.field->toolbar.Attach(OwnerOf(*this), this);
            }
            void OnDetach() override {
                params.field->toolbar.Detach();
                StopObserving();
                if (m_Cursor) m_Cursor->CancelAndHide();
                m_Magnifier.Detach();
            }
            void OnParamsChanged() {
                const CoreParams& prev = m_Previous;
                const bool previousShowCursor = prev.writeable && (prev.isFocused || prev.isDragHovered) && IsSpecified(prev.cursorBrush);
                const bool wasFocused = prev.isFocused;
                params.field->selection->isWindowAndTextFieldFocused = params.isFocused;
                if (!ShowCursor()) {
                    StopObserving();
                    if (m_Cursor) m_Cursor->CancelAndHide();
                } else if (!wasFocused || prev.transformed != params.transformed || !previousShowCursor) {
                    StartCursorJob();
                }
                if (prev.transformed != params.transformed || prev.scrollState != params.scrollState || prev.field != params.field) InvalidateMeasure();
                if (!(prev.cursorBrush == params.cursorBrush) || prev.selectionBackground != params.selectionBackground ||
                    prev.isFocused != params.isFocused || prev.isTouchDragInProgress != params.isTouchDragInProgress)
                    InvalidateDraw();
                m_Previous = params;
            }

            void StartCursorJob() {
                if (!m_Cursor) {
                    m_Cursor = std::make_unique<CursorAnimation>();
                    InvalidateDraw();   // draw did not read the alpha before
                }
                StopObserving();
                // Restart the animation whenever the text or the cursor changes (and now).
                auto alive = AliveToken();
                Field& f = *params.field;
                m_Observer = f.Observe([this, alive] { if (!alive.expired() && m_Cursor) m_Cursor->SnapToVisibleAndAnimate(BlinkEnabled()); });
                m_ObservedField = params.field;
                if (params.isFocused) m_Cursor->SnapToVisibleAndAnimate(BlinkEnabled());
            }
            bool BlinkEnabled() const {
                bool on = true;
                rt::Observation::Untracked([&] { on = ui::CurrentValueOf(*this, ui::LocalCursorBlinkEnabled); });
                return on;
            }
            void StopObserving() {
                if (auto f = m_ObservedField.lock()) f->changeObservers.erase(m_Observer);
                m_Observer = -1;
            }

            void OnGloballyPositioned() override {
                params.field->layout->coreNodeCoordinates = selection::Coordinates{ Owner(), this, AliveToken() };
                params.field->ScheduleCursorAnchorCheck();
            }

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Field& f = *params.field;
                const bool vertical = params.orientation == Orientation::Vertical;
                Constraints child = c;
                if (vertical) child.maxHeight = ui::Constraints::Infinity;
                else child.maxWidth = ui::Constraints::Infinity;
                Placeable& p = m.Measure(child);
                const int width = vertical ? p.Width() : std::min(p.Width(), c.maxWidth);
                const int height = vertical ? std::min(p.Height(), c.maxHeight) : p.Height();
                const TextRange selection = f.transformed->VisualText().selection;
                const ui::LayoutDirection dir = s.Direction();
                const ui::Density density = s;
                return s.Layout(width, height, [this, &p, width, height, vertical, selection, dir, density](PlacementScope& ps) {
                    // Bring the cursor back into view after the layout.
                    UpdateScrollState(vertical ? height : width, vertical ? p.Height() : p.Width(), selection, dir, density);
                    const int scroll = params.scrollState->Value();
                    if (vertical) ps.PlaceRelative(p, 0, -scroll);
                    else ps.PlaceRelative(p, -scroll, 0);
                });
            }

            int CalculateOffsetToFollow(TextRange curr, int containerSize, int textLayoutSize) const {
                if (!m_PreviousSelection || curr.end != m_PreviousSelection->end) return curr.end;
                if (curr.start != m_PreviousSelection->start) return curr.start;
                if (textLayoutSize != m_PreviousTextLayoutSize || containerSize != m_PreviousContainerSize) return curr.Min();
                return -1;
            }

            void UpdateScrollState(int containerSize, int textLayoutSize, TextRange currSelection, ui::LayoutDirection dir, const ui::Density& density) {
                Field& f = *params.field;
                ScrollState& scroll = *params.scrollState;
                scroll.SetBounds(textLayoutSize - containerSize, containerSize, textLayoutSize, false);
                const int offsetToFollow = CalculateOffsetToFollow(currSelection, containerSize, textLayoutSize);
                if (offsetToFollow < 0 || !ShowCursor()) return;
                const auto layout = f.layout->PeekLayoutResult();
                if (!layout) return;
                const Rect raw = f.selection->GetCursorRectInLayout(*layout, std::clamp(offsetToFollow, 0, layout->LayoutInput().text.Length()));
                // getCursorRectInScroller
                const float thickness = float(density.RoundToPx(Dp(kDefaultCursorThicknessDp)));
                const bool rtl = dir == ui::LayoutDirection::Rtl;
                Rect cursor = raw;
                cursor.left = rtl ? float(textLayoutSize) - raw.right : raw.left;
                cursor.right = std::min(rtl ? float(textLayoutSize) - raw.right + thickness : raw.left + thickness, float(textLayoutSize));
                const bool shouldBringIntoView = cursor.left != m_PreviousCursorRect.left || cursor.top != m_PreviousCursorRect.top ||
                                                 textLayoutSize != m_PreviousTextLayoutSize;
                if (!shouldBringIntoView && containerSize == m_PreviousContainerSize) return;
                const bool vertical = params.orientation == Orientation::Vertical;
                const float start = vertical ? cursor.top : cursor.left, end = vertical ? cursor.bottom : cursor.right;
                const float startVisible = float(scroll.Value()), endVisible = startVisible + float(containerSize);
                float difference = 0;
                if (end > endVisible) difference = end - endVisible;   // the bottom / end of the cursor
                else if (start < startVisible && end - start > float(containerSize)) difference = end - endVisible;   // too tall: its end
                else if (start < startVisible && end - start <= float(containerSize)) difference = start - startVisible;
                m_PreviousSelection = currSelection;
                m_PreviousCursorRect = cursor;
                m_PreviousContainerSize = containerSize;
                m_PreviousTextLayoutSize = textLayoutSize;
                // roundToNext; scrollBy respects the max value set above.
                const float target = std::isnan(difference) || std::isinf(difference) ? difference
                                     : difference > 0 ? std::ceil(difference) : std::floor(difference);
                const float scrolled = scroll.ConsumeScrollDelta(target);
                if (!shouldBringIntoView) return;
                Rect bring = raw;
                if (currSelection.Collapsed() && raw.Width() <= 0 && std::abs(target - scrolled) >= 1) bring.right = raw.right + 1;
                // The cursor rect from the text layout: bringIntoView handles RTL itself.
                std::weak_ptr<Field> weak = params.field;
                rt::Dispatcher::Main().Post([weak, bring] {
                    if (auto fl = weak.lock()) fl->layout->bringIntoViewRequester.BringIntoView(bring);
                });
            }

            void Draw(ContentDrawScope& s) override {
                s.DrawContent();
                Field& f = *params.field;
                const TextFieldCharSequence value = f.transformed->VisualText();
                const auto layout = f.layout->LayoutResult();
                if (!layout) return;
                // The text layout node is placed under the scroll: draw in its space.
                const Offset origin = f.layout->FromTextLayoutToCore({});
                s.Save();
                s.Translate(origin);
                if (value.highlight) DrawHighlight(s, *value.highlight, *layout);
                if (value.selection.Collapsed()) {
                    DrawTextLayout(s, *layout, layout->LayoutInput().style);
                    if (value.ShouldShowSelection()) DrawCursor(s);
                } else {
                    if (value.ShouldShowSelection()) {
                        for (const RectF& r : layout->SelectionRects(value.selection.Min(), value.selection.Max()))
                            s.DrawRect(params.selectionBackground, { r.x, r.y }, { r.w, r.h });
                    }
                    DrawTextLayout(s, *layout, layout->LayoutInput().style);
                }
                s.Restore();
                DrawMagnifier(s);
            }

            // textFieldMagnifierNode (TextFieldMagnifierNodeImpl28): visible while the field is
            // focused, drag-hovered or touch-dragged; centred on the dragged handle's line.
            void DrawMagnifier(ContentDrawScope& s) {
                const bool visible = params.isFocused || params.isDragHovered || params.isTouchDragInProgress;
                ui::Owner* owner = OwnerOf(*this);
                if (!visible || !owner || !owner->Magnifier().create) { m_Magnifier.Detach(); return; }
                Field& f = *params.field;
                const Offset anchor = selection::PositionInRoot(selection::Coordinates{ Owner(), this, AliveToken() });
                m_Magnifier.Update([&f](ui::IntSize size) { return MagnifierCenter(f, size); }, owner, anchor, s);
            }
            // calculateSelectionMagnifierCenterAndroid
            static Offset MagnifierCenter(Field& f, ui::IntSize magnifierSize) {
                constexpr Offset kUnspecified{ NAN, NAN };
                TextFieldSelectionState& sel = *f.selection;
                const Offset drag = sel.HandleDragPosition();
                const TextFieldCharSequence visual = f.transformed->VisualText();
                if (std::isnan(drag.x) || visual.text.empty()) return kUnspecified;
                const auto handle = sel.DraggingHandle();
                if (!handle) return kUnspecified;
                const int textOffset = *handle == TextFieldHandle::SelectionEnd ? visual.selection.end : visual.selection.start;
                const auto layout = f.layout->LayoutResult();
                if (!layout) return kUnspecified;
                const int line = layout->LineForOffset(textOffset);
                const float a = layout->LineLeft(line), b = layout->LineRight(line);
                const float centerX = std::clamp(drag.x, std::min(a, b), std::max(a, b));
                // Dragged too far from the text: hidden (Editor's rule).
                if (magnifierSize.width != 0 && std::abs(drag.x - centerX) > float(magnifierSize.width) / 2) return kUnspecified;
                const float top = layout->LineTop(line), bottom = layout->LineBottom(line);
                Offset offset{ centerX, (bottom - top) / 2 + top };
                const auto& coords = f.layout->textLayoutNodeCoordinates;
                if (coords && coords->IsAttached()) offset = CoerceIn(offset, selection::VisibleBounds(*coords));
                return f.layout->FromTextLayoutToCore(offset);
            }
            SelectionMagnifier m_Magnifier;

            void DrawHighlight(ContentDrawScope& s, const std::pair<TextHighlightType, TextRange>& highlight, const text::TextLayoutResult& layout) {
                const auto& [type, range] = highlight;
                if (range.Collapsed()) return;
                Color color = params.selectionBackground;
                if (type == TextHighlightType::HandwritingDeletePreview) {
                    // The text's colour at 20 % opacity.
                    const text::SpanStyle& span = layout.LayoutInput().style.span;
                    Color text = span.color.Specified() ? span.color : Color(0xFF000000u);
                    if (span.brush && span.brush->kind == gfx::Brush::Kind::Solid && !span.brush->colors.empty()) text = span.brush->colors.front();
                    color = text.WithAlpha(text.Alpha() * 0.2f);
                }
                for (const RectF& r : layout.SelectionRects(range.Min(), range.Max())) s.DrawRect(color, { r.x, r.y }, { r.w, r.h });
            }

            void DrawCursor(ContentDrawScope& s) {
                // Alpha is read first so the draw observes it.
                const float alpha = m_Cursor ? m_Cursor->alpha.Get() : 0.0f;
                if (alpha == 0 || !ShowCursor()) return;
                const Rect r = params.field->selection->GetCursorRect();
                s.DrawRect(params.cursorBrush, { r.left, r.top }, { r.Width(), r.Height() }, alpha);
            }

            CoreParams m_Previous;
            std::unique_ptr<CursorAnimation> m_Cursor;
            int m_Observer = -1;
            std::weak_ptr<Field> m_ObservedField;
            std::optional<TextRange> m_PreviousSelection;
            Rect m_PreviousCursorRect{ -1, -1, -1, -1 };
            int m_PreviousTextLayoutSize = 0, m_PreviousContainerSize = 0;
        };

        // --- TextFieldTextLayoutModifier ----------------------------------------------------
        struct TextLayoutParams {
            std::shared_ptr<Field> field;
            TransformedTextFieldState* transformed = nullptr;
            TextStyle style;
            bool singleLine = false;
            bool isKeyboardTypePhone = false;
            bool operator==(const TextLayoutParams&) const = default;
        };
        struct TextLayoutNode final : ModifierNode, LayoutModifier, ui::GlobalPositionAwareModifier {
            TextLayoutParams params;
            explicit TextLayoutNode(TextLayoutParams p) : params(std::move(p)) { Update(); }
            LayoutModifier* AsLayout() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            void OnParamsChanged() { Update(); InvalidateMeasure(); }
            void Update() {
                Field& f = *params.field;
                f.layout->UpdateNonMeasureInputs(*f.transformed, params.style, params.singleLine, !params.singleLine, params.isKeyboardTypePhone);
            }
            void OnGloballyPositioned() override {
                params.field->layout->textLayoutNodeCoordinates = selection::Coordinates{ Owner(), this, AliveToken() };
                params.field->ScheduleCursorAnchorCheck();
                params.field->selection->OnContentChanged();
            }
            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& c) override {
                Field& f = *params.field;
                ui::Owner* owner = OwnerOf(*this);
                f.selection->density = s.density;
                const auto result = f.layout->LayoutWithNewMeasureInputs(owner ? owner->Text() : nullptr, s, s.Direction(),
                                                                         owner ? owner->FontGeneration() : 0, c);
                const ui::IntSize size = result->Size();
                Placeable& p = m.Measure(ui::Constraints::Fixed(size.width, size.height));
                // A single line field's height follows its first line, so taller characters (an
                // emoji, a tall script) are not cut.
                f.ScheduleCursorAnchorCheck();
                f.layout->SetHeightForSingleLineField(params.singleLine ? std::ceil(result->LineBottom(0)) / s.density : 0.0f);
                return s.Layout(size.width, size.height, [&p](PlacementScope& ps) { ps.Place(p, 0, 0); },
                                { { &FirstBaseline, int(std::lround(result->FirstBaseline())) }, { &LastBaseline, int(std::lround(result->LastBaseline())) } });
            }
        };

    }

    void BasicTextField(TextFieldState& state, const Modifier& modifier, BasicTextFieldOptions options, rt::Key key) {
        auto& comp = *rt::Composition::Current();
        if (options.lineLimits.minHeightInLines <= 0 || options.lineLimits.maxHeightInLines <= 0)
            throw std::invalid_argument("both minLines and maxLines must be greater than zero");
        if (options.lineLimits.minHeightInLines > options.lineLimits.maxHeightInLines)
            throw std::invalid_argument("minLines must be less than or equal to maxLines");
        const bool singleLine = options.lineLimits == TextFieldLineLimits::SingleLine();
        const ui::LayoutDirection layoutDirection = comp.Read(ui::LocalLayoutDirection);
        auto interaction = options.interactionSource ? options.interactionSource : RememberInteractionSource(key);
        ScrollState& scrollState = options.scrollState ? *options.scrollState : RememberScrollState(0);
        const Orientation orientation = singleLine ? Orientation::Horizontal : Orientation::Vertical;

        auto& field = comp.Remember<std::shared_ptr<Field>>([] { return std::make_shared<Field>(); }, {}, key);
        Field& f = *field;
        f.Bind(state, singleLine, options.codepointTransformation, options.outputTransformation, options.inputTransformation);
        // InputTransformation.keyboardOptions under the field's own.
        KeyboardOptions resolved = options.keyboardOptions;
        if (options.inputTransformation)
            if (const auto k = options.inputTransformation->KeyboardOptions()) resolved = resolved.FillUnspecifiedValuesWith(*k);
        const bool sessionChanged = f.keyboard != resolved || f.Editable() != (options.enabled && !options.readOnly);
        f.interaction = interaction;
        const bool blocked = comp.Read(LocalPlatformTextInputBlocked);
        if (blocked && !f.textInputBlocked) f.DisposeInputSession();
        f.textInputBlocked = blocked;
        f.selection->interactionSource = interaction;
        // SideEffect: properties that are not observable.
        f.transformed->Update(options.inputTransformation);
        f.options = options;
        f.toolbar.toolbarProvider = rt::Composition::Current()->Read(LocalTextContextMenuToolbarProvider);
        f.keyboard = resolved;
        f.selection->Update(f.owner, f.selection->density, options.enabled, options.readOnly, options.isPassword);
        if (f.owner) f.UpdateBehaviors(options.textStyle.span.localeList);
        f.layout->onTextLayout = options.onTextLayout;
        if (sessionChanged) {
            if (f.Editable() && (f.IsFocused() || f.inputSession)) f.StartInputSession(false);   // replaces the old session
            else if (!f.Editable()) f.DisposeInputSession();
        }

        const bool writeable = options.enabled && !options.readOnly;
        const Color selectionBackground = comp.Read(LocalTextSelectionColors).backgroundColor;

        Modifier m = FocusRequesterModifier(modifier, f.requester);
        DecoratorParams dp{ field, options.enabled, options.readOnly, options.isPassword, resolved.imeAction.value_or(ui::ImeAction::Default),
                            options.inputTransformation, comp.Read(LocalAutofillHighlightColor) };
        // stylusHandwriting: a stroke on or near the field focuses it and starts handwriting
        // (password fields only focus).
        const ui::KeyboardType kt = resolved.keyboardType.value_or(ui::KeyboardType::Text);
        const bool handwritingEnabled = !options.isPassword && kt != ui::KeyboardType::Password && kt != ui::KeyboardType::NumberPassword;
        StylusHandwritingCallbacks hw;
        hw.isFocused = [field] { return field->nodeFocused; };
        hw.requestFocus = [field] { field->RequestFocus(); };
        hw.onHandwritingSlopExceeded = [field, handwritingEnabled] { if (handwritingEnabled) field->TriggerStylusHandwriting(); };
        m = StylusHandwriting(std::move(m), options.enabled, ui::IsStylusHandwritingSupported(), handwritingEnabled, std::move(hw));
        m = m.With<DecoratorNode>(dp);
        m = FocusPropertiesMod(std::move(m), [field](ui::FocusProperties& p) { p.focusRect = field->selection->GetFocusRect(); });
        m = Focusable(std::move(m), options.enabled ? ui::Focusability::Always : ui::Focusability::Never,
                      [field](ui::FocusState s) { field->OnFocusChange(ui::IsFocused(s)); }, options.enabled ? interaction : nullptr);
        m = OnKeyEvent(
            std::move(m),
            [field](const ui::KeyEvent& e) {
                Field& fl = *field;
                return fl.keys.OnKeyEvent(e, *fl.transformed, fl.layout->PeekLayoutResult(), fl.VisibleTextLayoutHeight(), fl.Editable(),
                                          fl.singleLine, fl.owner, fl.KeyCallbacks());
            },
            [field](const ui::KeyEvent& e) { return field->keys.OnPreKeyEvent(e, *field->transformed, field->KeyCallbacks()); });
        const auto keys = rt::Keys(reinterpret_cast<std::uintptr_t>(field.get()), reinterpret_cast<std::uintptr_t>(f.selection.get()),
                                   reinterpret_cast<std::uintptr_t>(interaction.get()));
        m = PointerInput(std::move(m), keys, [field](PointerInputScope& s) { return field->selection->DetectDirectTouchInteraction(s); });
        m = PointerInput(std::move(m), keys, [field](PointerInputScope& s) {
            return field->selection->DetectTextFieldTapGestures(
                s, [field] { if (!field->selection->isWindowAndTextFieldFocused) field->RequestFocus(); }, [field] { field->ShowKeyboard(); });
        });
        m = PointerInput(std::move(m), keys, [field](PointerInputScope& s) {
            return field->selection->TextFieldSelectionGestures(s, [field] { if (!field->selection->isWindowAndTextFieldFocused) field->RequestFocus(); });
        });
        // ScrollableDefaults.reverseDirection: horizontal scrolling reverses in RTL.
        ScrollableOptions so;
        // Off while a touch drags the selection; a mouse drag still lets the wheel scroll.
        so.enabled = options.enabled && field->selection->DirectDragGestureInitiator() != TextFieldSelectionState::InputType::Touch;
        so.reverseDirection = orientation == Orientation::Horizontal && layoutDirection == ui::LayoutDirection::Rtl;
        m = Scrollable(std::move(m), scrollState, orientation, so);
        m = PointerHoverIcon(std::move(m), PointerIcon::Text);
        m = AppendTextContextMenuComponents(std::move(m), [field](TextContextMenuBuilderScope& scope) { field->selection->AddTextContextMenuComponents(scope); });

        auto inner = [field, &scrollState, orientation, writeable, selectionBackground, singleLine, options, interaction] {
            auto& c = *rt::Composition::Current();
            Field& fl = *field;
            const bool isFocused = interaction->IsFocused();
            const bool windowFocused = ui::IsWindowFocused(c.Read(ui::LocalWindowInfo));
            const bool isWindowAndTextFieldFocused = isFocused && windowFocused;
            if (fl.windowFocused != windowFocused) {
                fl.windowFocused = windowFocused;
                fl.OnIsFocusedUpdated();
            }
            const int minLines = singleLine ? 1 : options.lineLimits.minHeightInLines;
            const int maxLines = singleLine ? 1 : options.lineLimits.maxHeightInLines;
            Modifier size = Modifier().With<SizeNode>(SizeParams{ options.textStyle, minLines, maxLines, singleLine, singleLine, fl.layout.get() });
            size = ClipToBounds(std::move(size));
            const bool touchDrag = fl.selection->DirectDragGestureInitiator() == TextFieldSelectionState::InputType::Touch;
            size = size.With<CoreNode>(CoreParams{ field, isWindowAndTextFieldFocused, interaction->IsDragAndDropHovered(), options.cursorBrush, selectionBackground, writeable,
                                                    &scrollState, orientation, fl.transformed.get(), touchDrag });
            // The handles show while the field and its window are focused and the last pointer
            // was a finger.
            const bool showHandles = options.enabled && isWindowAndTextFieldFocused && fl.selection->IsDirectTouchInteraction();
            TextFieldWithHandles(*fl.selection, showHandles, !options.readOnly, [field, singleLine, options, size] {
                Box(size, Alignment::TopStart(), true, [field, singleLine, options] {
                    Field& fi = *field;
                    Modifier layout = Modifier().With<TextLayoutNode>(
                        TextLayoutParams{ field, fi.transformed.get(), options.textStyle, singleLine,
                                          fi.keyboard.keyboardType == ui::KeyboardType::Phone });
                    layout = BringIntoViewRequesterMod(std::move(layout), fi.layout->bringIntoViewRequester);
                    Box(layout);
                });
            });
        };
        Box(m, Alignment::TopStart(), true, [options, inner, field] {
            // CommonContextMenuArea: a right click opens the dropdown menu.
            std::weak_ptr<Field> weak = field;
            ProvideDefaultPlatformTextContextMenuProviders([weak, enabled = options.enabled] {
                if (!enabled) return Modifier{};
                return ShowTextContextMenuOnSecondaryClick(Modifier{}, [weak](Offset) -> rt::Task<> {
                    if (auto f = weak.lock()) co_await f->selection->OnPreShowContextMenu();
                });
            }, [options, inner] {
                if (options.decorator) options.decorator(inner);
                else inner();
            });
        }, key);
    }

}
