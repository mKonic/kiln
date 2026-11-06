package dev.kiln;

import android.content.Context;
import android.content.res.Configuration;
import android.database.ContentObserver;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.graphics.Rect;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewConfiguration;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.FrameLayout;

/**
 * The View kiln is to Android. It owns the SurfaceView kiln renders into and is itself the
 * focusable, IME-target, accessibility and autofill View (architecture 3.1); those services
 * attach here in later milestones.
 */
public class KilnView extends FrameLayout implements SurfaceHolder.Callback {
    private final SurfaceView surfaceView;
    private final KilnText text;

    public KilnView(Context context) {
        super(context);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setDefaultFocusHighlightEnabled(false);
        setImportantForAutofill(IMPORTANT_FOR_AUTOFILL_YES);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        setForceDarkAllowed(false);
        setWillNotDraw(false);

        surfaceView = new SurfaceView(context);
        surfaceView.getHolder().addCallback(this);
        addView(surfaceView, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        text = new KilnText(context);
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        KilnNative.create(this, text, metrics.density, getResources().getConfiguration().fontScale);

        // ANIMATOR_DURATION_SCALE, observed for changes (WindowRecomposer:95-135).
        animationScaleObserver = new ContentObserver(new Handler(Looper.getMainLooper())) {
            @Override public void onChange(boolean selfChange) { pushAnimationScale(); }
        };
        context.getContentResolver().registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE), false,
                animationScaleObserver);
        pushAnimationScale();
        pushViewConfiguration();
    }

    private void pushViewConfiguration() {
        ViewConfiguration vc = ViewConfiguration.get(getContext());
        KilnNative.setViewConfiguration(ViewConfiguration.getLongPressTimeout(),
                ViewConfiguration.getDoubleTapTimeout(), vc.getScaledTouchSlop(),
                vc.getScaledMaximumFlingVelocity(), vc.getScaledMinimumFlingVelocity());
    }

    private int[] ids = new int[4];
    private float[] xy = new float[8];
    private int[] tools = new int[4];

    private boolean forward(MotionEvent e) {
        final int n = e.getPointerCount();
        if (ids.length < n) { ids = new int[n]; xy = new float[2 * n]; tools = new int[n]; }
        for (int i = 0; i < n; i++) {
            ids[i] = e.getPointerId(i);
            xy[2 * i] = e.getX(i);
            xy[2 * i + 1] = e.getY(i);
            tools[i] = e.getToolType(i);
        }
        int[] idsN = java.util.Arrays.copyOf(ids, n);
        float[] xyN = java.util.Arrays.copyOf(xy, 2 * n);
        int[] toolsN = java.util.Arrays.copyOf(tools, n);
        KilnNative.touchEvent(e.getActionMasked(), e.getActionIndex(), e.getEventTime(), idsN, xyN, toolsN,
                e.getButtonState(), e.getClassification(),
                e.getAxisValue(MotionEvent.AXIS_HSCROLL), e.getAxisValue(MotionEvent.AXIS_VSCROLL));
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) { return forward(e); }

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        final int a = e.getActionMasked();
        if (a == MotionEvent.ACTION_HOVER_MOVE || a == MotionEvent.ACTION_HOVER_ENTER
                || a == MotionEvent.ACTION_HOVER_EXIT || a == MotionEvent.ACTION_SCROLL) return forward(e);
        return super.onGenericMotionEvent(e);
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent e) { return true; }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (KilnNative.keyEvent(e.getAction(), e.getKeyCode(), e.getMetaState(), e.getUnicodeChar(),
                e.getRepeatCount(), e.getEventTime())) return true;
        return super.dispatchKeyEvent(e);
    }

    @Override
    protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
        KilnNative.hostFocusChanged(gainFocus);
    }   // the SurfaceView child never sees touches

    private final ContentObserver animationScaleObserver;

    private void pushAnimationScale() {
        KilnNative.setAnimationScale(Settings.Global.getFloat(getContext().getContentResolver(),
                Settings.Global.ANIMATOR_DURATION_SCALE, 1.0f));
    }

    void onHostStart() { KilnNative.setVisible(true); }
    void onHostStop() { KilnNative.setVisible(false); }

    void onHostConfigurationChanged(Configuration config) {
        KilnNative.setDensity(getResources().getDisplayMetrics().density, config.fontScale);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        KilnNative.surfaceCreated(holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        KilnNative.surfaceChanged(width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        KilnNative.surfaceDestroyed();
    }
}
