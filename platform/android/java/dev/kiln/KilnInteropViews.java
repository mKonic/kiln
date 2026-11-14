package dev.kiln;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.HardwareRenderer;
import android.graphics.PorterDuff;
import android.graphics.RecordingCanvas;
import android.graphics.Rect;
import android.graphics.Region;
import android.graphics.RenderNode;
import android.os.SystemClock;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.FrameLayout;

/**
 * Native Views inside kiln content (AndroidView; AndroidViewHolder and AndroidViewsHandler in
 * Compose). Each View sits in a holder laid out where its element is, so focus, the keyboard and
 * accessibility treat it as a View of the window. The window does not draw them: each holder
 * records its View into a RenderNode and renders it into its element's embedded surface, which
 * kiln draws with the rest of its frame. Touches come from kiln, which hands the element's to the
 * View (pointerInteropFilter).
 */
final class KilnInteropViews {
    private final KilnView owner;
    private final Container container;
    private final SparseArray<Holder> holders = new SparseArray<>();

    KilnInteropViews(KilnView owner) {
        this.owner = owner;
        container = new Container(owner.getContext());
        owner.addView(container, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** Draws nothing and takes no input itself: kiln draws the Views and hands them their touches. */
    static final class Container extends FrameLayout {
        Container(Context c) {
            super(c);
            setWillNotDraw(true);
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        }
        @Override protected void dispatchDraw(Canvas canvas) {}
        // Nothing here covers the SurfaceView kiln draws into: its hole stays whole.
        @Override public boolean gatherTransparentRegion(Region region) { return true; }
        @Override public boolean dispatchTouchEvent(MotionEvent e) { return false; }
        @Override public boolean dispatchGenericMotionEvent(MotionEvent e) { return false; }
        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            // kiln measures the holders; this only takes the window's size.
            setMeasuredDimension(MeasureSpec.getSize(widthSpec), MeasureSpec.getSize(heightSpec));
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            for (int i = 0; i < getChildCount(); i++) {
                Holder h = (Holder) getChildAt(i);
                h.layout(h.x, h.y, h.x + h.getMeasuredWidth(), h.y + h.getMeasuredHeight());
            }
        }
    }

    class Holder extends FrameLayout {
        final int id;
        final View view;
        int x, y;
        // Its own kiln node, and the node it is a child of in the accessibility tree.
        int a11yNode = View.NO_ID, a11yParent = View.NO_ID;
        private boolean ready;
        private Surface surface;
        private HardwareRenderer renderer;
        private RenderNode node;
        private boolean drawPosted;
        private final Runnable drawFrame = () -> { drawPosted = false; render(); };

        Holder(Context c, int id, View view) {
            super(c);
            this.id = id;
            this.view = view;
            if (view.getParent() instanceof ViewGroup) ((ViewGroup) view.getParent()).removeView(view);
            ViewGroup.LayoutParams lp = view.getLayoutParams();
            addView(view, lp != null ? lp : new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
            // A View that is not important for accessibility has no AccessibilityNodeInfo.
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
            ready = true;
        }

        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            view.measure(widthSpec, heightSpec);
            setMeasuredDimension(view.getMeasuredWidth(), view.getMeasuredHeight());
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            view.layout(0, 0, r - l, b - t);
            scheduleDraw();
        }
        // The View wants another size: kiln measures the element again.
        @Override public void requestLayout() {
            super.requestLayout();
            if (ready) KilnNative.interopRequestLayout(owner.windowId, id);
        }
        @Override public void requestDisallowInterceptTouchEvent(boolean disallow) {
            KilnNative.interopDisallowIntercept(owner.windowId, id, disallow);
            super.requestDisallowInterceptTouchEvent(disallow);
        }
        // Its content changed: draw it again for the next frame.
        @Override public void onDescendantInvalidated(View child, View target) {
            super.onDescendantInvalidated(child, target);
            scheduleDraw();
        }
        @Override @SuppressWarnings("deprecation")
        public ViewParent invalidateChildInParent(int[] location, Rect dirty) {
            super.invalidateChildInParent(location, dirty);
            scheduleDraw();
            return null;
        }
        @Override public boolean requestChildRectangleOnScreen(View child, Rect rect, boolean immediate) {
            int l = rect.left + child.getLeft() - child.getScrollX(), t = rect.top + child.getTop() - child.getScrollY();
            KilnNative.interopRequestRectangle(owner.windowId, id, l, t, l + rect.width(), t + rect.height());
            return true;
        }
        @Override public boolean shouldDelayChildPressedState() { return true; }

        // Nested scrolling, the platform's ViewParent half: touch scrolls, and flings by velocity.
        // androidx scrollers also send the scrolls of their flings, to a TypedHolder.
        @Override public boolean onStartNestedScroll(View child, View target, int axes) {
            return (axes & (View.SCROLL_AXIS_VERTICAL | View.SCROLL_AXIS_HORIZONTAL)) != 0;
        }
        @Override public void onNestedPreScroll(View target, int dx, int dy, int[] consumed) {
            int[] c = KilnNative.interopNestedPreScroll(owner.windowId, id, dx, dy, true);
            if (c != null) { consumed[0] = c[0]; consumed[1] = c[1]; }
        }
        @Override public void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed) {
            KilnNative.interopNestedScroll(owner.windowId, id, dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed, true);
        }
        @Override public boolean onNestedPreFling(View target, float velocityX, float velocityY) {
            KilnNative.interopNestedFling(owner.windowId, id, velocityX, velocityY, false, true);
            return false;
        }
        @Override public boolean onNestedFling(View target, float velocityX, float velocityY, boolean consumed) {
            KilnNative.interopNestedFling(owner.windowId, id, velocityX, velocityY, consumed, false);
            return false;
        }

        @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) {
            super.onInitializeAccessibilityNodeInfo(info);
            if (a11yNode != View.NO_ID) owner.accessibility.initializeHolderInfo(info, a11yNode, a11yParent);
        }

        void scheduleDraw() {
            if (drawPosted) return;
            drawPosted = true;
            postOnAnimation(drawFrame);
        }

        void setSurface(Surface s) {
            if (renderer != null) { renderer.destroy(); renderer = null; }
            if (surface != null && surface != s) surface.release();
            surface = s;
            if (s != null) scheduleDraw();
        }

        /** Records the View (as Compose's drawAndroidView does) and renders it into the surface. */
        void render() {
            final int w = getWidth(), h = getHeight();
            if (surface == null || !surface.isValid() || w <= 0 || h <= 0 || getVisibility() != VISIBLE) return;
            if (renderer == null) {
                node = new RenderNode("kiln-interop");
                renderer = new HardwareRenderer();
                renderer.setContentRoot(node);
                renderer.setSurface(surface);
                float density = getResources().getDisplayMetrics().density;
                renderer.setLightSourceAlpha(0.039f, 0.19f);
                renderer.setLightSourceGeometry(w / 2f, 0, 600 * density, 800 * density);
            }
            node.setPosition(0, 0, w, h);
            RecordingCanvas c = node.beginRecording(w, h);
            try {
                c.drawColor(0, PorterDuff.Mode.CLEAR);
                draw(c);
            } finally {
                node.endRecording();
            }
            renderer.createRenderRequest().setVsyncTime(System.nanoTime()).setWaitForPresent(false).syncAndDraw();
        }

        void destroy() {
            removeCallbacks(drawFrame);
            if (renderer != null) renderer.destroy();
            renderer = null;
            if (surface != null) surface.release();
            surface = null;
            if (node != null) node.discardDisplayList();
        }
    }

    /**
     * The holder in an app that has androidx.core: AndroidViewHolder's NestedScrollingParent3, which
     * androidx scrollers (RecyclerView, NestedScrollView) look for in their parents. They send it
     * the scrolls of their flings too (TYPE_NON_TOUCH). Only loaded when the interfaces exist.
     */
    final class TypedHolder extends Holder implements androidx.core.view.NestedScrollingParent3 {
        private static final int TYPE_TOUCH = 0;

        TypedHolder(Context c, int id, View view) { super(c, id, view); }

        @Override public boolean onStartNestedScroll(View child, View target, int axes, int type) {
            return (axes & (View.SCROLL_AXIS_VERTICAL | View.SCROLL_AXIS_HORIZONTAL)) != 0;
        }
        @Override public void onNestedScrollAccepted(View child, View target, int axes, int type) {}
        @Override public void onStopNestedScroll(View target, int type) {}
        @Override public void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed, int type, int[] consumed) {
            int[] c = KilnNative.interopNestedScroll(owner.windowId, id, dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed, type == TYPE_TOUCH);
            if (c != null) { consumed[0] = c[0]; consumed[1] = c[1]; }
        }
        @Override public void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed, int type) {
            KilnNative.interopNestedScroll(owner.windowId, id, dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed, type == TYPE_TOUCH);
        }
        @Override public void onNestedPreScroll(View target, int dx, int dy, int[] consumed, int type) {
            int[] c = KilnNative.interopNestedPreScroll(owner.windowId, id, dx, dy, type == TYPE_TOUCH);
            if (c != null) { consumed[0] = c[0]; consumed[1] = c[1]; }
        }
    }

    private static final java.lang.reflect.Constructor<?> typedHolder = findTypedHolder();

    private static java.lang.reflect.Constructor<?> findTypedHolder() {
        try {
            Class.forName("androidx.core.view.NestedScrollingParent3");
            return Class.forName("dev.kiln.KilnInteropViews$TypedHolder")
                    .getDeclaredConstructor(KilnInteropViews.class, Context.class, int.class, View.class);
        } catch (ReflectiveOperationException | LinkageError e) {
            return null;   // no androidx.core in this app
        }
    }

    private Holder newHolder(int id, View view) {
        if (typedHolder != null) {
            try { return (Holder) typedHolder.newInstance(this, owner.getContext(), id, view); }
            catch (ReflectiveOperationException | LinkageError ignored) {}
        }
        return new Holder(owner.getContext(), id, view);
    }

    // --- called from native ----------------------------------------------------------------------

    boolean create(int id, View view) {
        if (view == null) return false;
        Holder h = newHolder(id, view);
        holders.put(id, h);
        container.addView(h, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return true;
    }

    View view(int id) {
        Holder h = holders.get(id);
        return h == null ? null : h.view;
    }

    void release(int id) {
        Holder h = holders.get(id);
        if (h == null) return;
        holders.remove(id);
        h.destroy();
        h.removeAllViews();
        container.removeView(h);
    }

    /** Compose's obtainMeasureSpec, against the View's layout params. */
    private static int spec(int min, int max, int preferred) {
        final int infinity = Integer.MAX_VALUE;
        if (preferred >= 0 || min == max) return View.MeasureSpec.makeMeasureSpec(Math.max(min, Math.min(max, preferred >= 0 ? preferred : max)), View.MeasureSpec.EXACTLY);
        if (preferred == ViewGroup.LayoutParams.WRAP_CONTENT && max != infinity) return View.MeasureSpec.makeMeasureSpec(max, View.MeasureSpec.AT_MOST);
        if (preferred == ViewGroup.LayoutParams.MATCH_PARENT && max != infinity) return View.MeasureSpec.makeMeasureSpec(max, View.MeasureSpec.EXACTLY);
        return View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
    }

    private static ViewGroup.LayoutParams paramsOf(Holder h) {
        ViewGroup.LayoutParams lp = h.view.getLayoutParams();
        return lp != null ? lp : new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
    }

    long measure(int id, int minW, int maxW, int minH, int maxH) {
        Holder h = holders.get(id);
        if (h == null) return 0;
        ViewGroup.LayoutParams lp = paramsOf(h);
        if (minW != 0) h.view.setMinimumWidth(minW);
        if (minH != 0) h.view.setMinimumHeight(minH);
        h.measure(spec(minW, maxW, lp.width), spec(minH, maxH, lp.height));
        return ((long) h.getMeasuredWidth() << 32) | (h.getMeasuredHeight() & 0xffffffffL);
    }

    /** One side unbounded (-1), the other given. */
    long intrinsic(int id, int width, int height) {
        Holder h = holders.get(id);
        if (h == null) return 0;
        ViewGroup.LayoutParams lp = paramsOf(h);
        final int unspecified = View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
        h.measure(width < 0 ? unspecified : spec(0, width, lp.width), height < 0 ? unspecified : spec(0, height, lp.height));
        return ((long) h.getMeasuredWidth() << 32) | (h.getMeasuredHeight() & 0xffffffffL);
    }

    void place(int id, int l, int t, int r, int b, boolean visible) {
        Holder h = holders.get(id);
        if (h == null) return;
        int want = visible ? View.VISIBLE : View.INVISIBLE;
        if (h.getVisibility() != want) h.setVisibility(want);
        if (!visible) return;
        final int w = r - l, hh = b - t;
        h.x = l;
        h.y = t;
        if (h.getMeasuredWidth() != w || h.getMeasuredHeight() != hh)
            h.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(hh, View.MeasureSpec.EXACTLY));
        if (h.getLeft() != l || h.getTop() != t || h.getWidth() != w || h.getHeight() != hh || h.isLayoutRequested()) h.layout(l, t, r, b);
    }

    void target(int id, Surface surface) {
        Holder h = holders.get(id);
        if (h == null) { if (surface != null) surface.release(); return; }
        h.setSurface(surface);
    }

    /** The event kiln is dispatching now, to the View at (x, y) in the window; or a cancel. */
    boolean touch(int id, float x, float y, boolean cancel) {
        Holder h = holders.get(id);
        if (h == null) return false;
        MotionEvent current = owner.currentTouch;
        MotionEvent e;
        if (current != null) {
            e = MotionEvent.obtain(current);
            if (cancel) e.setAction(MotionEvent.ACTION_CANCEL);
        } else if (cancel) {
            long now = SystemClock.uptimeMillis();
            e = MotionEvent.obtain(now, now, MotionEvent.ACTION_CANCEL, 0, 0, 0);
        } else {
            return false;
        }
        e.offsetLocation(-x, -y);
        boolean handled = h.dispatchTouchEvent(e);
        e.recycle();
        return handled;
    }

    /** The holder of kiln's node `semanticsId`, the accessibility child of node `parentId`. */
    View accessibilityChild(int id, int semanticsId, int parentId) {
        Holder h = holders.get(id);
        if (h == null) return null;
        h.a11yNode = semanticsId;
        h.a11yParent = parentId;
        return h;
    }

    /** semanticsIdToView: the holder at kiln's node `semanticsId`, or null. */
    View holderOfNode(int windowId, int semanticsId) {
        if (holders.size() == 0) return null;
        int id = KilnNative.a11yInteropView(windowId, semanticsId);
        return id > 0 ? holders.get(id) : null;
    }

    void destroyAll() {
        for (int i = 0; i < holders.size(); i++) holders.valueAt(i).destroy();
        holders.clear();
        container.removeAllViews();
    }
}
