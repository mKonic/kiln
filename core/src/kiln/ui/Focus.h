#pragma once

// Focus and keys (spec 03 req. 4-5, §focus): a single active focus target per owner; states are
// derived (Active/Captured for the target, ActiveParent for its focus ancestors, Inactive for the
// rest). 1D search (Tab) follows placement order and wraps; 2D search (arrows, DPAD) is Android
// FocusFinder's beam algorithm. Key events tunnel root -> focused (preview) and bubble back.

#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>
#include "kiln/ui/Modifier.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kiln::ui {

    enum class FocusDirection : std::uint8_t { Next = 1, Previous, Left, Right, Up, Down, Enter, Exit };
    enum class FocusState : std::uint8_t { Inactive, Active, ActiveParent, Captured };
    enum class Focusability : std::uint8_t { SystemDefined, Always, Never };
    enum class InputMode : std::uint8_t { Touch, Keyboard };

    inline bool IsFocused(FocusState s) { return s == FocusState::Active || s == FocusState::Captured; }
    inline bool HasFocus(FocusState s) { return s != FocusState::Inactive; }

    // Android key codes (KeyEvent.KEYCODE_*); Key.kt wraps the native code.
    namespace keys {
        constexpr int DpadUp = 19, DpadDown = 20, DpadLeft = 21, DpadRight = 22, DpadCenter = 23;
        constexpr int Tab = 61, Space = 62, Enter = 66, Del = 67, Escape = 111, Back = 4;
        constexpr int PageUp = 92, PageDown = 93, MoveHome = 122, MoveEnd = 123, NumpadEnter = 160;
        constexpr int NavigatePrevious = 260, NavigateNext = 261;
    }
    constexpr std::uint32_t kMetaShift = 0x1, kMetaAlt = 0x2, kMetaCtrl = 0x1000, kMetaMeta = 0x10000;

    enum class KeyEventType : std::uint8_t { Unknown, KeyDown, KeyUp };
    struct KeyEvent {
        int keyCode = 0;
        KeyEventType type = KeyEventType::Unknown;
        std::uint32_t metaState = 0;
        std::uint32_t unicode = 0;
        int repeatCount = 0;
        std::int64_t uptimeMillis = 0;
        // Where it came from: the IME (KeyEvent.FLAG_SOFT_KEYBOARD), a physical alphabetic
        // keyboard (isFromHardwareSource), any keyboard source (InputDevice.SOURCE_KEYBOARD).
        bool fromSoftKeyboard = false, fromHardwareSource = false, fromKeyboardSource = true;
        bool Shift() const { return metaState & kMetaShift; }
        bool Ctrl() const { return metaState & kMetaCtrl; }
        bool Alt() const { return metaState & kMetaAlt; }
        bool Meta() const { return metaState & kMetaMeta; }
    };

    class KeyInputModifier {
    public:
        virtual ~KeyInputModifier() = default;
        virtual bool OnPreKeyEvent(const KeyEvent&) { return false; }
        virtual bool OnKeyEvent(const KeyEvent&) { return false; }
    };

    // onPreInterceptKeyBeforeSoftKeyboard / onInterceptKeyBeforeSoftKeyboard: keys seen before
    // the IME gets them (View.dispatchKeyEventPreIme), along the focused path.
    class SoftKeyboardInterceptionModifier {
    public:
        virtual ~SoftKeyboardInterceptionModifier() = default;
        virtual bool OnPreInterceptKeyBeforeSoftKeyboard(const KeyEvent&) { return false; }
        virtual bool OnInterceptKeyBeforeSoftKeyboard(const KeyEvent&) { return false; }
    };

    // RotaryScrollEvent: a rotary encoder (crown, bezel) turn, in pixels (AXIS_SCROLL times the
    // view's scaled scroll factors; positive scrolls down / right).
    struct RotaryScrollEvent {
        float verticalScrollPixels = 0, horizontalScrollPixels = 0;
        std::int64_t uptimeMillis = 0;
        int inputDeviceId = 0;
    };
    class RotaryInputModifier {
    public:
        virtual ~RotaryInputModifier() = default;
        virtual bool OnPreRotaryScrollEvent(const RotaryScrollEvent&) { return false; }
        virtual bool OnRotaryScrollEvent(const RotaryScrollEvent&) { return false; }
    };

    class FocusRequester;

    // FocusFinder's 2D comparison: whether `proposed` beats `current` as the next stop from
    // `focused` in direction `d` (beam first, then 13 * major^2 + minor^2).
    bool IsBetterFocusCandidate(const Rect& proposed, const Rect& current, const Rect& focused, FocusDirection d);

    // focusProperties { ... }: overrides for the next focus target down the chain. A direction's
    // requester: null = default search, FocusProperties::Cancel() = stay put.
    // FocusEnterExitScope: what onEnter / onExit see. Requesting focus elsewhere redirects the
    // change; CancelFocusChange stops it.
    struct FocusEnterExitScope {
        FocusDirection requestedFocusDirection = FocusDirection::Enter;
        void CancelFocusChange() { canceled = true; }
        bool canceled = false;
    };

    struct FocusProperties {
        bool canFocus = true;
        // Called when focus is about to enter / leave this target's subtree (a group).
        kiln::Function<void(FocusEnterExitScope&)> onEnter, onExit;
        // The target's focus area in its own coordinates (unset: its bounds), for focus search.
        std::optional<Rect> focusRect;
        FocusRequester* next = nullptr;
        FocusRequester* previous = nullptr;
        FocusRequester* up = nullptr;
        FocusRequester* down = nullptr;
        FocusRequester* left = nullptr;
        FocusRequester* right = nullptr;
        FocusRequester* start = nullptr;   // left in LTR, right in RTL (when left/right are unset)
        FocusRequester* end = nullptr;
        static FocusRequester* Cancel();
    };

    class FocusPropertiesModifier {
    public:
        virtual ~FocusPropertiesModifier() = default;
        virtual void ApplyFocusProperties(FocusProperties& p) = 0;
    };

    class FocusTargetNode;

    // focusTarget(): a node that can hold focus.
    class FocusTargetNode : public ModifierNode {
    public:
        Focusability focusability = Focusability::Always;
        kiln::Function<void(FocusState previous, FocusState current)> onFocusChange;

        FocusTargetNode* AsFocusTarget() override { return this; }
        FocusState State() const;
        bool CanFocus() const;
        bool RequestFocus(FocusDirection direction = FocusDirection::Enter);
        void OnDetach() override;
        // The focusProperties above this target: earlier in its chain back to the previous
        // target, then up the parents until a target (fetchFocusProperties).
        FocusProperties FetchFocusProperties() const;
        // The child FocusRequester.saveFocusedChild saved (its composite key hash).
        std::optional<std::uint64_t> savedFocusedChild;

    protected:
        // Properties the target sets on itself, after the modifiers above it (focusRestorer).
        virtual void ApplyOwnFocusProperties(FocusProperties&) const {}

    private:
        friend class Owner;
        FocusState m_LastReported = FocusState::Inactive;
        bool m_IsRoot = false;
        bool m_InCustomEnter = false, m_InCustomExit = false;   // re-entrancy guards
    };

    // BeyondBoundsLayout: a lazy layout composes items past its edge in `direction`, one at a
    // time, calling `block` after each until it returns a value (nullopt: keep going) or there
    // is no more content; the extra items go away afterwards. Focus search uses it to move into
    // items not composed yet.
    class BeyondBoundsLayout {
    public:
        virtual ~BeyondBoundsLayout() = default;
        virtual std::optional<bool> Layout(FocusDirection direction, const kiln::Function<std::optional<bool>(bool hasMoreContent)>& block) = 0;
    };

    // findNearestBeyondBoundsLayoutAncestor: the nearest beyond-bounds layout before the node in
    // its chain, then up the ancestors (null: none).
    BeyondBoundsLayout* FindNearestBeyondBoundsLayoutAncestor(const ModifierNode& node);

    // onFocusChanged / onFocusEvent: observes the first focus target at or after it.
    class FocusEventModifier {
    public:
        virtual ~FocusEventModifier() = default;
        virtual void OnFocusEvent(FocusState state) = 0;
    };

    // The focus targets right below `anchor` (visitChildren(FocusTarget)): the next target in
    // its own chain, else the first target down each child subtree.
    std::vector<FocusTargetNode*> FocusTargetsBelow(const ModifierNode& anchor);

    // FocusRequester: requestFocus() from code. focusRequester nodes register while attached;
    // a request goes to the targets right below them.
    // invalidateFocusProperties: a focusProperties node's values changed (re-read them).
    void InvalidateFocusProperties(const ModifierNode& node);
    // requestFocusForChildInLocalBounds / InRootBounds: focuses the highest focusable target
    // below `node` whose bounds meet the rect (in the node's space, or the root's). False when
    // none does, or one that does already has focus.
    bool RequestFocusForChildInLocalBounds(const ModifierNode& node, float left, float top, float right, float bottom);
    bool RequestFocusForChildInRootBounds(const ModifierNode& node, float left, float top, float right, float bottom);

    class FocusRequester {
    public:
        bool RequestFocus(FocusDirection direction = FocusDirection::Enter);
        // saveFocusedChild / restoreFocusedChild: remember which child of this requester's
        // focused target has focus, and later focus that child again (false: nothing saved, or it
        // is gone).
        bool SaveFocusedChild();
        bool RestoreFocusedChild();
        void Add(const ModifierNode* anchor) { m_Anchors.push_back(anchor); }
        // findFocusTarget: offers each target below its nodes to `onFound` until one is taken.
        bool FindFocusTarget(const kiln::Function<bool(FocusTargetNode&)>& onFound) {
            const auto anchors = m_Anchors;
            for (const ModifierNode* a : anchors)
                for (FocusTargetNode* t : FocusTargetsBelow(*a)) if (onFound(*t)) return true;
            return false;
        }
        void Remove(const ModifierNode* anchor) { std::erase(m_Anchors, anchor); }
        // Expires with the requester: nodes that outlive it skip Remove.
        std::weak_ptr<const char> AliveToken() const { return m_Alive; }
    private:
        std::vector<const ModifierNode*> m_Anchors;
        std::shared_ptr<const char> m_Alive = std::make_shared<const char>(0);
    };

}
