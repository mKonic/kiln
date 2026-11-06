package dev.kiln;

import android.view.View;
import android.view.ViewParent;

import java.lang.reflect.Method;

/**
 * Nested scrolling from kiln to the host view's parents: NestedScrollingChildHelper and
 * ViewParentCompat, without androidx. Parents that implement androidx's NestedScrollingParent2/3
 * (CoordinatorLayout) get the typed calls by reflection; others get the platform ViewParent calls,
 * which only take touch scrolls (as ViewParentCompat does).
 */
final class KilnNestedScrolling {
    static final int TYPE_TOUCH = 0, TYPE_NON_TOUCH = 1;

    private final View view;
    private final ViewParent[] parents = new ViewParent[2];

    KilnNestedScrolling(View view) {
        this.view = view;
        view.setNestedScrollingEnabled(true);
    }

    boolean start(int axes, int type) {
        if (parents[type] != null) return true;
        if (!view.isNestedScrollingEnabled()) return false;
        View child = view;
        for (ViewParent p = view.getParent(); p != null; p = p.getParent()) {
            if (onStart(p, child, axes, type)) {
                parents[type] = p;
                onAccepted(p, child, axes, type);
                return true;
            }
            if (p instanceof View) child = (View) p;
        }
        return false;
    }

    int[] preScroll(int dx, int dy, int type) {
        int[] consumed = new int[2];
        ViewParent p = parents[type];
        if (p == null || (dx == 0 && dy == 0)) return consumed;
        Method m = method(p, PARENT2, "onNestedPreScroll", View.class, int.class, int.class, int[].class, int.class);
        if (m != null) invoke(m, p, view, dx, dy, consumed, type);
        else if (type == TYPE_TOUCH) {
            try { p.onNestedPreScroll(view, dx, dy, consumed); } catch (AbstractMethodError ignored) {}
        }
        return consumed;
    }

    int[] scroll(int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed, int type) {
        int[] consumed = new int[2];
        ViewParent p = parents[type];
        if (p == null || (dxConsumed == 0 && dyConsumed == 0 && dxUnconsumed == 0 && dyUnconsumed == 0)) return consumed;
        Method m3 = method(p, PARENT3, "onNestedScroll", View.class, int.class, int.class, int.class, int.class, int.class, int[].class);
        if (m3 != null) {
            invoke(m3, p, view, dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed, type, consumed);
            return consumed;
        }
        // Parents before NestedScrollingParent3 are assumed to take everything they are offered.
        consumed[0] += dxUnconsumed;
        consumed[1] += dyUnconsumed;
        Method m2 = method(p, PARENT2, "onNestedScroll", View.class, int.class, int.class, int.class, int.class, int.class);
        if (m2 != null) invoke(m2, p, view, dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed, type);
        else if (type == TYPE_TOUCH) {
            try { p.onNestedScroll(view, dxConsumed, dyConsumed, dxUnconsumed, dyUnconsumed); } catch (AbstractMethodError ignored) {}
        }
        return consumed;
    }

    boolean preFling(float vx, float vy) {
        ViewParent p = parents[TYPE_TOUCH];
        if (p == null) return false;
        try { return p.onNestedPreFling(view, vx, vy); } catch (AbstractMethodError e) { return false; }
    }

    boolean fling(float vx, float vy, boolean consumed) {
        ViewParent p = parents[TYPE_TOUCH];
        if (p == null) return false;
        try { return p.onNestedFling(view, vx, vy, consumed); } catch (AbstractMethodError e) { return false; }
    }

    void stopAll() {
        for (int type = 0; type < 2; type++) {
            ViewParent p = parents[type];
            if (p == null) continue;
            Method m = method(p, PARENT2, "onStopNestedScroll", View.class, int.class);
            if (m != null) invoke(m, p, view, type);
            else if (type == TYPE_TOUCH) {
                try { p.onStopNestedScroll(view); } catch (AbstractMethodError ignored) {}
            }
            parents[type] = null;
        }
    }

    // --- ViewParentCompat -----------------------------------------------------------------

    private boolean onStart(ViewParent p, View child, int axes, int type) {
        Method m = method(p, PARENT2, "onStartNestedScroll", View.class, View.class, int.class, int.class);
        if (m != null) return Boolean.TRUE.equals(invoke(m, p, child, view, axes, type));
        if (type != TYPE_TOUCH) return false;
        try { return p.onStartNestedScroll(child, view, axes); } catch (AbstractMethodError e) { return false; }
    }

    private void onAccepted(ViewParent p, View child, int axes, int type) {
        Method m = method(p, PARENT2, "onNestedScrollAccepted", View.class, View.class, int.class, int.class);
        if (m != null) invoke(m, p, child, view, axes, type);
        else if (type == TYPE_TOUCH) {
            try { p.onNestedScrollAccepted(child, view, axes); } catch (AbstractMethodError ignored) {}
        }
    }

    private static final String PARENT2 = "androidx.core.view.NestedScrollingParent2";
    private static final String PARENT3 = "androidx.core.view.NestedScrollingParent3";

    /** The interface method when `p` implements `iface` (looked up in p's class loader), else null. */
    private static Method method(ViewParent p, String iface, String name, Class<?>... args) {
        try {
            Class<?> c = Class.forName(iface, false, p.getClass().getClassLoader());
            if (!c.isInstance(p)) return null;
            return c.getMethod(name, args);
        } catch (ReflectiveOperationException | LinkageError e) {
            return null;
        }
    }

    private static Object invoke(Method m, Object target, Object... args) {
        try { return m.invoke(target, args); } catch (ReflectiveOperationException e) { return null; }
    }
}
