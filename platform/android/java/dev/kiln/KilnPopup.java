package dev.kiln;

import android.content.Context;
import android.graphics.PixelFormat;
import android.os.IBinder;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;

/**
 * A Popup composable's window (Compose's PopupLayout), for content whose own window does not cover
 * the display: a dialog's, another popup's, a KilnView in an app's layout. A sub-panel window
 * holding a KilnView for the popup's own owner, placed by native code where the position provider
 * says (screen pixels). A press outside it (ACTION_OUTSIDE) or Escape asks native code to dismiss
 * it, as PopupWindow does; Back goes through the app's back dispatcher like any other window's.
 */
final class KilnPopup extends FrameLayout {
    final int id;
    final KilnView view;
    private final KilnView parent;
    private final WindowManager windowManager;
    private final WindowManager.LayoutParams params = new WindowManager.LayoutParams();
    private boolean shown;
    private boolean dismissOnBackPress = true, dismissOnClickOutside = true;

    /** Called by native code for a new popup window. */
    static KilnPopup open(KilnView parent, int id) { return new KilnPopup(parent, id); }

    private KilnPopup(KilnView parent, int id) {
        super(parent.getContext());
        this.id = id;
        this.parent = parent;
        windowManager = (WindowManager) parent.getContext().getSystemService(Context.WINDOW_SERVICE);
        view = new KilnView(parent.getContext(), id, parent);
        view.popup = this;
        setClipChildren(false);
        addView(view, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        // createLayoutParams: top-start gravity, the window placed by native code.
        params.gravity = Gravity.START | Gravity.TOP;
        params.type = WindowManager.LayoutParams.TYPE_APPLICATION_SUB_PANEL;
        params.format = PixelFormat.TRANSLUCENT;
        params.width = 0;
        params.height = 0;
        // accessibilityTitle is not public: the window title is what accessibility services read.
        params.setTitle("Pop-Up Window");
    }

    /** updatePopupProperties: createFlags with the secure policy resolved; scrim NaN or 0: none. */
    void update(boolean focusable, boolean dismissOnBackPress, boolean dismissOnClickOutside, boolean secure,
                boolean clippingEnabled, float scrimAlpha, boolean rtl) {
        this.dismissOnBackPress = dismissOnBackPress;
        this.dismissOnClickOutside = dismissOnClickOutside;
        int flags = WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH;
        if (!focusable) flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        if (secure) flags |= WindowManager.LayoutParams.FLAG_SECURE;
        if (!clippingEnabled) flags |= WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS;
        if (scrimAlpha > 0f) {
            flags |= WindowManager.LayoutParams.FLAG_DIM_BEHIND;
            params.dimAmount = scrimAlpha;
        } else {
            params.dimAmount = 0f;
        }
        params.flags = flags;
        view.setLayoutDirection(rtl ? View.LAYOUT_DIRECTION_RTL : View.LAYOUT_DIRECTION_LTR);
        if (shown) windowManager.updateViewLayout(this, params);
    }

    /** updatePosition: the window at these screen pixels (the first call shows it). */
    void place(int left, int top, int width, int height) {
        int[] origin = tokenWindowOrigin();
        params.x = left - origin[0];
        params.y = top - origin[1];
        params.width = width;
        params.height = height;
        if (!shown) {
            params.token = windowToken();
            windowManager.addView(this, params);
            shown = true;
        } else {
            windowManager.updateViewLayout(this, params);
        }
    }

    /**
     * resolveWindowToken: a popup opened from a sub-window (another popup) hangs off that
     * sub-window's parent, as a sub-window cannot parent another; otherwise the parent view's window.
     */
    private IBinder windowToken() {
        ViewGroup.LayoutParams lp = parent.getRootView().getLayoutParams();
        if (lp instanceof WindowManager.LayoutParams) {
            WindowManager.LayoutParams w = (WindowManager.LayoutParams) lp;
            if (w.type >= WindowManager.LayoutParams.FIRST_SUB_WINDOW && w.type <= WindowManager.LayoutParams.LAST_SUB_WINDOW && w.token != null)
                return w.token;
        }
        return parent.getApplicationWindowToken();
    }

    /** Where the window the token names sits on the screen: a sub-window's x / y are relative to it. */
    int[] tokenWindowOrigin() {
        if (parent.popup != null) return parent.popup.tokenWindowOrigin();
        int[] at = new int[2];
        parent.getRootView().getLocationOnScreen(at);
        return at;
    }

    // onTouchEvent (from PopupWindow): a press outside asks to dismiss.
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!dismissOnClickOutside) return super.onTouchEvent(event);
        int action = event.getActionMasked();
        boolean outside = action == MotionEvent.ACTION_OUTSIDE || (action == MotionEvent.ACTION_DOWN
                && (event.getX() < 0 || event.getX() >= getWidth() || event.getY() < 0 || event.getY() >= getHeight()));
        if (outside) {
            KilnNative.dialogDismissRequest(id);
            return true;
        }
        return super.onTouchEvent(event);
    }

    // dispatchKeyEvent (from PopupWindow): Escape dismisses; Back reaches the app's dispatcher.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (dismissOnBackPress && event.getKeyCode() == KeyEvent.KEYCODE_ESCAPE) {
            KeyEvent.DispatcherState state = getKeyDispatcherState();
            if (state != null) {
                if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                    state.startTracking(event, this);
                    return true;
                }
                if (event.getAction() == KeyEvent.ACTION_UP && state.isTracking(event) && !event.isCanceled()) {
                    KilnNative.dialogDismissRequest(id);
                    return true;
                }
            }
        }
        return super.dispatchKeyEvent(event);
    }

    /** Native code removing the window (the popup left the composition). */
    void dismissFromNative() {
        view.release();
        if (shown) windowManager.removeViewImmediate(this);
        shown = false;
    }
}
