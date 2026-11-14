package androidx.core.view;

import android.view.View;

// Compile-only (see NestedScrollingParent).
public interface NestedScrollingParent2 extends NestedScrollingParent {
    boolean onStartNestedScroll(View child, View target, int axes, int type);
    void onNestedScrollAccepted(View child, View target, int axes, int type);
    void onStopNestedScroll(View target, int type);
    void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed, int type);
    void onNestedPreScroll(View target, int dx, int dy, int[] consumed, int type);
}
