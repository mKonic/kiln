#pragma once

// Native Views inside kiln (AndroidView's host side, AndroidViewHolder.android.kt). The host keeps
// each View by id in a holder laid out where its element is, so input, focus, the keyboard and
// accessibility reach it as a real View; it draws the View into the element's embedded surface,
// so kiln composes it with its content (clips, alpha, content drawn over it).
// Content measures the View, places it, and hands it the touches that reach the element; the
// host reports back what the View asks of its parent.

#include "kiln/ui/ExternalSurface.h"
#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <cstdint>
#include <vector>

namespace kiln::ui {

    struct Constraints;
    class LayoutNode;

    // A View's factory: (JNIEnv*, the window's Context) to a local reference to a new View.
    using InteropViewFactory = kiln::Function<void*(void* env, void* context)>;
    // Runs with the View: (JNIEnv*, the View).
    using InteropViewBlock = kiln::Function<void(void* env, void* view)>;

    // What the host tells the element.
    class InteropViewClient {
    public:
        virtual ~InteropViewClient() = default;
        // View.requestLayout: measure again.
        virtual void OnRequestRemeasure() = 0;
        // requestDisallowInterceptTouchEvent: while true, scrolling parents leave the gesture to it.
        virtual void OnRequestDisallowInterceptTouchEvent(bool disallow) = 0;
        // Nested scrolling from a View inside (its holder is a NestedScrollingParent), in the
        // View's pixels and signs; touch: TYPE_TOUCH, else a fling's TYPE_NON_TOUCH. The scrolls
        // return what kiln's nested-scroll parents consumed; flings go on without waiting.
        virtual IntOffset OnNestedPreScroll(int dx, int dy, bool touch) = 0;
        virtual IntOffset OnNestedScroll(int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed, bool touch) = 0;
        virtual void OnNestedPreFling(float velocityX, float velocityY) = 0;
        virtual void OnNestedFling(float velocityX, float velocityY, bool consumed) = 0;
        // requestChildRectangleOnScreen: bring this part of the element into view.
        virtual void OnRequestRectangleOnScreen(Rect rect) = 0;
        // Where the View has SurfaceViews, in the element's pixels. Their surfaces are below
        // kiln's, so the element clears its frame there for them to show through.
        virtual void OnSurfaceHoles(std::vector<IntRect> holes) = 0;
        // The element's layout node (where its semantics are, for accessibility).
        virtual const LayoutNode* Node() const = 0;
    };

    struct InteropViewHost {
        // Makes the View from the factory and keeps it; false when it could not.
        kiln::Function<bool(int id, const InteropViewFactory& factory)> create;
        // Runs `block` with the View now (update, onReset, onRelease).
        kiln::Function<void(int id, const InteropViewBlock& block)> with;
        // Removes the View from its holder and forgets it.
        kiln::Function<void(int id)> release;
        // Measures the View within the constraints (against its layout params, as Compose's
        // obtainMeasureSpec); intrinsic: one side unbounded, the other given (-1: unbounded).
        kiln::Function<IntSize(int id, const Constraints& constraints)> measure;
        kiln::Function<IntSize(int id, int width, int height)> intrinsic;
        // Where the element is in the window (pixels) and whether it is placed.
        kiln::Function<void(int id, IntRect bounds, bool visible)> place;
        // The embedded surface the View is drawn into (nativeWindow null: none now).
        kiln::Function<void(int id, PlatformSurface surface, int width, int height)> target;
        // Hands the touch event being dispatched now to the View, with `origin` the element's
        // top left in the window; cancel: an ACTION_CANCEL made from it. Returns whether the
        // View took it (on a down, whether it wants the rest of the gesture).
        kiln::Function<bool(int id, Offset origin, bool cancel)> touch;
    };

}
