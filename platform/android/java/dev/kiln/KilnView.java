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
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeProvider;
import android.view.WindowInsets;
import android.view.WindowInsetsAnimation;
import java.util.List;
import android.view.ViewConfiguration;
import android.graphics.Matrix;
import android.view.inputmethod.CursorAnchorInfo;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
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

    public KilnView(Context context) { this(context, null); }

    /** savedState: the bytes from {@link #saveState()} of the previous instance, or null. */
    public KilnView(Context context, byte[] savedState) {
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
        KilnNative.create(this, text, context.getAssets(), metrics.density, getResources().getConfiguration().fontScale, savedState);

        // ANIMATOR_DURATION_SCALE, observed for changes (WindowRecomposer:95-135).
        animationScaleObserver = new ContentObserver(new Handler(Looper.getMainLooper())) {
            @Override public void onChange(boolean selfChange) { pushAnimationScale(); }
        };
        context.getContentResolver().registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE), false,
                animationScaleObserver);
        pushAnimationScale();
        pushViewConfiguration();
        installInsets();
    }

    // --- window insets (spec 02 req. 15) ----------------------------------------------------
    // Order matches kiln::ui::InsetsType.
    private static final int[] TYPES = {
        WindowInsets.Type.statusBars(), WindowInsets.Type.navigationBars(), WindowInsets.Type.captionBar(),
        WindowInsets.Type.ime(), WindowInsets.Type.displayCutout(), WindowInsets.Type.systemGestures(),
        WindowInsets.Type.mandatorySystemGestures(), WindowInsets.Type.tappableElement(), 0 /* waterfall below */ };
    private boolean imeAnimating;

    private void pushInsets(WindowInsets insets, boolean skipIme) {
        for (int i = 0; i < TYPES.length; i++) {
            if (skipIme && i == 3) continue;
            android.graphics.Insets v;
            if (i == 8) {
                android.view.DisplayCutout c = insets.getDisplayCutout();
                v = c != null ? c.getWaterfallInsets() : android.graphics.Insets.NONE;
            } else {
                v = insets.getInsets(TYPES[i]);
            }
            KilnNative.setInsets(i, v.left, v.top, v.right, v.bottom);
        }
        KilnNative.setImeState(insets.isVisible(WindowInsets.Type.ime()), imeAnimating);
    }

    private void installInsets() {
        setOnApplyWindowInsetsListener((v, insets) -> {
            // While the IME animates, onProgress drives its insets frame by frame.
            pushInsets(insets, imeAnimating);
            return insets;
        });
        setWindowInsetsAnimationCallback(new WindowInsetsAnimation.Callback(
                WindowInsetsAnimation.Callback.DISPATCH_MODE_STOP) {
            @Override public void onPrepare(WindowInsetsAnimation animation) {
                if ((animation.getTypeMask() & WindowInsets.Type.ime()) != 0) imeAnimating = true;
            }
            @Override public WindowInsets onProgress(WindowInsets insets, List<WindowInsetsAnimation> running) {
                android.graphics.Insets ime = insets.getInsets(WindowInsets.Type.ime());
                KilnNative.setInsets(3, ime.left, ime.top, ime.right, ime.bottom);
                return insets;
            }
            @Override public void onEnd(WindowInsetsAnimation animation) {
                if ((animation.getTypeMask() & WindowInsets.Type.ime()) == 0) return;
                imeAnimating = false;
                WindowInsets now = getRootWindowInsets();
                if (now != null) pushInsets(now, false);
            }
        });
        requestApplyInsets();
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
        // Batched samples since the last event (velocity tracking uses them).
        long[] historyTimes = null;
        float[] historyXy = null;
        final int hs = e.getHistorySize();
        if (hs > 0) {
            historyTimes = new long[hs];
            historyXy = new float[hs * n * 2];
            for (int h = 0; h < hs; h++) {
                historyTimes[h] = e.getHistoricalEventTime(h);
                for (int i = 0; i < n; i++) {
                    historyXy[(h * n + i) * 2] = e.getHistoricalX(i, h);
                    historyXy[(h * n + i) * 2 + 1] = e.getHistoricalY(i, h);
                }
            }
        }
        // Trackpad swipes: the gesture scroll distance now and per batched sample (pointer 0).
        // Intermediate events of a swipe do not repeat the classification, so send it for every
        // touchpad / mouse event and let native track the gesture.
        float[] pan = null;
        if (e.isFromSource(InputDevice.SOURCE_TOUCHPAD) || e.isFromSource(InputDevice.SOURCE_MOUSE)) {
            pan = new float[2 + 2 * hs];
            pan[0] = e.getAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_X_DISTANCE, 0);
            pan[1] = e.getAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_Y_DISTANCE, 0);
            for (int h = 0; h < hs; h++) {
                pan[2 + 2 * h] = e.getHistoricalAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_X_DISTANCE, 0, h);
                pan[3 + 2 * h] = e.getHistoricalAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_Y_DISTANCE, 0, h);
            }
        }
        KilnNative.touchEvent(e.getActionMasked(), e.getActionIndex(), e.getEventTime(), idsN, xyN, toolsN,
                e.getButtonState(), e.getClassification(),
                e.getAxisValue(MotionEvent.AXIS_HSCROLL), e.getAxisValue(MotionEvent.AXIS_VSCROLL),
                historyTimes, historyXy, pan);
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

    // --- text input (called from native; spec 02 req. 12) -----------------------------------
    private boolean inputActive;
    private int inputType, imeOptions;
    private boolean singleLine;
    private KilnInputConnection connection;
    private int batch;
    private boolean selectionPending;
    private int[] pendingSelection = new int[4];

    private InputMethodManager imm() { return getContext().getSystemService(InputMethodManager.class); }

    /** A field took focus: new EditorInfo, then show the keyboard (coalesced restart + show). */
    void startInput(int type, int options, boolean singleLine) {
        this.inputActive = true;
        this.inputType = type;
        this.imeOptions = options;
        this.singleLine = singleLine;
        if (!hasFocus()) requestFocus();
        imm().restartInput(this);
        imm().showSoftInput(this, 0);
    }

    void stopInput() {
        if (!inputActive) return;
        inputActive = false;
        imm().hideSoftInputFromWindow(getWindowToken(), 0);
        imm().restartInput(this);
    }

    void restartInputFromNative() { if (inputActive) imm().restartInput(this); }

    /** InputMethodManager.updateSelection, held back until the IME's batch edit ends. */
    void updateSelection(int selStart, int selEnd, int compStart, int compEnd) {
        pendingSelection[0] = selStart; pendingSelection[1] = selEnd;
        pendingSelection[2] = compStart; pendingSelection[3] = compEnd;
        if (batch > 0) { selectionPending = true; return; }
        imm().updateSelection(this, selStart, selEnd, compStart, compEnd);
        if (cursorMonitoring) sendCursorAnchorInfo();
    }

    private boolean cursorMonitoring;
    void setCursorMonitoring(boolean on) { cursorMonitoring = on; }

    /** CursorAnchorInfo: selection, composing text and the caret, mapped to screen coordinates. */
    void sendCursorAnchorInfo() {
        if (!inputActive) return;
        int[] sel = KilnNative.imeGetSelection();
        float[] caret = KilnNative.imeGetCaret();
        String text = KilnNative.imeGetText();
        CursorAnchorInfo.Builder b = new CursorAnchorInfo.Builder();
        b.setSelectionRange(sel[0], sel[1]);
        if (sel[2] >= 0 && sel[3] <= text.length()) b.setComposingText(sel[2], text.substring(sel[2], sel[3]));
        if (caret[4] != 0) b.setInsertionMarkerLocation(caret[0], caret[1], caret[2], caret[3], CursorAnchorInfo.FLAG_HAS_VISIBLE_REGION);
        int[] loc = new int[2];
        getLocationOnScreen(loc);
        Matrix m = new Matrix();
        m.setTranslate(loc[0], loc[1]);
        b.setMatrix(m);
        imm().updateCursorAnchorInfo(this, b.build());
    }

    void beginBatch() { batch++; }
    void endBatch() {
        if (batch > 0) batch--;
        if (batch == 0 && selectionPending) {
            selectionPending = false;
            imm().updateSelection(this, pendingSelection[0], pendingSelection[1], pendingSelection[2], pendingSelection[3]);
            if (cursorMonitoring) sendCursorAnchorInfo();
        }
    }

    boolean isCurrentConnection(KilnInputConnection c) { return c == connection && inputActive; }
    boolean isSingleLineInput() { return singleLine; }

    // --- accessibility (spec 01) --------------------------------------------------------------
    private final KilnAccessibility accessibility = new KilnAccessibility(this);
    private boolean contentChangePosted;

    @Override
    public AccessibilityNodeProvider getAccessibilityNodeProvider() { return accessibility; }

    /** Native semantics changed: one content-changed event per 100 ms while a service listens. */
    void onSemanticsChanged() {
        AccessibilityManager am = getContext().getSystemService(AccessibilityManager.class);
        if (am == null || !am.isEnabled() || contentChangePosted) return;
        contentChangePosted = true;
        postDelayed(() -> {
            contentChangePosted = false;
            // Services can go away during the delay; sending with accessibility off throws.
            if (!am.isEnabled()) return;
            AccessibilityEvent e = AccessibilityEvent.obtain(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
            e.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
            e.setPackageName(getContext().getPackageName());
            e.setSource(this);
            if (getParent() != null) getParent().requestSendAccessibilityEvent(this, e);
            accessibility.sendScrollEvents();
        }, 100);
    }

    @Override
    public boolean dispatchHoverEvent(MotionEvent e) {
        AccessibilityManager am = getContext().getSystemService(AccessibilityManager.class);
        if (am != null && am.isTouchExplorationEnabled()
                && accessibility.onHover(e.getActionMasked(), e.getX(), e.getY())) return true;
        return super.dispatchHoverEvent(e);
    }

    @Override
    public boolean onCheckIsTextEditor() { return inputActive; }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo out) {
        if (!inputActive) return null;
        out.inputType = inputType;
        out.imeOptions = imeOptions;
        int[] sel = KilnNative.imeGetSelection();
        out.initialSelStart = sel[0];
        out.initialSelEnd = sel[1];
        if (android.os.Build.VERSION.SDK_INT >= 30) out.setInitialSurroundingText(KilnNative.imeGetText());
        if (connection != null) connection.closeConnection();
        connection = new KilnInputConnection(this);
        batch = 0;
        return connection;
    }

    // --- back (spec 15 req. 2) --------------------------------------------------------------
    final KilnBack back = new KilnBack(this);

    /** From native when the first handler becomes enabled or the last one disabled. */
    void setBackEnabled(boolean enabled) { back.setEnabled(enabled); }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        back.onAttached();
        KilnNative.setLayoutDirection(getLayoutDirection() == LAYOUT_DIRECTION_RTL);
    }

    @Override
    protected void onDetachedFromWindow() {
        back.onDetached();
        super.onDetachedFromWindow();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (back.onKey(e)) return true;
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

    // LocalLayoutDirection follows the view (locale, android:layoutDirection, RTL forced in
    // developer options).
    @Override
    public void onRtlPropertiesChanged(int layoutDirection) {
        super.onRtlPropertiesChanged(layoutDirection);
        KilnNative.setLayoutDirection(layoutDirection == LAYOUT_DIRECTION_RTL);
    }

    private void pushAnimationScale() {
        KilnNative.setAnimationScale(Settings.Global.getFloat(getContext().getContentResolver(),
                Settings.Global.ANIMATOR_DURATION_SCALE, 1.0f));
    }

    /** rememberSaveable values, serialised (DisposableSaveableStateRegistry's bundle entry). */
    public byte[] saveState() { return KilnNative.saveState(); }

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
