package androidx.core.view;

import android.view.View;

// Compile-only: the signatures of androidx.core's interface, so kiln's holder of an embedded View
// can implement it when the app has androidx.core. Not packaged with kiln.
public interface NestedScrollingParent {
    boolean onStartNestedScroll(View child, View target, int axes);
    void onNestedScrollAccepted(View child, View target, int axes);
    void onStopNestedScroll(View target);
    void onNestedScroll(View target, int dxConsumed, int dyConsumed, int dxUnconsumed, int dyUnconsumed);
    void onNestedPreScroll(View target, int dx, int dy, int[] consumed);
    boolean onNestedFling(View target, float velocityX, float velocityY, boolean consumed);
    boolean onNestedPreFling(View target, float velocityX, float velocityY);
    int getNestedScrollAxes();
}
