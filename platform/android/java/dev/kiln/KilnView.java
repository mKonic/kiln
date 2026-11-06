package dev.kiln;

import android.content.Context;
import android.content.res.Configuration;
import android.util.DisplayMetrics;
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
