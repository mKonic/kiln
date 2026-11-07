// SelectionContainer (SelectionContainer.kt, SelectionManager.kt, SelectionGestures.kt,
// SelectionModifierNode.kt, SelectionHandles.kt, AndroidSelectionHandles.android.kt).

#include "kiln/foundation/text/Selection.h"
#include "kiln/foundation/Magnifier.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/text/PlatformSelectionBehaviors.h"
#include "kiln/foundation/text/contextmenu/TextContextMenu.h"
#include "kiln/foundation/text/input/TextFieldBuffer.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/text/Savers.h"

#include <algorithm>
#include <cmath>

namespace kiln::foundation {

    namespace selection {

        namespace {
            constexpr int kHapticTextHandleMove = 9;   // HapticFeedbackConstants.TEXT_HANDLE_MOVE

            bool IsSpecified(Offset o) { return !std::isnan(o.x) && !std::isnan(o.y); }
            constexpr Offset kUnspecified{ NAN, NAN };
            float Distance(Offset o) { return std::sqrt(o.x * o.x + o.y * o.y); }
            bool ContainsInclusive(const Rect& r, Offset p) { return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom; }

            bool IsMouseOrTouchPad(const PointerEvent& e) {
                if (e.changes.empty()) return false;
                for (const auto& c : e.changes) if (c.type != PointerType::Mouse) return false;
                return true;
            }
            bool AnyPressed(const PointerEvent& e) {
                for (const auto& c : e.changes) if (c.pressed) return true;
                return false;
            }
            void ConsumeUps(const PointerEvent& e) {
                for (auto c : e.changes) if (c.ChangedToUp()) c.Consume();   // shares the consumed flag
            }
            bool DistanceIsTolerable(const ViewConfiguration& config, const PointerInputChange& a, const PointerInputChange& b) {
                return Distance(a.position - b.position) < PointerSlop(config, a.type);
            }

            // Runs `fn` when destroyed unless disarmed (a cancelled gesture coroutine).
            struct CancelGuard {
                std::function<void()> fn;
                ~CancelGuard() { if (fn) fn(); }
            };
        }

        namespace {
            // ClicksCounter: presses close in time and space count up.
            struct ClicksCounter {
                int clicks = 0;
                std::optional<PointerInputChange> previous;
                void Update(const ViewConfiguration& config, const PointerInputChange& c) {
                    if (previous && c.uptimeMillis - previous->uptimeMillis < config.doubleTapTimeoutMillis && DistanceIsTolerable(config, *previous, c)) ++clicks;
                    else clicks = 1;
                    previous = c;
                }
            };

            enum class DownResolution { Up, Drag, Timeout, Cancel };

            // awaitTouchSlopOrCancellation inside withTimeoutOrNull(longPressTimeout).
            rt::Task<DownResolution> AwaitSlopWithin(PointerInputScope& s, std::int64_t pointerId, std::int64_t deadline, Offset& overSlop) {
                const float slop = PointerSlop(s.Config(), PointerType::Touch);
                std::int64_t pointer = pointerId;
                Offset total;
                std::int64_t now = s.CurrentEvent().changes.empty() ? deadline : s.CurrentEvent().changes[0].uptimeMillis;
                auto upOrCancel = [&]() {
                    if (s.CurrentEvent().changes.empty()) return DownResolution::Cancel;
                    auto first = s.CurrentEvent().changes[0];
                    if (first.ChangedToUpIgnoreConsumed()) { first.Consume(); return DownResolution::Up; }
                    return DownResolution::Cancel;
                };
                for (;;) {
                    auto e = co_await s.AwaitPointerEventWithTimeout(std::max<std::int64_t>(0, deadline - now), PointerEventPass::Main);
                    if (!e) co_return DownResolution::Timeout;
                    if (!e->changes.empty()) now = e->changes[0].uptimeMillis;
                    PointerInputChange* drag = nullptr;
                    for (auto& c : e->changes) if (c.id == pointer) drag = &c;
                    if (!drag || drag->IsConsumed()) co_return upOrCancel();
                    if (drag->ChangedToUpIgnoreConsumed()) {
                        const PointerInputChange* other = nullptr;
                        for (const auto& c : e->changes) if (c.pressed) { other = &c; break; }
                        if (!other) co_return upOrCancel();
                        pointer = other->id;
                        continue;
                    }
                    total = total + drag->PositionChangeIgnoreConsumed();
                    const float len = Distance(total);
                    if (len > 0 && len >= slop) {
                        drag->Consume();
                        overSlop = { total.x - total.x / len * slop, total.y - total.y / len * slop };
                        co_return DownResolution::Drag;
                    }
                    auto check = co_await s.AwaitPointerEventWithTimeout(std::max<std::int64_t>(0, deadline - now), PointerEventPass::Final);
                    if (!check) co_return DownResolution::Timeout;
                    if (drag->IsConsumed()) co_return upOrCancel();
                }
            }

            rt::Task<> TouchSelectionFirstPress(PointerInputScope& s, std::shared_ptr<TextDragObserver> observer, PointerEvent down) {
                const PointerInputChange firstDown = down.changes[0];
                CancelGuard guard{ [observer] { observer->OnCancel(); } };
                auto longPress = co_await AwaitLongPressOrCancellation(s, firstDown.id);
                if (longPress && DistanceIsTolerable(s.Config(), firstDown, *longPress)) {
                    observer->OnStart(longPress->position, Adjustment::Word);   // FirstLongPressSelectionAdjustment
                    auto up = co_await DragPointer(s, longPress->id, [&](PointerInputChange& c) {
                        observer->OnDrag(c.PositionChange());
                        c.Consume();
                    });
                    guard.fn = nullptr;
                    if (up) {
                        ConsumeUps(s.CurrentEvent());
                        observer->OnStop();
                    } else {
                        observer->OnCancel();
                    }
                }
                guard.fn = nullptr;
            }

            rt::Task<> TouchSelectionSubsequentPress(PointerInputScope& s, std::shared_ptr<TextDragObserver> observer, PointerEvent down, int clicks) {
                const PointerInputChange firstDown = down.changes[0];
                CancelGuard guard{ [observer] { observer->OnCancel(); } };
                observer->OnStart(firstDown.position, clicks > 2 ? Adjustment::Paragraph : Adjustment::Word);
                Offset overSlop;
                const auto resolution = co_await AwaitSlopWithin(s, firstDown.id, firstDown.uptimeMillis + s.Config().longPressTimeoutMillis, overSlop);
                if (resolution == DownResolution::Cancel) { guard.fn = nullptr; observer->OnCancel(); co_return; }
                if (resolution == DownResolution::Up) { guard.fn = nullptr; observer->OnStop(); co_return; }
                if (resolution == DownResolution::Drag) observer->OnDrag(overSlop);
                auto up = co_await DragPointer(s, firstDown.id, [&](PointerInputChange& c) {
                    observer->OnDrag(c.PositionChange());
                    c.Consume();
                });
                guard.fn = nullptr;
                if (up) {
                    ConsumeUps(s.CurrentEvent());
                    observer->OnStop();
                } else {
                    observer->OnCancel();
                }
            }

            rt::Task<> MouseSelection(PointerInputScope& s, std::shared_ptr<MouseSelectionObserver> observer, int clicks, PointerEvent down) {
                PointerInputChange downChange = down.changes[0];
                if (down.keyboardModifiers.IsShiftPressed()) {
                    if (!observer->OnExtend(downChange.position)) co_return;
                    CancelGuard guard{ [observer] { observer->OnDragDone(); } };
                    downChange.Consume();
                    auto up = co_await DragPointer(s, downChange.id, [&](PointerInputChange& c) {
                        if (observer->OnExtendDrag(c.position)) c.Consume();
                    });
                    if (up) ConsumeUps(s.CurrentEvent());
                    co_return;   // the guard ends the drag
                }
                const Adjustment adjustment = clicks == 1 ? Adjustment::None : clicks == 2 ? Adjustment::Word : Adjustment::Paragraph;
                if (!observer->OnStart(downChange.position, adjustment, clicks)) co_return;
                CancelGuard guard{ [observer] { observer->OnDragDone(); } };
                bool dragConsumed = adjustment != Adjustment::None;
                auto up = co_await DragPointer(s, downChange.id, [&](PointerInputChange& c) {
                    if (observer->OnDrag(c.position, adjustment)) {
                        c.Consume();
                        dragConsumed = true;
                    }
                });
                if (up && dragConsumed) ConsumeUps(s.CurrentEvent());
            }
        }

        // awaitSelectionGestures: a mouse selects by clicks and drags; touch selects on long press
        // (then drags) and by double / triple taps.
        rt::Task<> AwaitSelectionGestures(PointerInputScope& s, std::shared_ptr<MouseSelectionObserver> mouse, std::shared_ptr<TextDragObserver> drag) {
            ClicksCounter counter;
            for (;;) {
                PointerEvent down;
                for (;;) {
                    down = *co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    bool all = !down.changes.empty();
                    for (const auto& c : down.changes) if (!c.ChangedToDown()) all = false;
                    if (all) break;
                }
                counter.Update(s.Config(), down.changes[0]);
                const bool precise = IsMouseOrTouchPad(down);
                bool unconsumed = true;
                for (const auto& c : down.changes) if (c.IsConsumed()) unconsumed = false;
                if (precise && down.buttons.IsPrimaryPressed() && unconsumed) co_await MouseSelection(s, mouse, counter.clicks, down);
                else if (!precise && drag) {
                    if (counter.clicks == 1) co_await TouchSelectionFirstPress(s, drag, down);
                    else co_await TouchSelectionSubsequentPress(s, drag, down, counter.clicks);
                }
                // awaitEachGesture: every pointer up before the next gesture.
                while (AnyPressed(s.CurrentEvent())) co_await s.AwaitPointerEvent(PointerEventPass::Final);
            }
        }

        namespace {
            // DefaultLongPressDragObserver.
            class LongPressDragObserver final : public TextDragObserver {
            public:
                LongPressDragObserver(std::shared_ptr<SelectionRegistrar> r, std::int64_t id, std::function<std::optional<Coordinates>()> c)
                    : m_Registrar(std::move(r)), m_Id(id), m_Coords(std::move(c)) {}
                void OnStart(Offset start, Adjustment a) override {
                    m_Adjustment = a;
                    if (auto c = m_Coords(); c && c->IsAttached()) {
                        m_Registrar->NotifySelectionUpdateStart(*c, start, a, true);
                        m_LastPosition = start;
                        m_LastOffsetInRoot = PositionInRoot(*c);
                    }
                    if (!m_Registrar->HasSelection(m_Id)) return;
                    m_Total = {};
                }
                void OnDrag(Offset delta) override {
                    auto c = m_Coords();
                    if (!c || !c->IsAttached() || !m_Registrar->HasSelection(m_Id)) return;
                    m_Total = m_Total + delta;
                    // The text may have scrolled under the finger since the last update.
                    const Offset scroll = PositionInRoot(*c) - m_LastOffsetInRoot;
                    const Offset next = m_LastPosition + m_Total - scroll;
                    if (m_Registrar->NotifySelectionUpdate(*c, next, m_LastPosition, false, m_Adjustment, true)) {
                        m_LastPosition = next;
                        m_LastOffsetInRoot = PositionInRoot(*c);
                        m_Total = {};
                    }
                }
                void OnStop() override { End(); }
                void OnCancel() override { End(); }

            private:
                void End() {
                    if (m_Registrar->HasSelection(m_Id)) m_Registrar->NotifySelectionUpdateEnd();
                    m_LastPosition = kUnspecified;
                    m_LastOffsetInRoot = kUnspecified;
                }
                std::shared_ptr<SelectionRegistrar> m_Registrar;
                std::int64_t m_Id;
                std::function<std::optional<Coordinates>()> m_Coords;
                Offset m_LastPosition = kUnspecified, m_LastOffsetInRoot = kUnspecified, m_Total;
                Adjustment m_Adjustment = Adjustment::None;
            };

            // DefaultMouseSelectionObserver: `id` null for the container (selection between texts).
            class DefaultMouseObserver final : public MouseSelectionObserver {
            public:
                DefaultMouseObserver(std::shared_ptr<SelectionRegistrar> r, std::optional<std::int64_t> id, std::function<std::optional<Coordinates>()> c)
                    : m_Registrar(std::move(r)), m_Id(id), m_Coords(std::move(c)) {}
                bool OnExtend(Offset down) override {
                    auto c = m_Coords();
                    if (!c || !c->IsAttached()) return false;
                    if (m_Registrar->NotifySelectionUpdate(*c, down, m_LastPosition, false, Adjustment::None, false)) m_LastPosition = down;
                    return ShouldProcess();
                }
                bool OnExtendDrag(Offset p) override {
                    auto c = m_Coords();
                    if (!c || !c->IsAttached() || !ShouldProcess()) return false;
                    if (m_Registrar->NotifySelectionUpdate(*c, p, m_LastPosition, false, Adjustment::None, false)) m_LastPosition = p;
                    return true;
                }
                bool OnStart(Offset down, Adjustment a, int) override {
                    auto c = m_Coords();
                    if (!c || !c->IsAttached()) return false;
                    m_Registrar->NotifySelectionUpdateStart(*c, down, a, false);
                    m_LastPosition = down;
                    return ShouldProcess();
                }
                bool OnDrag(Offset p, Adjustment a) override {
                    auto c = m_Coords();
                    if (!c || !c->IsAttached() || !ShouldProcess()) return false;
                    if (m_Registrar->NotifySelectionUpdate(*c, p, m_LastPosition, false, a, false)) m_LastPosition = p;
                    return true;
                }
                void OnDragDone() override { m_Registrar->NotifySelectionUpdateEnd(); }

            private:
                bool ShouldProcess() const { return !m_Id || m_Registrar->HasSelection(*m_Id); }
                std::shared_ptr<SelectionRegistrar> m_Registrar;
                std::optional<std::int64_t> m_Id;
                std::function<std::optional<Coordinates>()> m_Coords;
                Offset m_LastPosition;
            };
        }

        Modifier SelectionGestures(Modifier m, std::shared_ptr<SelectionRegistrar> registrar, std::int64_t id,
                                   std::function<std::optional<Coordinates>()> coordinates) {
            auto mouse = std::make_shared<DefaultMouseObserver>(registrar, id, coordinates);
            auto drag = std::make_shared<LongPressDragObserver>(registrar, id, coordinates);
            return PointerInput(std::move(m), rt::Keys(registrar.get(), id), [mouse, drag](PointerInputScope& s) {
                return AwaitSelectionGestures(s, mouse, drag);
            });
        }

        bool IsCopyKeyEvent(const ui::KeyEvent& e) {
            // platformDefaultKeyMapping: Ctrl+C / Ctrl+Insert, or the Copy key, with exactly those modifiers.
            constexpr std::uint32_t kAll = ui::kMetaShift | ui::kMetaAlt | ui::kMetaCtrl | ui::kMetaMeta;
            const std::uint32_t mods = e.metaState & kAll;
            constexpr int kKeyC = 31, kInsert = 124, kCopy = 278;
            if (mods == ui::kMetaCtrl) return e.keyCode == kKeyC || e.keyCode == kInsert;
            return mods == 0 && e.keyCode == kCopy;
        }

        // --- SelectionManager ------------------------------------------------------------------

        class SelectionManager {
        public:
            explicit SelectionManager(std::shared_ptr<SelectionRegistrar> r) : registrar(std::move(r)) {
                Install();
                toolbar.computeContentBounds = [this] { return ContentRect(); };
                toolbar.onShow = [this] { return OnToolbarShow(this, alive); };
            }
            static rt::Task<> OnToolbarShow(SelectionManager* m, std::weak_ptr<char>) {
                if (!m->behaviors) co_return;
                if (const auto ts = m->GetContextTextAndSelection()) co_await m->behaviors->OnShowSelectionToolbar(ts->first, ts->second);
            }
            void EnsureBehaviors() {
                if (!behaviors && owner) behaviors = PlatformSelectionBehaviors::Create(owner, ui::SelectedTextType::StaticText, {});
            }
            ~SelectionManager() {
                alive.reset();
                suggestJob.Cancel();
                UnpinAll();
                registrar->onPositionChange = {};
                registrar->onSelectionUpdateStart = {};
                registrar->onSelectionUpdateSelectAll = {};
                registrar->onSelectionUpdate = {};
                registrar->onSelectionUpdateEnd = {};
                registrar->onSelectableChange = {};
                registrar->afterSelectableUnsubscribe = {};
            }

            std::shared_ptr<SelectionRegistrar> registrar;
            rt::State<std::optional<Selection>> selection;
            rt::State<bool> touchMode{ true };
            rt::State<bool> hasFocus{ false };
            rt::State<std::optional<Offset>> startHandlePosition, endHandlePosition, currentDragPosition;
            rt::State<std::optional<Handle>> draggingHandle;
            std::function<void(std::optional<Selection>)> onSelectionChangeUser;
            ui::Owner* owner = nullptr;
            FocusRequester focusRequester;
            std::optional<Coordinates> container;
            std::optional<SelectionLayout> previousSelectionLayout;
            bool isLongPressOrClickSelection = false;
            bool showToolbar = false;
            std::optional<Offset> previousPositionInWindow;
            std::map<std::int64_t, std::shared_ptr<ui::PinnableContainer>> pinned;
            std::shared_ptr<DefaultMouseObserver> mouseObserver;
            // rememberPlatformSelectionBehaviors(StaticText), the toolbar handler of
            // contextMenuAreaModifier, and the suggestion in flight.
            std::unique_ptr<PlatformSelectionBehaviors> behaviors;
            TextContextMenuToolbarHandler toolbar;
            rt::Job suggestJob;
            std::shared_ptr<char> alive = std::make_shared<char>(0);

            // selection's setter.
            void SetSelection(std::optional<Selection> value) {
                if (selection.Peek() == value) return;
                selection.Set(value);
                if (value) UpdateHandleOffsets();
                UpdatePinnedSelectables();
            }
            void OnSelectionChange(std::optional<Selection> s) {
                SetSelection(s);
                if (onSelectionChangeUser) onSelectionChangeUser(s);
            }
            void SetTouchMode(bool v) {
                if (touchMode.Peek() == v) return;
                touchMode.Set(v);
                UpdateSelectionToolbar();
            }
            bool TouchMode() const { return touchMode.Peek(); }
            bool IsDragging() const { return draggingHandle.Peek().has_value(); }

            void SetContainerCoordinates(const Coordinates& c) {
                container = c;
                if (c.IsAttached() && selection.Peek() && registrar->subselections.Peek().empty()) {
                    UpdateSubSelections(*selection.Peek());
                    focusRequester.RequestFocus();
                }
                if (hasFocus.Peek() && selection.Peek()) {
                    const Offset p = PositionInRoot(c);
                    if (previousPositionInWindow != p) {
                        previousPositionInWindow = p;
                        UpdateHandleOffsets();
                        UpdateSelectionToolbar();
                    }
                }
            }

            bool ContainerAttached() const { return container && container->IsAttached(); }

            const std::vector<std::shared_ptr<Selectable>>& Sorted() {
                static const std::vector<std::shared_ptr<Selectable>> kEmpty;
                return ContainerAttached() ? registrar->Sort(*container) : kEmpty;
            }

            Selectable* AnchorSelectable(const AnchorInfo& a) const { return registrar->Find(a.selectableId); }

            std::optional<Offset> ToContainer(const Coordinates& c, Offset p) const {
                if (!ContainerAttached()) return std::nullopt;
                return LocalPositionOf(*container, c, p);
            }

            void UpdateHandleOffsets() {
                const auto sel = selection.Peek();
                Selectable* startSel = sel ? AnchorSelectable(sel->start) : nullptr;
                Selectable* endSel = sel ? AnchorSelectable(sel->end) : nullptr;
                const auto startCoords = startSel ? startSel->GetLayoutCoordinates() : std::nullopt;
                const auto endCoords = endSel ? endSel->GetLayoutCoordinates() : std::nullopt;
                if (!sel || !ContainerAttached() || (!startCoords && !endCoords)) {
                    startHandlePosition.Set(std::nullopt);
                    endHandlePosition.Set(std::nullopt);
                    return;
                }
                const Rect visible = VisibleBounds(*container);
                auto position = [&](Selectable* s, const std::optional<Coordinates>& c, bool isStart) -> std::optional<Offset> {
                    if (!c) return std::nullopt;
                    const auto h = s->GetHandlePosition(*sel, isStart);
                    if (!h) return std::nullopt;
                    const Offset p = LocalPositionOf(*container, *c, *h);
                    const Handle mine = isStart ? Handle::SelectionStart : Handle::SelectionEnd;
                    if (draggingHandle.Peek() == mine || ContainsInclusive(visible, p)) return p;
                    return std::nullopt;
                };
                startHandlePosition.Set(position(startSel, startCoords, true));
                endHandlePosition.Set(position(endSel, endCoords, false));
            }

            float HandleLineHeight(bool isStart) const {
                const auto sel = selection.Peek();
                if (!sel) return 0;
                Selectable* s = AnchorSelectable(isStart ? sel->start : sel->end);
                return s ? s->GetLineHeight(isStart ? sel->start.offset : sel->end.offset) : 0;
            }

            void Install() {
                registrar->onPositionChange = [this](std::int64_t id) {
                    if (registrar->subselections.Peek().count(id)) {
                        UpdateHandleOffsets();
                        UpdateSelectionToolbar();
                    }
                };
                registrar->onSelectionUpdateStart = [this](bool touch, const Coordinates& c, Offset raw, Adjustment a) {
                    const ui::Size size = c.GetSize();
                    const Rect textRect{ 0, 0, size.width, size.height };
                    const Offset position = ContainsInclusive(textRect, raw) ? raw : Offset{ std::clamp(raw.x, 0.0f, size.width), std::clamp(raw.y, 0.0f, size.height) };
                    const auto inContainer = ToContainer(c, position);
                    if (inContainer && IsSpecified(*inContainer)) {
                        SetTouchMode(touch);
                        StartSelection(*inContainer, false, a);
                        focusRequester.RequestFocus();
                        HideSelectionToolbar();
                        isLongPressOrClickSelection = true;
                    }
                };
                registrar->onSelectionUpdateSelectAll = [this](bool touch, std::int64_t id) {
                    auto [next, subs] = SelectAllInSelectable(id, selection.Peek());
                    if (next != selection.Peek()) {
                        registrar->subselections.Set(subs);
                        OnSelectionChange(next);
                    }
                    SetTouchMode(touch);
                    focusRequester.RequestFocus();
                    HideSelectionToolbar();
                };
                registrar->onSelectionUpdate = [this](bool touch, const Coordinates& c, Offset next, Offset previous, bool isStart, Adjustment a) {
                    const auto n = ToContainer(c, next);
                    const auto p = IsSpecified(previous) ? ToContainer(c, previous) : std::nullopt;
                    SetTouchMode(touch);
                    if (!n) return false;
                    return UpdateSelection(*n, p, isStart, a);
                };
                registrar->onSelectionUpdateEnd = [this] {
                    ShowSelectionToolbar();
                    draggingHandle.Set(std::nullopt);
                    currentDragPosition.Set(std::nullopt);
                    previousSelectionLayout.reset();
                    if (isLongPressOrClickSelection && IsNonEmptySelection()) SuggestSelectionForLongPressOrDoubleClick();
                    isLongPressOrClickSelection = false;
                };
                registrar->onSelectableChange = [this](std::int64_t id) {
                    if (registrar->subselections.Peek().count(id)) {
                        OnRelease();
                        SetSelection(std::nullopt);
                    }
                };
                registrar->afterSelectableUnsubscribe = [this](std::int64_t id) {
                    const auto sel = selection.Peek();
                    if (sel && id == sel->start.selectableId) startHandlePosition.Set(std::nullopt);
                    if (sel && id == sel->end.selectableId) endHandlePosition.Set(std::nullopt);
                    if (registrar->subselections.Peek().count(id)) UpdateSelectionToolbar();
                    if (auto it = pinned.find(id); it != pinned.end()) {
                        it->second->Release();
                        pinned.erase(it);
                    }
                };
            }

            void UnpinAll() {
                for (auto& [id, p] : pinned) p->Release();
                pinned.clear();
            }
            // Selected lazy items stay composed while scrolled away.
            void UpdatePinnedSelectables() {
                const auto& subs = registrar->subselections.Peek();
                for (auto it = pinned.begin(); it != pinned.end();) {
                    if (!subs.count(it->first)) { it->second->Release(); it = pinned.erase(it); }
                    else ++it;
                }
                for (const auto& [id, sub] : subs) {
                    if (sub.start.offset == sub.end.offset || pinned.count(id)) continue;
                    Selectable* s = registrar->Find(id);
                    auto p = s && s->pinnableContainer ? s->pinnableContainer() : nullptr;
                    if (!p) continue;
                    p->Pin();
                    pinned[id] = p;
                }
            }

            std::pair<std::optional<Selection>, SubSelections> SelectAllInSelectable(std::int64_t id, const std::optional<Selection>& previous) {
                SubSelections subs;
                std::optional<Selection> merged;
                for (const auto& s : Sorted()) {
                    const auto sel = s->selectableId == id ? s->GetSelectAllSelection() : std::nullopt;
                    if (sel) subs[s->selectableId] = *sel;
                    merged = merged ? merged->Merge(sel) : sel;
                }
                if (TouchMode() && merged != previous && owner) owner->PerformHapticFeedback(kHapticTextHandleMove);
                return { merged, subs };
            }

            bool IsEntireContainerSelected() {
                const auto& sorted = Sorted();
                if (sorted.empty()) return true;
                const auto& subs = registrar->subselections.Peek();
                for (const auto& s : sorted) {
                    const int length = s->GetText().Length();
                    if (length == 0) continue;
                    auto it = subs.find(s->selectableId);
                    if (it == subs.end() || std::abs(it->second.start.offset - it->second.end.offset) != length) return false;
                }
                return true;
            }

            void UpdateSubSelections(const Selection& sel) {
                const auto& sorted = Sorted();
                if (sorted.empty()) return;
                registrar->subselections.Set(CreateSubSelectionsOf(sel, sorted));
            }

            static SubSelections CreateSubSelectionsOf(const Selection& sel, const std::vector<std::shared_ptr<Selectable>>& sorted) {
                const std::int64_t startId = sel.start.selectableId, endId = sel.end.selectableId;
                if (startId == endId) return { { startId, sel } };
                int startIndex = -1, endIndex = -1;
                for (std::size_t i = 0; i < sorted.size(); ++i) {
                    if (startIndex < 0 && sorted[i]->selectableId == startId) startIndex = int(i);
                    if (endIndex < 0 && sorted[i]->selectableId == endId) endIndex = int(i);
                }
                if (startIndex < 0 || endIndex < 0) return {};
                const bool crossed = startIndex > endIndex;
                SubSelections out;
                for (int i = std::min(startIndex, endIndex); i <= std::max(startIndex, endIndex); ++i) {
                    const auto& s = *sorted[std::size_t(i)];
                    const auto all = s.GetSelectAllSelection();
                    if (!all) continue;
                    Selection sub;
                    if (s.selectableId == startId) sub = crossed ? Selection{ sel.start, all->start, true } : Selection{ sel.start, all->end, false };
                    else if (s.selectableId == endId) sub = crossed ? Selection{ all->end, sel.end, true } : Selection{ all->start, sel.end, false };
                    else sub = crossed ? Selection{ all->end, all->start, true } : *all;
                    out[s.selectableId] = sub;
                }
                return out;
            }

            void SetSelectionAndShow(const Selection& sel) {
                UpdateSubSelections(sel);
                OnSelectionChange(sel);
                previousSelectionLayout.reset();
                focusRequester.RequestFocus();
                ShowSelectionToolbar();
            }

            // setSelection(TextRange): offsets over the texts laid end to end.
            void SetSelectionRange(int start, int end) {
                const auto& sorted = Sorted();
                if (sorted.empty()) return;
                auto anchorFor = [&](int global) -> std::optional<AnchorInfo> {
                    int running = 0;
                    for (const auto& s : sorted) {
                        const int length = s->GetText().Length();
                        if (global >= running && global <= running + length) {
                            const auto t = s->layoutCallback ? s->layoutCallback() : nullptr;
                            if (!t) return std::nullopt;
                            const int local = global - running;
                            return AnchorInfo{ TextDirectionForOffset(*t, local), local, s->selectableId };
                        }
                        running += length;
                    }
                    return std::nullopt;
                };
                const auto a = anchorFor(start), b = anchorFor(end);
                if (!a || !b) return;
                SetSelectionAndShow({ *a, *b, start > end });
            }

            void SelectAll() {
                const auto& sorted = Sorted();
                if (sorted.empty()) return;
                std::optional<Selection> first, last;
                SubSelections subs;
                for (const auto& s : sorted) {
                    const auto sub = s->GetSelectAllSelection();
                    if (!sub) continue;
                    if (!first) first = sub;
                    last = sub;
                    subs[s->selectableId] = *sub;
                }
                if (subs.empty()) return;
                const Selection next = subs.size() == 1 ? *first : Selection{ first->start, last->end, false };
                registrar->subselections.Set(subs);
                OnSelectionChange(next);
                previousSelectionLayout.reset();
                focusRequester.RequestFocus();
                ShowSelectionToolbar();
            }

            void ExtendSelectionByWord() {
                const auto& sorted = Sorted();
                if (sorted.empty()) return;
                const auto current = selection.Peek();
                if (!current) {
                    const Selectable* first = nullptr;
                    for (const auto& s : sorted) if (s->GetText().Length() > 0) { first = s.get(); break; }
                    if (!first) return;
                    const auto t = first->layoutCallback();
                    if (!t) return;
                    const auto word = t->WordBoundary(0);
                    const Selection next{ { TextDirectionForOffset(*t, 0), 0, first->selectableId },
                                          { TextDirectionForOffset(*t, std::max(0, word.second - 1)), word.second, first->selectableId }, false };
                    registrar->subselections.Set({ { first->selectableId, next } });
                    OnSelectionChange(next);
                    previousSelectionLayout.reset();
                    focusRequester.RequestFocus();
                    ShowSelectionToolbar();
                    return;
                }
                std::int64_t id = current->end.selectableId;
                int index = -1;
                for (std::size_t i = 0; i < sorted.size(); ++i) if (sorted[i]->selectableId == id) index = int(i);
                if (index == -1) return;
                const Selectable* sel = sorted[std::size_t(index)].get();
                auto t = sel->layoutCallback();
                if (!t) return;
                int maxOffset = sel->GetText().Length();
                int offset = current->end.offset;
                if (current->handlesCrossed) {
                    while (offset <= 0) {
                        if (index <= 0) return;
                        sel = sorted[std::size_t(--index)].get();
                        id = sel->selectableId;
                        t = sel->layoutCallback();
                        if (!t) return;
                        maxOffset = sel->GetText().Length();
                        offset = maxOffset;
                    }
                    const auto boundary = t->WordBoundary(std::max(0, offset - 1));
                    if (boundary.first < offset) offset = boundary.first;
                    else {
                        const int next = t->WordBoundary(std::max(offset - 1, 0)).first;
                        offset = next < offset ? next : std::max(offset - 1, 0);
                    }
                } else {
                    while (offset >= maxOffset) {
                        if (index >= int(sorted.size()) - 1) return;
                        sel = sorted[std::size_t(++index)].get();
                        id = sel->selectableId;
                        t = sel->layoutCallback();
                        if (!t) return;
                        maxOffset = sel->GetText().Length();
                        offset = 0;
                    }
                    const auto boundary = t->WordBoundary(offset);
                    if (boundary.second > offset) offset = boundary.second;
                    else {
                        const int next = t->WordBoundary(std::min(offset + 1, std::max(0, maxOffset - 1))).second;
                        offset = next > offset ? next : std::min(offset + 1, maxOffset);
                    }
                }
                const int directionOffset = current->handlesCrossed ? std::min(offset, std::max(0, maxOffset - 1)) : std::max(0, offset - 1);
                const AnchorInfo newEnd{ TextDirectionForOffset(*t, directionOffset), offset, id };
                bool crossed;
                if (current->start.selectableId == id) crossed = current->start.offset > offset;
                else {
                    int startIndex = -1;
                    for (std::size_t i = 0; i < sorted.size(); ++i) if (sorted[i]->selectableId == current->start.selectableId) startIndex = int(i);
                    crossed = startIndex > index;
                }
                SetSelectionAndShow({ current->start, newEnd, crossed });
            }

            bool IsTriviallyCollapsedSelection() const {
                const auto s = selection.Peek();
                return !s || s->start == s->end;
            }

            bool IsNonEmptySelection() {
                const auto s = selection.Peek();
                if (!s || s->start == s->end) return false;
                if (s->start.selectableId == s->end.selectableId) return true;
                const auto& subs = registrar->subselections.Peek();
                for (const auto& sel : Sorted()) {
                    auto it = subs.find(sel->selectableId);
                    if (it != subs.end() && it->second.start.offset != it->second.end.offset) return true;
                }
                return false;
            }

            // forEachSelectableWithSelection: in order, with whether it is the last one selected.
            void ForEachSelectableWithSelection(const std::function<bool(const Selectable&, const AnnotatedString&, int, int, bool)>& block) {
                const auto& sorted = Sorted();
                const auto& subs = registrar->subselections.Peek();
                int lastIndex = -1;
                for (int i = int(sorted.size()) - 1; i >= 0; --i) {
                    auto it = subs.find(sorted[std::size_t(i)]->selectableId);
                    if (it != subs.end() && it->second.start.offset != it->second.end.offset) { lastIndex = i; break; }
                }
                if (lastIndex == -1) return;
                for (int i = 0; i < int(sorted.size()); ++i) {
                    auto it = subs.find(sorted[std::size_t(i)]->selectableId);
                    if (it == subs.end()) continue;
                    if (!block(*sorted[std::size_t(i)], sorted[std::size_t(i)]->GetText(), it->second.start.offset, it->second.end.offset, i >= lastIndex)) return;
                }
            }

            std::optional<AnnotatedString> GetSelectedText() {
                if (!selection.Peek() || registrar->subselections.Peek().empty()) return std::nullopt;
                text::AnnotatedStringBuilder b;
                ForEachSelectableWithSelection([&](const Selectable&, const AnnotatedString& t, int s, int e, bool last) {
                    b.Append(t.SubSequence(std::min(s, e), std::max(s, e)));
                    if (!last) b.Append("\n");
                    return true;
                });
                return b.ToAnnotatedString();
            }

            std::vector<AnnotatedString> GetSelectedTexts() {
                std::vector<AnnotatedString> out;
                if (!selection.Peek() || registrar->subselections.Peek().empty()) return out;
                ForEachSelectableWithSelection([&](const Selectable&, const AnnotatedString& t, int s, int e, bool) {
                    out.push_back(t.SubSequence(std::min(s, e), std::max(s, e)));
                    return true;
                });
                return out;
            }

            std::vector<AnnotatedString> GetSelectableTexts() {
                std::vector<AnnotatedString> out;
                for (const auto& s : Sorted()) out.push_back(s->GetText());
                return out;
            }

            void Copy() {
                const auto t = GetSelectedText();
                if (t && !t->Empty() && owner) owner->SetClipboardAnnotatedText(*t);
            }

            void ShowSelectionToolbar() {
                showToolbar = true;
                UpdateSelectionToolbar();
            }
            void HideSelectionToolbar() {
                if (!showToolbar) return;
                showToolbar = false;
                UpdateSelectionToolbar();
            }

            // getContentRect: the selected region in root pixels, its bottom extended by four
            // handle heights.
            std::optional<Rect> ContentRect() {
                if (!selection.Peek() || !ContainerAttached()) return std::nullopt;
                const auto& subs = registrar->subselections.Peek();
                std::vector<std::pair<Selectable*, Selection>> pairs;
                for (const auto& s : Sorted())
                    if (auto it = subs.find(s->selectableId); it != subs.end()) pairs.emplace_back(s.get(), it->second);
                if (pairs.size() > 2) pairs = { pairs.front(), pairs.back() };
                if (pairs.empty()) return std::nullopt;
                Rect region{ INFINITY, INFINITY, -INFINITY, -INFINITY };
                for (const auto& [s, sub] : pairs) {
                    if (sub.start.offset == sub.end.offset) continue;
                    const auto c = s->GetLayoutCoordinates();
                    if (!c) continue;
                    const int lo = std::min(sub.start.offset, sub.end.offset), hi = std::max(sub.start.offset, sub.end.offset);
                    Rect local{ INFINITY, INFINITY, -INFINITY, -INFINITY };
                    for (int o : lo == hi - 1 ? std::vector<int>{ lo } : std::vector<int>{ lo, hi - 1 }) {
                        const RectF r = s->GetBoundingBox(o);
                        local = { std::min(local.left, r.x), std::min(local.top, r.y), std::max(local.right, r.x + r.w), std::max(local.bottom, r.y + r.h) };
                    }
                    const Offset tl = LocalPositionOf(*container, *c, { local.left, local.top });
                    const Offset br = LocalPositionOf(*container, *c, { local.right, local.bottom });
                    region = { std::min(region.left, tl.x), std::min(region.top, tl.y), std::max(region.right, br.x), std::max(region.bottom, br.y) };
                }
                if (std::isinf(region.left)) return std::nullopt;
                const Rect visible = VisibleBounds(*container).Intersect(region);
                if (visible.Width() < 0 || visible.Height() < 0) return std::nullopt;
                Rect root = visible.Translate(PositionInRoot(*container));
                if (owner) root.bottom += owner->GetDensity().ToPx(Dp{ kHandleHeightDp * 4 });
                return root;
            }

            // The new context menu: the toolbar shows once while there is a content rect; it
            // follows the rect itself.
            void UpdateSelectionToolbar() {
                if (!hasFocus.Peek()) return;
                if (showToolbar && TouchMode() && ContentRect()) {
                    toolbar.Show();
                    toolbar.InvalidateContentBounds();
                } else {
                    toolbar.Hide();
                }
            }

            // getContextTextAndSelection: the selected selectables' text (each selection joined
            // by a line break, the first's text before it and the last's after it) and the
            // selection in it.
            std::optional<std::pair<std::u16string, TextRange>> GetContextTextAndSelection() {
                if (!selection.Peek() || registrar->subselections.Peek().empty()) return std::nullopt;
                std::u16string text;
                int start = -1, end = -1;
                ForEachSelectableWithSelection([&](const Selectable&, const AnnotatedString& t, int s0, int e0, bool last) {
                    const int lo = std::min(s0, e0), hi = std::max(s0, e0);
                    if (start == -1) {
                        start = lo;
                        text += ToUtf16(t.SubSequence(0, lo).Text());
                    }
                    text += ToUtf16(t.SubSequence(lo, hi).Text());
                    if (!last) {
                        text += u'\n';
                    } else {
                        end = int(text.size());
                        text += ToUtf16(t.SubSequence(hi, t.Length()).Text());
                    }
                    return true;
                });
                if (start == -1 || end == -1) return std::nullopt;
                return std::pair{ std::move(text), TextRange(start, end) };
            }

            // addSelectionContainerTextContextMenuComponents
            void AddTextContextMenuComponents(TextContextMenuBuilderScope& scope) {
                auto item = [](TextContextMenuBuilderScope& sc, TextContextMenuKey key, bool enabled, std::function<void()> onClick,
                               std::function<bool()> closePredicate = {}) {
                    if (!enabled) return;
                    sc.Item(key, {}, [onClick, closePredicate](TextContextMenuSession& session) {
                        onClick();
                        if (!closePredicate || closePredicate()) session.Close();
                    });
                };
                const auto ts = GetContextTextAndSelection();
                AddPlatformTextContextMenuItems(scope, owner, false, ts ? &ts->first : nullptr, ts ? std::optional<TextRange>(ts->second) : std::nullopt,
                                                behaviors.get(), [&](TextContextMenuBuilderScope& sc) {
                    sc.Separator();
                    item(sc, TextContextMenuKeys::Copy, IsNonEmptySelection(), [this] {
                        Copy();
                        if (TouchMode()) OnRelease();
                    });
                    item(sc, TextContextMenuKeys::LookUp, CanShowLookUpMenuItem(), [this] { LookUp(); });
                    item(sc, TextContextMenuKeys::SelectAll, !IsEntireContainerSelected(), [this] { SelectAll(); },
                         [this] { return !showToolbar || !TouchMode(); });
                    sc.Separator();
                });
            }
            bool CanShowLookUpMenuItem() {
                if (!IsNonEmptySelection()) return false;
                const auto t = GetSelectedText();
                return t && CanShowLookUp(owner, ToUtf16(t->Text()));
            }
            void LookUp() {
                if (const auto t = GetSelectedText()) StartLookUpActivity(owner, ToUtf16(t->Text()));
            }

            // suggestSelectionForLongPressOrDoubleClick: the selection grows to what the
            // classifier suggests, while the selectable and its text stay.
            void SuggestSelectionForLongPressOrDoubleClick() {
                if (!behaviors) return;
                std::optional<AnnotatedString> textInSelectable;
                TextRange selectionInSelectable;
                std::int64_t targetId = kInvalidSelectableId;
                ForEachSelectableWithSelection([&](const Selectable& s, const AnnotatedString& t, int s0, int e0, bool last) {
                    if (last) {
                        textInSelectable = t;
                        selectionInSelectable = TextRange(s0, e0);
                        targetId = s.selectableId;
                    }
                    return false;
                });
                if (!textInSelectable || targetId == kInvalidSelectableId || textInSelectable->Length() == 0) return;
                suggestJob = rt::Launch(rt::Dispatcher::Main(), Suggest(this, alive, *textInSelectable, selectionInSelectable, targetId));
            }
            static rt::Task<> Suggest(SelectionManager* m, std::weak_ptr<char> weak, AnnotatedString text, TextRange range, std::int64_t id) {
                const auto suggested = co_await m->behaviors->SuggestSelectionForLongPressOrDoubleClick(ToUtf16(text.Text()), range);
                if (weak.expired() || !suggested || *suggested == range) co_return;
                Selectable* selectable = m->registrar->Find(id);
                if (!selectable || selectable->GetText().Text() != text.Text()) co_return;
                const auto layout = selectable->layoutCallback ? selectable->layoutCallback() : nullptr;
                if (!layout) co_return;
                const Selection next{ AnchorInfo{ layout->BidiRunDirection(suggested->start), suggested->start, id },
                                      AnchorInfo{ layout->BidiRunDirection(suggested->end), suggested->end, id }, false };
                m->registrar->subselections.Set(SubSelections{ { id, next } });
                m->OnSelectionChange(next);
                m->previousSelectionLayout.reset();
            }

            void OnRelease() {
                registrar->subselections.Set({});
                HideSelectionToolbar();
                previousSelectionLayout.reset();
                if (selection.Peek()) {
                    OnSelectionChange(std::nullopt);
                    if (TouchMode() && owner) owner->PerformHapticFeedback(kHapticTextHandleMove);
                }
            }

            void StartSelection(Offset position, bool isStartHandle, Adjustment a) {
                previousSelectionLayout.reset();
                UpdateSelection(position, std::nullopt, isStartHandle, a);
            }

            std::optional<SelectionLayout> GetSelectionLayout(Offset position, std::optional<Offset> previousHandle, bool isStartHandle) {
                const auto& sorted = Sorted();
                std::map<std::int64_t, int> order;
                for (std::size_t i = 0; i < sorted.size(); ++i) order[sorted[i]->selectableId] = int(i);
                SelectionLayoutBuilder builder;
                builder.currentPosition = position;
                builder.previousHandlePosition = previousHandle;
                builder.containerCoordinates = *container;
                builder.isStartHandle = isStartHandle;
                builder.previousSelection = previousHandle ? selection.Peek() : std::nullopt;
                const SelectionLayout* previousLayout = previousHandle && previousSelectionLayout ? &*previousSelectionLayout : nullptr;
                builder.previousLayout = previousLayout;
                builder.compareIds = [order](std::int64_t a, std::int64_t b) {
                    const int ia = order.count(a) ? order.at(a) : 0, ib = order.count(b) ? order.at(b) : 0;
                    return ia < ib ? -1 : ia > ib ? 1 : 0;
                };
                builder.allowSelectionBetweenSelectables = !TouchMode();   // isMouseSelectionBetweenTextEnabled
                for (std::size_t i = 0; i < sorted.size(); ++i) sorted[i]->AppendSelectableInfoToBuilder(builder, i + 1 == sorted.size(), previousLayout);
                return builder.Build();
            }

            bool UpdateSelection(Offset position, std::optional<Offset> previousHandle, bool isStartHandle, Adjustment a) {
                if (!ContainerAttached()) return false;
                draggingHandle.Set(isStartHandle ? Handle::SelectionStart : Handle::SelectionEnd);
                currentDragPosition.Set(position);
                auto layout = GetSelectionLayout(position, previousHandle, isStartHandle);
                if (!layout) return false;
                const bool recompute = layout->ShouldRecomputeSelection(previousSelectionLayout);
                if (recompute) {
                    const Selection next = Adjust(a, *layout);
                    if (next != selection.Peek()) {
                        if (ShouldPerformHaptics() && owner) owner->PerformHapticFeedback(kHapticTextHandleMove);
                        registrar->subselections.Set(layout->CreateSubSelections(next));
                        OnSelectionChange(next);
                        isLongPressOrClickSelection = false;
                    }
                    previousSelectionLayout = std::move(layout);
                }
                BringDraggedPositionIntoView();
                return recompute;
            }

            void BringDraggedPositionIntoView() {
                if (!previousSelectionLayout) return;
                const SelectableInfo& info = previousSelectionLayout->CurrentInfo();
                Selectable* s = registrar->Find(info.selectableId);
                if (!s || !s->bringIntoView) return;
                const auto c = s->GetLayoutCoordinates();
                const auto drag = currentDragPosition.Peek();
                if (!c || !drag) return;
                const Offset p = LocalPositionOf(*c, *container, *drag);
                Rect rect{ p.x, p.y, p.x, p.y };
                if (TouchMode()) {
                    const int offset = previousSelectionLayout->isStartHandle ? info.rawStartHandleOffset : info.rawEndHandleOffset;
                    const auto cursor = info.layout->Cursor(offset);
                    rect = { std::min(rect.left, cursor.x), std::min(rect.top, cursor.top), std::max(rect.right, cursor.x), std::max(rect.bottom, cursor.bottom) };
                }
                const ui::Size size = c->GetSize();
                const Offset nearest{ std::clamp(p.x, 0.0f, size.width), std::clamp(p.y, 0.0f, size.height) };
                rect = { std::min(rect.left, nearest.x), std::min(rect.top, nearest.y), std::max(rect.right, nearest.x), std::max(rect.bottom, nearest.y) };
                s->bringIntoView(rect);
            }

            bool ShouldPerformHaptics() const {
                if (!TouchMode()) return false;
                for (const auto& s : registrar->Selectables()) if (s->GetText().Length() > 0) return true;
                return false;
            }

            // handleDragObserver.
            class HandleObserver final : public TextDragObserver {
            public:
                HandleObserver(std::shared_ptr<SelectionManager> m, bool isStart) : m_Ptr(std::move(m)), m_Manager(*m_Ptr), m_IsStart(isStart) {}
                void OnDown(Offset) override {
                    auto& m = m_Manager;
                    if (!(m_IsStart ? m.startHandlePosition.Peek() : m.endHandlePosition.Peek())) return;
                    const auto sel = m.selection.Peek();
                    if (!sel) return;
                    Selectable* s = m.AnchorSelectable(m_IsStart ? sel->start : sel->end);
                    const auto c = s ? s->GetLayoutCoordinates() : std::nullopt;
                    if (!c || !m.ContainerAttached()) return;
                    const auto h = s->GetHandlePosition(*sel, m_IsStart);
                    if (!h) return;
                    m.currentDragPosition.Set(LocalPositionOf(*m.container, *c, { h->x, h->y - 1 }));   // getAdjustedCoordinates
                    m.draggingHandle.Set(m_IsStart ? Handle::SelectionStart : Handle::SelectionEnd);
                    m.HideSelectionToolbar();
                }
                void OnStart(Offset, Adjustment) override {
                    auto& m = m_Manager;
                    if (!m.draggingHandle.Peek()) return;
                    const auto sel = m.selection.Peek();
                    if (!sel) return;
                    Selectable* s = m.AnchorSelectable(m_IsStart ? sel->start : sel->end);
                    const auto c = s ? s->GetLayoutCoordinates() : std::nullopt;
                    if (!c || !m.ContainerAttached()) return;
                    const auto h = s->GetHandlePosition(*sel, m_IsStart);
                    if (!h) return;
                    m_Begin = LocalPositionOf(*m.container, *c, { h->x, h->y - 1 });
                    m_Total = {};
                }
                void OnDrag(Offset delta) override {
                    auto& m = m_Manager;
                    if (!m.draggingHandle.Peek()) return;
                    m_Total = m_Total + delta;
                    const Offset end = m_Begin + m_Total;
                    if (m.UpdateSelection(end, m_Begin, m_IsStart, Adjustment::CharacterWithWordAccelerate)) {
                        m_Begin = end;
                        m_Total = {};
                    }
                }
                void OnUp() override { Done(); }
                void OnStop() override { Done(); }
                void OnCancel() override { Done(); }

            private:
                void Done() {
                    m_Manager.ShowSelectionToolbar();
                    m_Manager.draggingHandle.Set(std::nullopt);
                    m_Manager.currentDragPosition.Set(std::nullopt);
                }
                std::shared_ptr<SelectionManager> m_Ptr;
                SelectionManager& m_Manager;
                bool m_IsStart;
                Offset m_Begin, m_Total;
            };
        };

        namespace {
            // detectDownAndDragGesturesWithObserver: down / up, and a drag past slop.
            rt::Task<> DetectDownAndDrag(PointerInputScope& s, std::shared_ptr<TextDragObserver> observer) {
                for (;;) {
                    auto down = co_await AwaitFirstDown(s, false);
                    CancelGuard guard{ [observer] { observer->OnCancel(); } };
                    observer->OnDown(down->position);
                    Offset overSlop;
                    auto drag = co_await AwaitTouchSlopOrCancellation(s, down->id, [&](PointerInputChange& c, Offset over) {
                        c.Consume();
                        overSlop = over;
                    });
                    if (drag) {
                        observer->OnStart(drag->position, Adjustment::None);
                        observer->OnDrag(overSlop);
                        auto up = co_await DragPointer(s, drag->id, [&](PointerInputChange& c) {
                            observer->OnDrag(c.PositionChange());
                            c.Consume();
                        });
                        guard.fn = nullptr;
                        if (up) observer->OnStop();
                        else observer->OnCancel();
                    }
                    while (AnyPressed(s.CurrentEvent())) co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    guard.fn = nullptr;
                    observer->OnUp();
                }
            }

            // awaitAllPointersUpWithSlopDetection.
            rt::Task<bool> AwaitAllUpWithSlop(PointerInputScope& s, const PointerInputChange& initial, PointerEventPass pass) {
                if (!AnyPressed(s.CurrentEvent())) co_return false;
                std::int64_t pointer = initial.id;
                bool reached = false;
                const float slop = PointerSlop(s.Config(), initial.type);
                Offset total;
                for (;;) {
                    auto e = co_await s.AwaitPointerEvent(pass);
                    const PointerInputChange* drag = nullptr;
                    for (const auto& c : e->changes) if (c.id == pointer) drag = &c;
                    if (!drag || drag->ChangedToUpIgnoreConsumed()) {
                        const PointerInputChange* other = nullptr;
                        for (const auto& c : e->changes) if (c.pressed) { other = &c; break; }
                        if (!other) co_return reached;
                        pointer = other->id;
                    } else {
                        total = total + drag->PositionChangeIgnoreConsumed();
                        if (Distance(total) > 0 && Distance(total) >= slop) reached = true;
                    }
                    if (!AnyPressed(*e)) co_return reached;
                }
            }

            // The handle: 25 dp, its point at the selection position (left handles mirrored).
            void SelectionHandle(SelectionManager& manager, bool isStart, std::shared_ptr<TextDragObserver> observer) {
                auto* c = rt::Composition::Current();
                const Color color = c->Read(LocalTextSelectionColors).handleColor;
                const auto sel = manager.selection.Get();
                if (!sel) return;
                const ResolvedTextDirection direction = isStart ? sel->start.direction : sel->end.direction;
                const bool ltr = (direction == ResolvedTextDirection::Ltr && !sel->handlesCrossed) || (direction == ResolvedTextDirection::Rtl && sel->handlesCrossed);
                const bool isLeft = isStart ? ltr : !ltr;
                SelectionManager* m = &manager;
                Modifier mod = PointerInput(Modifier{}, rt::Keys(observer.get()), [observer](PointerInputScope& s) { return DetectDownAndDrag(s, observer); });
                mod = SizeMod(std::move(mod), Dp{ kHandleWidthDp }, Dp{ kHandleHeightDp });
                mod = DrawBehind(std::move(mod), [m, isStart, isLeft, color](DrawScope& d) {
                    const auto p = isStart ? m->startHandlePosition.Get() : m->endHandlePosition.Get();
                    if (!p) return;   // iconVisible
                    const ui::Size size = d.GetSize();
                    const float r = size.width / 2;
                    gfx::Path path;
                    path.AddRect({ 0, 0, r, r });
                    path.AddOval({ 0, 0, 2 * r, 2 * r });
                    if (isLeft) {
                        d.Save();
                        d.Scale(-1, 1, { size.width / 2, size.height / 2 });
                        d.DrawPath(path, color);
                        d.Restore();
                    } else {
                        d.DrawPath(path, color);
                    }
                });
                Box(mod, Alignment::TopStart(), false, {}, rt::Key(isStart ? 1 : 2));
                (void)isLeft;
            }
        }

        // Handle positions for placement (HandlePositionProvider): the last one is kept while
        // the handle has none.
        struct HandleLayer {
            SelectionManager* manager;
            std::optional<Offset> previous[2];
        };
    }

    // --- SelectionState --------------------------------------------------------------------------

    void SelectionState::SelectAll() { if (m_Manager) m_Manager->SelectAll(); }
    void SelectionState::Clear() { if (m_Manager) m_Manager->OnRelease(); }
    void SelectionState::ExtendSelectionByWord() { if (m_Manager) m_Manager->ExtendSelectionByWord(); }
    std::vector<text::AnnotatedString> SelectionState::GetSelectableTexts() const { return m_Manager ? m_Manager->GetSelectableTexts() : std::vector<text::AnnotatedString>{}; }
    void SelectionState::Select(int start, int end) { if (m_Manager) m_Manager->SetSelectionRange(start, end); }

    SelectionState& RememberSelectionState() {
        using namespace selection;
        // Saver: the selected texts and the selection's anchors.
        rt::Saver<std::shared_ptr<SelectionState>> saver{
            [](const std::shared_ptr<SelectionState>& s) {
                rt::SavedValue::List texts;
                const auto textSaver = text::AnnotatedStringSaver();
                for (const auto& t : s->m_SelectedTexts.Peek()) texts.push_back(textSaver.save(t));
                rt::SavedValue::List out{ rt::SavedValue(std::move(texts)) };
                if (const auto& sel = s->m_Selection.Peek()) {
                    out.emplace_back(sel->start.offset);
                    out.emplace_back(sel->start.selectableId);
                    out.emplace_back(sel->start.direction == ResolvedTextDirection::Rtl);
                    out.emplace_back(sel->end.offset);
                    out.emplace_back(sel->end.selectableId);
                    out.emplace_back(sel->end.direction == ResolvedTextDirection::Rtl);
                    out.emplace_back(sel->handlesCrossed);
                }
                return rt::SavedValue(std::move(out));
            },
            [](const rt::SavedValue& v) -> std::optional<std::shared_ptr<SelectionState>> {
                const auto* list = std::get_if<rt::SavedValue::List>(&v.v);
                if (!list || list->empty()) return std::nullopt;
                auto s = std::make_shared<SelectionState>();
                std::vector<text::AnnotatedString> texts;
                if (const auto* t = std::get_if<rt::SavedValue::List>(&(*list)[0].v))
                    for (const auto& item : *t) if (auto restored = text::AnnotatedStringSaver().restore(item)) texts.push_back(std::move(*restored));
                s->m_SelectedTexts.Set(std::move(texts));
                if (list->size() == 8) {
                    auto i = [&](std::size_t k) { const auto* p = std::get_if<std::int64_t>(&(*list)[k].v); return p ? *p : 0; };
                    auto b = [&](std::size_t k) { const auto* p = std::get_if<bool>(&(*list)[k].v); return p && *p; };
                    auto dir = [&](std::size_t k) { return b(k) ? ResolvedTextDirection::Rtl : ResolvedTextDirection::Ltr; };
                    s->m_Selection.Set(Selection{ { dir(3), int(i(1)), i(2) }, { dir(6), int(i(4)), i(5) }, b(7) });
                }
                return s;
            },
        };
        auto& holder = rt::RememberSaveable<std::shared_ptr<SelectionState>>([] { return std::make_shared<SelectionState>(); }, saver);
        return *holder;
    }

    // --- composables -----------------------------------------------------------------------------

    namespace {
        using namespace selection;
        // selectionMagnifier (Android): while a handle is dragged in touch mode, the text magnifier
        // over the dragged anchor's line (calculateSelectionMagnifierCenterAndroid).
        Offset MagnifierCenter(SelectionManager& m, ui::IntSize magnifierSize) {
            constexpr Offset kUnspecified{ NAN, NAN };
            const auto sel = m.selection.Get();
            const auto handle = m.draggingHandle.Get();
            if (!sel || !handle) return kUnspecified;
            const AnchorInfo& anchor = *handle == Handle::SelectionStart ? sel->start : sel->end;
            selection::Selectable* selectable = m.AnchorSelectable(anchor);
            const auto drag = m.currentDragPosition.Get();
            if (!selectable || !m.container || !drag) return kUnspecified;
            const auto coords = selectable->GetLayoutCoordinates();
            if (!coords) return kUnspecified;
            const int offset = anchor.offset;
            if (offset > selectable->GetLastVisibleOffset()) return kUnspecified;
            // The x tracks the drag itself, within the line's bounds.
            const float dragX = LocalPositionOf(*coords, *m.container, *drag).x;
            const auto lineRange = selectable->GetRangeOfLineContaining(offset);
            float x;
            if (lineRange.first == lineRange.second) {
                x = selectable->GetLineLeft(offset);   // empty text
            } else {
                const float a = selectable->GetLineLeft(lineRange.first), b = selectable->GetLineRight(lineRange.second - 1);
                x = std::clamp(dragX, std::min(a, b), std::max(a, b));
            }
            if (x == -1) return kUnspecified;
            // Dragged too far past the text (Editor's rule).
            if (magnifierSize.width != 0 && std::abs(dragX - x) > float(magnifierSize.width) / 2) return kUnspecified;
            const float centerY = selectable->GetCenterYForOffset(offset);
            if (centerY == -1) return kUnspecified;
            return LocalPositionOf(*m.container, *coords, { x, centerY });
        }
        struct MagnifierParams_ { std::shared_ptr<SelectionManager> manager; bool operator==(const MagnifierParams_&) const = default; };
        struct SelectionMagnifierNode final : ModifierNode, DrawModifier, ui::GlobalPositionAwareModifier {
            MagnifierParams_ params;
            explicit SelectionMagnifierNode(MagnifierParams_ p) : params(std::move(p)) {}
            DrawModifier* AsDraw() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            void OnParamsChanged() { InvalidateDraw(); }
            void OnDetach() override { m_Magnifier.Detach(); }
            void OnGloballyPositioned() override { InvalidateDraw(); }
            void Draw(ContentDrawScope& d) override {
                d.DrawContent();
                SelectionManager& m = *params.manager;
                ui::Owner* owner = Owner() ? Owner()->GetOwner() : nullptr;
                // shouldShowMagnifier
                const bool show = m.draggingHandle.Get().has_value() && m.touchMode.Get() && !m.IsTriviallyCollapsedSelection();
                if (!show || !owner || !owner->Magnifier().create) { m_Magnifier.Detach(); return; }
                const Offset anchor = PositionInRoot(Coordinates{ Owner(), this, AliveToken() });
                m_Magnifier.Update([&m](ui::IntSize size) { return MagnifierCenter(m, size); }, owner, anchor, d);
            }
            SelectionMagnifier m_Magnifier;
        };

        // Where the toolbar handler sits (it collects the menu from here up).
        struct ToolbarAnchorNode final : ModifierNode {
            MagnifierParams_ params;
            explicit ToolbarAnchorNode(MagnifierParams_ p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            void OnAttach() override { params.manager->toolbar.Attach(Owner() ? Owner()->GetOwner() : nullptr, this); }
            void OnDetach() override { params.manager->toolbar.Detach(); }
        };

        using namespace selection;

        // The container's onGloballyPositioned: its inner space is the manager's coordinates.
        struct PositionedParams {
            std::shared_ptr<std::function<void(const Coordinates&)>> fn;
            bool operator==(const PositionedParams& o) const { return fn == o.fn; }
        };
        struct PositionedNode final : ModifierNode, ui::GlobalPositionAwareModifier {
            PositionedParams params;
            explicit PositionedNode(PositionedParams p) : params(std::move(p)) {}
            void OnParamsChanged() {}
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return this; }
            void OnGloballyPositioned() override {
                if (params.fn && *params.fn && Owner()) (*params.fn)(Coordinates{ Owner(), this, AliveToken() });
            }
        };

        // A focus target without semantics (Modifier.focusTarget).
        struct TargetParams {
            std::shared_ptr<std::function<void(FocusState)>> onChange;
            bool operator==(const TargetParams& o) const { return onChange == o.onChange; }
        };
        struct TargetNode final : ui::FocusTargetNode {
            TargetParams params;
            explicit TargetNode(TargetParams p) : params(std::move(p)) { Apply(); }
            void OnParamsChanged() { Apply(); }
            void Apply() {
                auto cb = params.onChange;
                onFocusChange = [cb](FocusState, FocusState now) { if (cb && *cb) (*cb)(now); };
            }
        };

        // onClearSelectionRequested: a primary press that lifts without moving past slop (and
        // no handle dragging) clears the selection. Handlers are plain functions: a coroutine
        // lambda's captures die with the lambda, which recomposition replaces.
        rt::Task<> ClearOnTap(PointerInputScope& s, std::shared_ptr<SelectionManager> m) {
            for (;;) {
                // awaitPrimaryFirstDown(requireUnconsumed = false)
                std::optional<PointerInputChange> down;
                for (;;) {
                    auto e = co_await s.AwaitPointerEvent(PointerEventPass::Main);
                    bool all = !e->changes.empty();
                    for (const auto& ch : e->changes) if (!ch.ChangedToDownIgnoreConsumed()) all = false;
                    if (all && IsMouseOrTouchPad(*e) && !e->buttons.IsPrimaryPressed()) all = false;
                    if (all) { down = e->changes[0]; break; }
                }
                const bool slop = co_await AwaitAllUpWithSlop(s, *down, PointerEventPass::Initial);
                if (!slop && !m->IsDragging()) m->OnRelease();
            }
        }

        // updateSelectionTouchMode: touch unless every pointer is a mouse.
        rt::Task<> TrackTouchMode(PointerInputScope& s, std::shared_ptr<SelectionManager> m) {
            for (;;) {
                auto e = co_await s.AwaitPointerEvent(PointerEventPass::Initial);
                m->SetTouchMode(!IsMouseOrTouchPad(*e));
            }
        }

        // SimpleLayout: children measured with the container's constraints, sized to the largest.
        // The handle layer reports no size (the handles are popups in Compose).
        std::shared_ptr<ui::MeasurePolicy> SimpleLayoutPolicy() {
            static auto policy = std::make_shared<ui::LambdaMeasurePolicy>([](MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
                std::vector<Placeable*> placed;
                int w = 0, h = 0;
                for (Measurable* m : children) {
                    Placeable& p = m->Measure(c);
                    w = std::max(w, p.Width());
                    h = std::max(h, p.Height());
                    placed.push_back(&p);
                }
                return s.Layout(c.ConstrainWidth(w), c.ConstrainHeight(h), [placed](PlacementScope& ps) { for (Placeable* p : placed) ps.Place(*p, 0, 0); });
            });
            return policy;
        }
    }

    void SelectionContainer(const Modifier& modifier, std::function<void()> content) {
        SelectionContainer(RememberSelectionState(), modifier, std::move(content));
    }

    void SelectionContainer(SelectionState& state, const Modifier& modifier, std::function<void()> content) {
        auto* c = rt::Composition::Current();
        // The registrar's next id survives re-creation (SelectionRegistrarImpl.Saver).
        auto& registrar = rt::RememberSaveable<std::shared_ptr<SelectionRegistrar>>(
            [] { return std::make_shared<SelectionRegistrar>(); },
            rt::Saver<std::shared_ptr<SelectionRegistrar>>{
                [](const std::shared_ptr<SelectionRegistrar>& r) { return rt::SavedValue(r->PeekNextId()); },
                [](const rt::SavedValue& v) -> std::optional<std::shared_ptr<SelectionRegistrar>> {
                    if (const auto* i = std::get_if<std::int64_t>(&v.v)) return std::make_shared<SelectionRegistrar>(*i);
                    return std::nullopt;
                } });
        auto manager = c->Remember<std::shared_ptr<SelectionManager>>([&] { return std::make_shared<SelectionManager>(registrar); });
        SelectionManager* m = manager.get();
        m->onSelectionChangeUser = [&state, m](std::optional<Selection> s) {
            state.m_Selection.Set(s);
            state.m_SelectedTexts.Set(m->GetSelectedTexts());
        };
        // manager.selection = state.selection
        if (m->selection.Peek() != state.m_Selection.Peek()) m->SetSelection(state.m_Selection.Peek());
        if (!m->mouseObserver)
            m->mouseObserver = std::make_shared<DefaultMouseObserver>(registrar, std::nullopt, [m] { return m->container; });

        auto onFocus = c->Remember<std::shared_ptr<std::function<void(FocusState)>>>([m = manager] {
            return std::make_shared<std::function<void(FocusState)>>([m](FocusState f) {
                if (!HasFocus(f) && m->hasFocus.Peek()) m->OnRelease();
                m->hasFocus.Set(HasFocus(f));
            });
        });
        auto onPositioned = c->Remember<std::shared_ptr<std::function<void(const Coordinates&)>>>([m = manager] {
            return std::make_shared<std::function<void(const Coordinates&)>>([m](const Coordinates& co) {
                if (!m->owner) m->owner = co.node->GetOwner();
                m->EnsureBehaviors();
                m->SetContainerCoordinates(co);
            });
        });

        // manager.modifier: clear on a tap, coordinates, focus, touch mode, copy key, mouse
        // selection between texts.
        Modifier mod = PointerInput(modifier, rt::Keys(m, 1), [manager](PointerInputScope& s) { return ClearOnTap(s, manager); });
        mod = mod.With<PositionedNode>(PositionedParams{ onPositioned });
        mod = FocusRequesterModifier(std::move(mod), m->focusRequester);
        mod = OnFocusChanged(std::move(mod), [onFocus](FocusState f) { (*onFocus)(f); });
        mod = mod.With<TargetNode>(TargetParams{});
        mod = PointerInput(std::move(mod), rt::Keys(m, 2), [manager](PointerInputScope& s) { return TrackTouchMode(s, manager); });
        mod = OnKeyEvent(std::move(mod), [m](const KeyEvent& e) {
            if (!IsCopyKeyEvent(e)) return false;
            if (e.type == KeyEventType::KeyDown) m->Copy();
            return true;
        });
        auto mouse = m->mouseObserver;
        mod = PointerInput(std::move(mod), rt::Keys(m, 3), [mouse](PointerInputScope& s) { return AwaitSelectionGestures(s, mouse, nullptr); });
        mod = mod.With<SelectionMagnifierNode>(MagnifierParams_{ manager });
        mod = AppendTextContextMenuComponents(std::move(mod), [m](TextContextMenuBuilderScope& scope) { m->AddTextContextMenuComponents(scope); });
        // contextMenuAreaModifier's toolbar handler, under the components.
        mod = mod.With<ToolbarAnchorNode>(MagnifierParams_{ manager });

        auto handleObservers = c->Remember<std::shared_ptr<std::pair<std::shared_ptr<TextDragObserver>, std::shared_ptr<TextDragObserver>>>>([manager] {
            return std::make_shared<std::pair<std::shared_ptr<TextDragObserver>, std::shared_ptr<TextDragObserver>>>(
                std::make_shared<SelectionManager::HandleObserver>(manager, true), std::make_shared<SelectionManager::HandleObserver>(manager, false));
        });
        auto layer = c->Remember<std::shared_ptr<HandleLayer>>([m] { return std::make_shared<HandleLayer>(HandleLayer{ m, {} }); });

        Layout(mod, SimpleLayoutPolicy(), [content, m, registrarPtr = registrar, handleObservers, layer] {
            rt::Composition::Current()->Provide(LocalSelectionRegistrar, registrarPtr, content);
            // The handles (touch mode, focused, a selection that is not trivially collapsed),
            // over the content, outside the container's size.
            const bool show = m->touchMode.Get() && m->hasFocus.Get() && m->selection.Get() && !m->IsTriviallyCollapsedSelection();
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([layer](MeasureScope& s, std::span<Measurable* const> children, const Constraints& c) {
                std::vector<std::pair<Placeable*, ui::IntOffset>> placed;
                for (std::size_t i = 0; i < children.size() && i < 2; ++i) {
                    Placeable& p = children[i]->Measure({ 0, Constraints::Infinity, 0, Constraints::Infinity });
                    const bool isStart = i == 0;
                    auto pos = isStart ? layer->manager->startHandlePosition.Get() : layer->manager->endHandlePosition.Get();
                    if (pos) layer->previous[i] = pos;
                    const Offset at = layer->previous[i].value_or(Offset{});
                    const auto sel = layer->manager->selection.Peek();
                    bool isLeft = false;
                    if (sel) {
                        const auto dir = isStart ? sel->start.direction : sel->end.direction;
                        const bool ltr = (dir == ResolvedTextDirection::Ltr && !sel->handlesCrossed) || (dir == ResolvedTextDirection::Rtl && sel->handlesCrossed);
                        isLeft = isStart ? ltr : !ltr;
                    }
                    // AbsoluteAlignment.TopRight for a left handle: its right edge at the point.
                    placed.emplace_back(&p, ui::IntOffset{ RoundJava(at.x) - (isLeft ? p.Width() : 0), RoundJava(at.y) });
                }
                // As small as allowed: the layer never grows the container.
                return s.Layout(c.minWidth, c.minHeight, [placed](PlacementScope& ps) { for (auto& [p, o] : placed) ps.Place(*p, o.x, o.y); });
            });
            Layout(ZIndex(Modifier{}, 1), policy, [m, handleObservers, show] {
                if (!show) return;
                SelectionHandle(*m, true, handleObservers->first);
                SelectionHandle(*m, false, handleObservers->second);
            }, rt::Key(0x5e1ec7));
        });

        // Binds the state; on dispose the selection goes (manager.onRelease, hasFocus = false).
        c->DisposableEffect(rt::Keys(&state, m), [&state, m = manager] {
            state.m_Manager = m.get();
            return std::function<void()>([&state, m] {
                m->onSelectionChangeUser = {};
                m->OnRelease();
                m->hasFocus.Set(false);
                m->toolbar.Hide();
                if (state.m_Manager == m.get()) {
                    state.m_Manager = nullptr;
                    state.m_Selection.Set(std::nullopt);
                }
            });
        });
    }

    void DisableSelection(std::function<void()> content) {
        rt::Composition::Current()->Provide(selection::LocalSelectionRegistrar, std::shared_ptr<selection::SelectionRegistrar>{}, std::move(content));
    }

}
