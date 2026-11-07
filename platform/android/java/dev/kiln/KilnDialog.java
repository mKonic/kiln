package dev.kiln;

import android.app.Dialog;
import android.os.Build;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;

/**
 * A Dialog composable's window (Compose's DialogWrapper with its DialogLayout): a platform dialog
 * holding a KilnView for the dialog's own owner. The window wraps the content (or takes the
 * platform's default dialog width), centres it and dims what is behind; Back, Escape and a press
 * outside the content ask native code to dismiss it. cancel() does nothing: dismissal is the
 * content's decision.
 */
final class KilnDialog extends Dialog {
    final int id;
    final KilnView view;
    private boolean dismissOnBackPress = true;
    private boolean usePlatformDefaultWidth;
    boolean decorFitsSystemWindows;
    private boolean hasCalledSetLayout;
    private boolean pressOutside;

    /** Called by native code for a new dialog window. */
    static KilnDialog open(KilnView parent, int id, boolean decorFitsSystemWindows) {
        return new KilnDialog(parent, id, decorFitsSystemWindows);
    }

    @SuppressWarnings("deprecation")
    private KilnDialog(KilnView parent, int id, boolean decorFits) {
        super(parent.getContext());
        this.id = id;
        this.decorFitsSystemWindows = decorFits;
        Window window = getWindow();
        window.requestFeature(Window.FEATURE_NO_TITLE);
        // DialogWindowTheme / FloatingDialogWindowTheme: translucent, not clipped to the outline.
        window.setBackgroundDrawableResource(android.R.color.transparent);
        window.setDecorFitsSystemWindows(decorFits);
        window.setGravity(Gravity.CENTER);
        if (!decorFits) {
            window.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_INSET_DECOR | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN);
            WindowManager.LayoutParams attrs = window.getAttributes();
            attrs.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            attrs.setFitInsetsSides(0);
            attrs.setFitInsetsTypes(0);
            window.setAttributes(attrs);
        }
        view = new KilnView(getContext(), id, parent);
        view.hostWindow = window;
        view.dialog = this;
        view.setClipChildren(false);
        setContentView(view);
        disableClipping(window.getDecorView());
    }

    private static void disableClipping(View v) {
        if (!(v instanceof ViewGroup) || v instanceof KilnView) return;
        ViewGroup g = (ViewGroup) v;
        g.setClipChildren(false);
        for (int i = 0; i < g.getChildCount(); i++) disableClipping(g.getChildAt(i));
    }

    /** updateParameters (blur radii -1: unspecified; scrim NaN: the theme's dim). */
    void update(boolean dismissOnBackPress, boolean dismissOnClickOutside, boolean secure, boolean platformWidth,
                boolean decorFits, String title, float scrimAlpha, int blurBehindRadius, int backgroundBlurRadius, boolean rtl) {
        this.dismissOnBackPress = dismissOnBackPress;
        Window window = getWindow();
        window.setFlags(secure ? WindowManager.LayoutParams.FLAG_SECURE : 0, WindowManager.LayoutParams.FLAG_SECURE);
        view.setLayoutDirection(rtl ? View.LAYOUT_DIRECTION_RTL : View.LAYOUT_DIRECTION_LTR);
        // DialogLayout.updateProperties.
        boolean callSetLayout = !hasCalledSetLayout || platformWidth != usePlatformDefaultWidth || decorFits != decorFitsSystemWindows;
        usePlatformDefaultWidth = platformWidth;
        decorFitsSystemWindows = decorFits;
        if (callSetLayout) {
            int width = platformWidth ? ViewGroup.LayoutParams.WRAP_CONTENT : ViewGroup.LayoutParams.MATCH_PARENT;
            if (width != window.getAttributes().width || !hasCalledSetLayout) {
                window.setLayout(width, ViewGroup.LayoutParams.WRAP_CONTENT);
                hasCalledSetLayout = true;
            }
        }
        setCanceledOnTouchOutside(dismissOnClickOutside);
        this.dismissOnClickOutside = dismissOnClickOutside;
        setTitle(title);
        if (Build.VERSION.SDK_INT >= 31) {
            if (blurBehindRadius >= 0) {
                if (blurBehindRadius > 0) {
                    window.addFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND);
                    WindowManager.LayoutParams attrs = window.getAttributes();
                    attrs.setBlurBehindRadius(blurBehindRadius);
                    window.setAttributes(attrs);
                } else {
                    window.clearFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND);
                }
            }
            if (backgroundBlurRadius >= 0) window.setBackgroundBlurRadius(backgroundBlurRadius);
        }
        if (!Float.isNaN(scrimAlpha)) window.setDimAmount(scrimAlpha);
        window.setSoftInputMode(decorFits ? WindowManager.LayoutParams.SOFT_INPUT_ADJUST_UNSPECIFIED
                                          : WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
    }

    private boolean dismissOnClickOutside = true;

    boolean usesPlatformDefaultWidth() { return usePlatformDefaultWidth; }

    @Override
    public void cancel() {}

    // A press outside the content (outside the window, or a part of it the content does not
    // cover) dismisses on its release.
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        boolean result = super.onTouchEvent(event);
        int[] at = new int[2];
        view.getLocationInWindow(at);
        boolean inside = view.insideContent(event.getX() - at[0], event.getY() - at[1]);
        if (dismissOnClickOutside && !inside) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    pressOutside = true;
                    result = true;
                    break;
                case MotionEvent.ACTION_UP:
                    if (pressOutside) {
                        KilnNative.dialogDismissRequest(id);
                        result = true;
                        pressOutside = false;
                    }
                    break;
                case MotionEvent.ACTION_CANCEL:
                    pressOutside = false;
                    break;
                default:
                    break;
            }
        } else {
            int a = event.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) pressOutside = false;
        }
        return result;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (dismissOnBackPress && event.isTracking() && !event.isCanceled() && keyCode == KeyEvent.KEYCODE_ESCAPE) {
            KilnNative.dialogDismissRequest(id);
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    /** Native code dismissing the window (the Dialog composable left the composition). */
    void dismissFromNative() {
        view.release();
        dismiss();
    }
}
