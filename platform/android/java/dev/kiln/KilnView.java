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
        KilnNative.setFontWeightAdjustment(fontWeightAdjustment(getResources().getConfiguration()));
        KilnNative.windowFocusChanged(hasWindowFocus());

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
    // WindowInsetsWatcher: insets that arrive while an animation is being prepared wait for
    // onStart, which starts the animation of each type whose insets change; while types
    // animate, onProgress delivers their insets frame by frame.
    private boolean insetsPrepared;
    private int runningAnimationMask;
    private WindowInsets savedInsets, currentInsets;

    /** current (4), maximum (4) and visible (1) per type, then the display cutout rects. */
    private void pushInsets(WindowInsets insets) {
        android.view.DisplayCutout cutout = insets.getDisplayCutout();
        android.graphics.Insets waterfall = cutout != null ? cutout.getWaterfallInsets() : android.graphics.Insets.NONE;
        java.util.List<Rect> rects = cutout != null ? cutout.getBoundingRects() : java.util.Collections.emptyList();
        if (currentInsets == null) {
            // Nothing to report yet: the rulers keep their defaults until there is.
            boolean any = !waterfall.equals(android.graphics.Insets.NONE) || !rects.isEmpty();
            for (int i = 0; i < TYPES.length - 1 && !any; i++) {
                android.graphics.Insets v = i == 3 ? insets.getInsets(TYPES[i]) : insets.getInsetsIgnoringVisibility(TYPES[i]);
                any = !v.equals(android.graphics.Insets.NONE);
            }
            if (!any) return;
        }
        currentInsets = insets;
        // Flutter's TextInputPlugin: when the keyboard hides, restart the input so keyboards that
        // cache state (Samsung Keyboard) start fresh.
        final boolean imeVisible = insets.isVisible(WindowInsets.Type.ime());
        if (imeWasVisible && !imeVisible && inputActive) imm().restartInput(this);
        imeWasVisible = imeVisible;
        int[] data = new int[TYPES.length * 9 + 1 + rects.size() * 4];
        for (int i = 0; i < TYPES.length; i++) {
            android.graphics.Insets cur, max;
            boolean visible;
            if (i == 8) {
                cur = max = waterfall;
                visible = !waterfall.equals(android.graphics.Insets.NONE);
            } else {
                cur = insets.getInsets(TYPES[i]);
                max = i == 3 ? android.graphics.Insets.NONE : insets.getInsetsIgnoringVisibility(TYPES[i]);
                visible = insets.isVisible(TYPES[i]);
            }
            int o = i * 9;
            data[o] = cur.left; data[o + 1] = cur.top; data[o + 2] = cur.right; data[o + 3] = cur.bottom;
            data[o + 4] = max.left; data[o + 5] = max.top; data[o + 6] = max.right; data[o + 7] = max.bottom;
            data[o + 8] = visible ? 1 : 0;
        }
        int o = TYPES.length * 9;
        data[o++] = rects.size();
        for (Rect r : rects) { data[o++] = r.left; data[o++] = r.top; data[o++] = r.right; data[o++] = r.bottom; }
        KilnNative.setWindowInsets(data);
    }

    private void sendAnimation(int typeMask, WindowInsetsAnimation a, android.graphics.Insets[] positions) {
        for (int i = 0; i < TYPES.length - 1; i++) {
            if ((typeMask & TYPES[i]) == 0) continue;
            int[] bounds = null;
            if (positions != null) {
                android.graphics.Insets s = positions[0], t = positions[1];
                bounds = new int[] { s.left, s.top, s.right, s.bottom, t.left, t.top, t.right, t.bottom };
            }
            if (a == null) KilnNative.setInsetsAnimation(i, false, null, 0, 1, 0);
            else KilnNative.setInsetsAnimation(i, true, bounds, a.getInterpolatedFraction(), a.getAlpha(), a.getDurationMillis());
        }
    }

    private void installInsets() {
        setOnApplyWindowInsetsListener((v, insets) -> {
            if (insetsPrepared) {
                savedInsets = insets;   // for onStart
                if (android.os.Build.VERSION.SDK_INT == 30) post(() -> {
                    // API 30 may not start the animation: apply what was saved.
                    if (!insetsPrepared) return;
                    runningAnimationMask = 0;
                    insetsPrepared = false;
                    if (savedInsets != null) { pushInsets(savedInsets); savedInsets = null; }
                });
            } else if (runningAnimationMask == 0) {
                pushInsets(insets);
            }
            return insets;
        });
        setWindowInsetsAnimationCallback(new WindowInsetsAnimation.Callback(
                WindowInsetsAnimation.Callback.DISPATCH_MODE_STOP) {
            @Override public void onPrepare(WindowInsetsAnimation animation) { insetsPrepared = true; }
            @Override public WindowInsetsAnimation.Bounds onStart(WindowInsetsAnimation animation, WindowInsetsAnimation.Bounds bounds) {
                WindowInsets insets = savedInsets;
                insetsPrepared = false;
                savedInsets = null;
                if (animation.getDurationMillis() != 0 && insets != null && currentInsets != null) {
                    for (int i = 0; i < TYPES.length - 1; i++) {
                        int type = TYPES[i];
                        if ((animation.getTypeMask() & type) == 0) continue;
                        android.graphics.Insets current = currentInsets.getInsets(type), target = insets.getInsets(type);
                        if (target.equals(current)) continue;
                        runningAnimationMask |= type;
                        sendAnimation(type, animation, new android.graphics.Insets[] { current, target });
                    }
                }
                return bounds;
            }
            @Override public WindowInsets onProgress(WindowInsets insets, List<WindowInsetsAnimation> running) {
                for (WindowInsetsAnimation a : running) {
                    int mask = a.getTypeMask() & runningAnimationMask;
                    if (mask != 0) sendAnimation(mask, a, null);
                }
                pushInsets(insets);
                return insets;
            }
            @Override public void onEnd(WindowInsetsAnimation animation) {
                insetsPrepared = false;
                int mask = animation.getTypeMask() & runningAnimationMask;
                if (mask != 0) sendAnimation(mask, null, null);
                runningAnimationMask &= ~animation.getTypeMask();
                savedInsets = null;
                WindowInsets now = getRootWindowInsets();
                if (now != null && runningAnimationMask == 0) pushInsets(now);
            }
        });
        requestApplyInsets();
    }

    /** WindowInsetsController appearance: dark status / navigation bar icons over light content. */
    void setSystemBarsAppearance(boolean lightStatus, boolean lightNavigation) {
        android.view.WindowInsetsController c = getWindowInsetsController();
        if (c == null) return;
        final int status = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS;
        final int nav = android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
        c.setSystemBarsAppearance((lightStatus ? status : 0) | (lightNavigation ? nav : 0), status | nav);
    }

    // --- imeNestedScroll: direct control of a type's insets (API 30+) ---------------------------
    private android.view.WindowInsetsAnimationController insetsController;

    boolean requestInsetsControl(int typeIndex) {
        if (android.os.Build.VERSION.SDK_INT < 30 || typeIndex < 0 || typeIndex >= TYPES.length - 1) return false;
        android.view.WindowInsetsController c = getWindowInsetsController();
        if (c == null) return false;
        c.controlWindowInsetsAnimation(TYPES[typeIndex], -1, null, new android.os.CancellationSignal(),
            new android.view.WindowInsetsAnimationControlListener() {
                @Override public void onReady(android.view.WindowInsetsAnimationController controller, int types) {
                    insetsController = controller;
                    android.graphics.Insets h = controller.getHiddenStateInsets(), s = controller.getShownStateInsets(), cur = controller.getCurrentInsets();
                    KilnNative.insetsControlReady(new int[] { h.left, h.top, h.right, h.bottom, s.left, s.top, s.right, s.bottom,
                                                              cur.left, cur.top, cur.right, cur.bottom });
                }
                @Override public void onFinished(android.view.WindowInsetsAnimationController controller) { ended(); }
                @Override public void onCancelled(android.view.WindowInsetsAnimationController controller) { ended(); }
                private void ended() {
                    insetsController = null;
                    KilnNative.insetsControlEnded();
                }
            });
        return true;
    }

    void setControlledInsets(int left, int top, int right, int bottom, float alpha, float progress) {
        if (android.os.Build.VERSION.SDK_INT >= 30 && insetsController != null && insetsController.isReady())
            insetsController.setInsetsAndAlpha(android.graphics.Insets.of(left, top, right, bottom), alpha, progress);
    }

    void finishInsetsControl(boolean shown) {
        if (android.os.Build.VERSION.SDK_INT >= 30 && insetsController != null && insetsController.isReady()) insetsController.finish(shown);
    }

    private void pushViewConfiguration() {
        ViewConfiguration vc = ViewConfiguration.get(getContext());
        KilnNative.setViewConfiguration(ViewConfiguration.getLongPressTimeout(),
                ViewConfiguration.getDoubleTapTimeout(), vc.getScaledTouchSlop(),
                vc.getScaledMaximumFlingVelocity(), vc.getScaledMinimumFlingVelocity(),
                android.os.Build.VERSION.SDK_INT >= 34 ? vc.getScaledHandwritingSlop() : 2f,
                android.os.Build.VERSION.SDK_INT >= 34 ? vc.getScaledHandwritingGestureLineMargin() : 16f);
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
        // Trackpad gestures: the swipe scroll distance and the pinch scale factor, now and per
        // batched sample (pointer 0), as (x, y, scale) triples. Intermediate events of a swipe
        // do not repeat the classification, so send them for every touchpad / mouse event and
        // let native track the gesture.
        float[] pan = null;
        if (e.isFromSource(InputDevice.SOURCE_TOUCHPAD) || e.isFromSource(InputDevice.SOURCE_MOUSE)) {
            pan = new float[3 + 3 * hs];
            pan[0] = e.getAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_X_DISTANCE, 0);
            pan[1] = e.getAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_Y_DISTANCE, 0);
            pan[2] = e.getAxisValue(MotionEvent.AXIS_GESTURE_PINCH_SCALE_FACTOR, 0);
            for (int h = 0; h < hs; h++) {
                pan[3 + 3 * h] = e.getHistoricalAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_X_DISTANCE, 0, h);
                pan[4 + 3 * h] = e.getHistoricalAxisValue(MotionEvent.AXIS_GESTURE_SCROLL_Y_DISTANCE, 0, h);
                pan[5 + 3 * h] = e.getHistoricalAxisValue(MotionEvent.AXIS_GESTURE_PINCH_SCALE_FACTOR, 0, h);
            }
        }
        KilnNative.touchEvent(e.getActionMasked(), e.getActionIndex(), e.getEventTime(), idsN, xyN, toolsN,
                e.getButtonState(), e.getMetaState(), e.getClassification(),
                e.getAxisValue(MotionEvent.AXIS_HSCROLL), e.getAxisValue(MotionEvent.AXIS_VSCROLL),
                historyTimes, historyXy, pan);
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) { return forward(e); }

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        final int a = e.getActionMasked();
        // Rotary encoders (AndroidComposeView.handleRotaryEvent): AXIS_SCROLL, inverted, in pixels.
        if (a == MotionEvent.ACTION_SCROLL && e.isFromSource(android.view.InputDevice.SOURCE_ROTARY_ENCODER)) {
            android.view.ViewConfiguration vc = android.view.ViewConfiguration.get(getContext());
            final float v = -e.getAxisValue(MotionEvent.AXIS_SCROLL);
            if (KilnNative.rotary(v * vc.getScaledVerticalScrollFactor(), v * vc.getScaledHorizontalScrollFactor(),
                    e.getEventTime(), e.getDeviceId())) return true;
            return super.onGenericMotionEvent(e);
        }
        if (e.isFromSource(InputDevice.SOURCE_TOUCH_NAVIGATION)) return indirect(e);
        if (a == MotionEvent.ACTION_HOVER_EXIT && e.getX() >= 0 && e.getY() >= 0
                && e.getX() < getWidth() && e.getY() < getHeight()) {
            // A mouse button press: not leaving at all.
            if (e.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE && e.getButtonState() != 0) return true;
            // Maybe a stylus or mouse about to press: hold the exit until the next event says so.
            pendingHoverExit = MotionEvent.obtainNoHistory(e);
            postDelayed(sendHoverExit, HOVER_EXIT_DELAY_MS);
            return true;
        }
        if (a == MotionEvent.ACTION_HOVER_MOVE || a == MotionEvent.ACTION_HOVER_ENTER
                || a == MotionEvent.ACTION_HOVER_EXIT || a == MotionEvent.ACTION_SCROLL) return forward(e);
        return super.onGenericMotionEvent(e);
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent e) { return true; }

    // --- deferred hover exit (AndroidComposeView.sendHoverExitEvent) ---------------------------
    // A stylus or mouse sends HOVER_EXIT right before it presses down. The exit waits one 120 Hz
    // frame: a DOWN from the same device drops it, anything else sends it first.
    private static final long HOVER_EXIT_DELAY_MS = 8;
    private MotionEvent pendingHoverExit;
    private final Runnable sendHoverExit = () -> {
        MotionEvent e = pendingHoverExit;
        pendingHoverExit = null;
        if (e != null) { forward(e); e.recycle(); }
    };

    private void flushHoverExit(boolean drop) {
        if (pendingHoverExit == null) return;
        removeCallbacks(sendHoverExit);
        if (drop) { pendingHoverExit.recycle(); pendingHoverExit = null; }
        else sendHoverExit.run();
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent e) {
        if (e.getActionMasked() == MotionEvent.ACTION_DOWN) { lastDownX = e.getX(); lastDownY = e.getY(); }
        if (pendingHoverExit != null) {
            final MotionEvent last = pendingHoverExit;
            final boolean sameDevice = last.getSource() == e.getSource() && last.getToolType(0) == e.getToolType(0);
            flushHoverExit(e.getActionMasked() == MotionEvent.ACTION_DOWN && sameDevice);
        }
        return super.dispatchTouchEvent(e);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent e) {
        // An exit right before a scroll is spurious (screen mirroring adds them).
        flushHoverExit(e.getActionMasked() == MotionEvent.ACTION_SCROLL);
        return super.dispatchGenericMotionEvent(e);
    }

    // --- indirect pointers (touch-navigation pads) --------------------------------------------
    // The focused path sees every event first; a fling the path did not consume moves focus along
    // the pad's axis (AndroidComposeView's IndirectPointerNavigationGestureDetector).
    private android.view.GestureDetector navDetector;
    private int navAxis;               // 0 none, 1 x, 2 y
    private boolean navIgnoreStream;

    private boolean indirect(MotionEvent e) {
        final int a = e.getActionMasked();
        final int axis = indirectAxis(e);
        final int n = e.getPointerCount();
        int[] pids = new int[n];
        float[] pxy = new float[2 * n];
        float[] pressures = new float[n];
        for (int i = 0; i < n; i++) {
            pids[i] = e.getPointerId(i);
            pxy[2 * i] = e.getX(i);
            pxy[2 * i + 1] = e.getY(i);
            pressures[i] = e.getPressure(i);
        }
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
        final boolean consumed = KilnNative.indirectPointer(a, e.getActionIndex(), e.getEventTime(), pids, pxy,
                pressures, historyTimes, historyXy, axis);
        if (a == MotionEvent.ACTION_CANCEL) {
            navAxis = 0;
            navIgnoreStream = true;
            return true;
        }
        switch (e.getAction()) {
            case MotionEvent.ACTION_DOWN:
                navAxis = axis;
                navIgnoreStream = false;
                break;
            case MotionEvent.ACTION_MOVE:
            case MotionEvent.ACTION_UP:
                if (consumed) { navAxis = 0; navIgnoreStream = true; }
                break;
        }
        if (navDetector == null) {
            navDetector = new android.view.GestureDetector(getContext(), new android.view.GestureDetector.SimpleOnGestureListener() {
                @Override public boolean onDown(MotionEvent ev) { return true; }
                @Override public boolean onFling(MotionEvent e1, MotionEvent e2, float vx, float vy) {
                    if (navIgnoreStream) return true;
                    if (navAxis == 1 && Math.abs(vx) > Math.abs(vy)) KilnNative.indirectFling(vx > 0);
                    else if (navAxis == 2 && Math.abs(vy) > Math.abs(vx)) KilnNative.indirectFling(vy > 0);
                    return true;
                }
            }, getHandler());
        }
        navDetector.onTouchEvent(e);
        return true;
    }

    // indirectPrimaryDirectionalScrollAxis: the axis with a motion range, or the much longer one.
    private static int indirectAxis(MotionEvent e) {
        InputDevice d = e.getDevice();
        if (d == null) return 0;
        InputDevice.MotionRange x = d.getMotionRange(MotionEvent.AXIS_X);
        InputDevice.MotionRange y = d.getMotionRange(MotionEvent.AXIS_Y);
        if (x != null && y == null) return 1;
        if (y != null && x == null) return 2;
        if (x != null && y != null) {
            final float xr = x.getRange(), yr = y.getRange();
            if (xr > yr && (yr == 0f || xr / yr >= 5f)) return 1;
            if (yr > xr && (xr == 0f || yr / xr >= 5f)) return 2;
        }
        return 0;
    }

    // --- text input (called from native; spec 02 req. 12) -----------------------------------
    private boolean inputActive;
    private int inputType, imeOptions;
    private String hintLocales = "";
    private boolean acceptsContent;
    private boolean imeWasVisible;
    private boolean stylusHandwriting;

    /** ComposeInputMethodManagerImplApi34.startStylusHandwriting. */
    void startStylusHandwriting() {
        if (inputActive && android.os.Build.VERSION.SDK_INT >= 34) imm().startStylusHandwriting(this);
    }
    // InputContentInfo whose URI permission the keyboard granted, by handle, until released.
    private final android.util.SparseArray<android.view.inputmethod.InputContentInfo> contentPermissions = new android.util.SparseArray<>();
    private int nextContentPermission = 1;

    boolean acceptsContent() { return acceptsContent; }

    int holdContentPermission(android.view.inputmethod.InputContentInfo info) {
        final int handle = nextContentPermission++;
        contentPermissions.put(handle, info);
        return handle;
    }

    void releaseContentPermission(int handle) {
        android.view.inputmethod.InputContentInfo info = contentPermissions.get(handle);
        if (info == null) return;
        contentPermissions.remove(handle);
        info.releasePermission();
    }

    /** ContentResolver.openInputStream: the bytes behind a content URI, or null. */
    byte[] readContent(String uri) {
        try (java.io.InputStream in = getContext().getContentResolver().openInputStream(android.net.Uri.parse(uri))) {
            if (in == null) return null;
            return in.readAllBytes();
        } catch (Exception e) {
            return null;
        }
    }
    private KilnInputConnection connection;
    // getExtractedText(GET_EXTRACTED_TEXT_MONITOR): the IME's token for updateExtractedText.
    private boolean extractedTextMonitor;
    private int extractedTextToken;

    private InputMethodManager imm() { return getContext().getSystemService(InputMethodManager.class); }

    /** A field took focus: new EditorInfo, then show the keyboard (coalesced restart + show). */
    void startInput(int type, int options, boolean singleLine, String hintLocales, boolean acceptsContent, boolean stylusHandwriting) {
        this.inputActive = true;
        this.stylusHandwriting = stylusHandwriting;
        this.acceptsContent = acceptsContent;
        this.inputType = type;
        this.imeOptions = options;
        this.hintLocales = hintLocales;
        requestOwnerFocus();
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

    void showKeyboard() { if (inputActive) imm().showSoftInput(this, 0); }

    void hideKeyboard() { imm().hideSoftInputFromWindow(getWindowToken(), 0); }

    /** The field changed without a restart (AndroidTextInputSession.collectImeNotifications). */
    void imeUpdate(int selStart, int selEnd, int compStart, int compEnd, boolean selectionChanged) {
        if (!inputActive) return;
        if (extractedTextMonitor) {
            imm().updateExtractedText(this, extractedTextToken,
                    KilnInputConnection.extractedText(KilnNative.imeGetText(), new int[] { selStart, selEnd }));
        }
        if (selectionChanged) imm().updateSelection(this, selStart, selEnd, compStart, compEnd);
    }

    void requestExtractedTextUpdates(int token) {
        extractedTextMonitor = true;
        extractedTextToken = token;
    }

    /** ComposeInputMethodManager.sendKeyEvent (API 24+). */
    void sendKeyEventFromInputMethod(KeyEvent e) { imm().dispatchKeyEventFromInputMethod(this, e); }

    /**
     * CursorAnchorInfoBuilder.build from native data: matrix[9], selStart, selEnd, hasMarker, x,
     * top, baseline, bottom, markerFlags, composingStart, n, n x (index, l, t, r, b, flags),
     * hasEditorBounds, l, t, r, b, lines, lines x (l, t, r, b). Positions are in the text layout,
     * the matrix maps them to this view; the view's screen position is added here.
     */
    void cursorAnchorInfo(float[] d, String composingText) {
        if (!inputActive) return;
        Matrix m = new Matrix();
        m.setValues(java.util.Arrays.copyOf(d, 9));
        int[] loc = new int[2];
        getLocationOnScreen(loc);
        m.postTranslate(loc[0], loc[1]);
        CursorAnchorInfo.Builder b = new CursorAnchorInfo.Builder();
        b.setMatrix(m);
        int i = 9;
        b.setSelectionRange((int) d[i], (int) d[i + 1]);
        i += 2;
        if (d[i] != 0) b.setInsertionMarkerLocation(d[i + 1], d[i + 2], d[i + 3], d[i + 4], (int) d[i + 5]);
        i += 6;
        final int composingStart = (int) d[i++];
        if (composingStart >= 0) b.setComposingText(composingStart, composingText);
        final int chars = (int) d[i++];
        for (int k = 0; k < chars; k++, i += 6) b.addCharacterBounds((int) d[i], d[i + 1], d[i + 2], d[i + 3], d[i + 4], (int) d[i + 5]);
        if (d[i] != 0 && android.os.Build.VERSION.SDK_INT >= 33) {
            android.graphics.RectF r = new android.graphics.RectF(d[i + 1], d[i + 2], d[i + 3], d[i + 4]);
            b.setEditorBoundsInfo(new android.view.inputmethod.EditorBoundsInfo.Builder()
                    .setEditorBounds(r).setHandwritingBounds(r).build());
        }
        i += 5;
        final int lines = (int) d[i++];
        for (int k = 0; k < lines; k++, i += 4)
            if (android.os.Build.VERSION.SDK_INT >= 34) b.addVisibleLineBounds(d[i], d[i + 1], d[i + 2], d[i + 3]);
        imm().updateCursorAnchorInfo(this, b.build());
    }

    boolean isCurrentConnection(KilnInputConnection c) { return c == connection && inputActive; }

    // --- accessibility (spec 01) --------------------------------------------------------------
    private final KilnAccessibility accessibility = new KilnAccessibility(this);

    /** PackageManager.FEATURE_LEANBACK: focus scrolling pivots on TVs. */
    boolean isTelevision() {
        return getContext().getPackageManager().hasSystemFeature(android.content.pm.PackageManager.FEATURE_LEANBACK);
    }

    /** The root's bringIntoView: our parents (a ScrollView) scroll the rect into view. */
    private static int fontWeightAdjustment(android.content.res.Configuration config) {
        final int a = config.fontWeightAdjustment;
        return a == android.content.res.Configuration.FONT_WEIGHT_ADJUSTMENT_UNDEFINED ? 0 : a;
    }

    /** PointerIconService: the hovered content's icon (android.view.PointerIcon types). */
    void setPointerIconType(int type) {
        setPointerIcon(android.view.PointerIcon.getSystemIcon(getContext(), type));
    }

    /** AndroidUriHandler.openUri: ACTION_VIEW; false when no activity handles it. */
    boolean openUri(String uri) {
        try {
            getContext().startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(uri)));
            return true;
        } catch (android.content.ActivityNotFoundException e) {
            return false;
        }
    }

    // --- clipboard, haptics, text toolbar -------------------------------------------------------

    private android.content.ClipboardManager clipboard() {
        return (android.content.ClipboardManager) getContext().getSystemService(Context.CLIPBOARD_SERVICE);
    }

    /** AndroidClipboard.setClipEntry: plain text under the "plain text" label. */
    void setClipboardText(String text) {
        android.content.ClipboardManager cm = clipboard();
        if (cm != null) cm.setPrimaryClip(android.content.ClipData.newPlainText("plain text", text));
    }

    String getClipboardText() {
        android.content.ClipboardManager cm = clipboard();
        android.content.ClipData clip = cm != null ? cm.getPrimaryClip() : null;
        if (clip == null || clip.getItemCount() == 0) return null;
        CharSequence t = clip.getItemAt(0).getText();
        return t != null ? t.toString() : null;
    }

    /**
     * The whole primary clip for the receiveContent modifiers: its label and MIME types, then
     * (text, html, uri) per item; null without a clip.
     */
    String[] getClipboardClip() {
        android.content.ClipboardManager cm = clipboard();
        android.content.ClipData clip = cm != null ? cm.getPrimaryClip() : null;
        if (clip == null) return null;
        android.content.ClipDescription d = clip.getDescription();
        final int types = d.getMimeTypeCount();
        String[] out = new String[2 + types + clip.getItemCount() * 3];
        CharSequence label = d.getLabel();
        out[0] = label != null ? label.toString() : "";
        out[1] = Integer.toString(types);
        for (int i = 0; i < types; i++) out[2 + i] = d.getMimeType(i);
        for (int i = 0, k = 2 + types; i < clip.getItemCount(); i++, k += 3) {
            android.content.ClipData.Item item = clip.getItemAt(i);
            out[k] = item.getText() != null ? item.getText().toString() : null;
            out[k + 1] = item.getHtmlText();
            out[k + 2] = item.getUri() != null ? item.getUri().toString() : null;
        }
        return out;
    }

    /** The primary clip's label and MIME types, without reading it. */
    String[] getClipboardMetadata() {
        android.content.ClipboardManager cm = clipboard();
        android.content.ClipDescription d = cm != null ? cm.getPrimaryClipDescription() : null;
        if (d == null) return null;
        String[] out = new String[1 + d.getMimeTypeCount()];
        CharSequence label = d.getLabel();
        out[0] = label != null ? label.toString() : "";
        for (int i = 0; i < d.getMimeTypeCount(); i++) out[1 + i] = d.getMimeType(i);
        return out;
    }

    /** From the clip's description only: reading the clip itself shows the "pasted" toast. */
    boolean clipboardHasText() {
        android.content.ClipboardManager cm = clipboard();
        android.content.ClipDescription d = cm != null ? cm.getPrimaryClipDescription() : null;
        return d != null && d.hasMimeType("text/*");
    }

    /** PlatformHapticFeedback: View.performHapticFeedback(HapticFeedbackConstants.*). */
    void performHaptic(int constant) {
        performHapticFeedback(constant);
    }

    // AndroidTextContextMenuToolbarProvider: a floating ActionMode over the content rect, items
    // in the order cut, copy, paste, select all (bits 4, 1, 2, 8), always shown as actions.
    private android.view.ActionMode toolbarMode;
    private final Rect toolbarRect = new Rect();
    private int toolbarItems;

    void showTextToolbar(int left, int top, int right, int bottom, int items) {
        toolbarRect.set(left, top, right, bottom);
        toolbarItems = items;
        if (toolbarMode == null) {
            toolbarMode = startActionMode(toolbarCallback, android.view.ActionMode.TYPE_FLOATING);
        } else {
            toolbarMode.invalidate();
            toolbarMode.invalidateContentRect();
        }
    }

    void hideTextToolbar() {
        android.view.ActionMode mode = toolbarMode;
        toolbarMode = null;
        if (mode != null) mode.finish();
    }

    private static final int[][] TOOLBAR_ITEMS = {
        { 4, android.R.id.cut, android.R.string.cut },
        { 1, android.R.id.copy, android.R.string.copy },
        { 2, android.R.id.paste, android.R.string.paste },
        { 8, android.R.id.selectAll, android.R.string.selectAll },
    };

    private void fillToolbar(android.view.Menu menu) {
        menu.clear();
        int order = 1;
        for (int[] item : TOOLBAR_ITEMS) {
            if ((toolbarItems & item[0]) == 0) continue;
            menu.add(1, item[1], order++, item[2]).setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS);
        }
    }

    private final android.view.ActionMode.Callback2 toolbarCallback = new android.view.ActionMode.Callback2() {
        @Override public boolean onCreateActionMode(android.view.ActionMode mode, android.view.Menu menu) {
            fillToolbar(menu);
            return menu.size() > 0;
        }
        @Override public boolean onPrepareActionMode(android.view.ActionMode mode, android.view.Menu menu) {
            fillToolbar(menu);
            return true;
        }
        @Override public boolean onActionItemClicked(android.view.ActionMode mode, android.view.MenuItem item) {
            for (int[] i : TOOLBAR_ITEMS)
                if (i[1] == item.getItemId()) { KilnNative.textToolbarItem(i[0]); return true; }
            return false;
        }
        @Override public void onDestroyActionMode(android.view.ActionMode mode) {
            if (mode == toolbarMode) toolbarMode = null;
            KilnNative.textToolbarDestroyed();
        }
        @Override public void onGetContentRect(android.view.ActionMode mode, android.view.View view, Rect outRect) {
            outRect.set(toolbarRect);
        }
    };

    // View.playSoundEffect; the audio service may be gone (DeadSystemException).
    void playSound(int constant) {
        try { playSoundEffect(constant); } catch (RuntimeException ignored) {}
    }

    void requestRectangle(int left, int top, int right, int bottom) {
        requestRectangleOnScreen(new Rect(left, top, right, bottom), false);
    }

    // --- nested scrolling to our parents (called from native; NestedScrollInteropConnection) ---
    private final KilnNestedScrolling nestedScrolling = new KilnNestedScrolling(this);
    boolean nsStart(int axes, boolean touch) { return nestedScrolling.start(axes, touch ? 0 : 1); }
    int[] nsPreScroll(int dx, int dy, boolean touch) { return nestedScrolling.preScroll(dx, dy, touch ? 0 : 1); }
    int[] nsScroll(int dxc, int dyc, int dxu, int dyu, boolean touch) { return nestedScrolling.scroll(dxc, dyc, dxu, dyu, touch ? 0 : 1); }
    boolean nsPreFling(float vx, float vy) { return nestedScrolling.preFling(vx, vy); }
    boolean nsFling(float vx, float vy, boolean consumed) { return nestedScrolling.fling(vx, vy, consumed); }
    void nsStop() { nestedScrolling.stopAll(); }

    @Override
    public AccessibilityNodeProvider getAccessibilityNodeProvider() { return accessibility; }

    /** Native: semantics (false) or layout (true) changed while a service listens. */
    void onAccessibilityChange(boolean layout) { accessibility.onChange(layout); }

    boolean isTouchExplorationOn() { return accessibility.isTouchExplorationEnabled(); }

    /** AccessibilityManager.isRequestFromAccessibilityTool (API 34); before: every service is a tool. */
    boolean isRequestFromAccessibilityTool() {
        if (android.os.Build.VERSION.SDK_INT < 34) return true;
        AccessibilityManager am = getContext().getSystemService(AccessibilityManager.class);
        return am == null || am.isRequestFromAccessibilityTool();
    }

    private final AccessibilityManager.AccessibilityStateChangeListener a11yStateListener = on -> accessibility.updateEnabled();
    private final AccessibilityManager.TouchExplorationStateChangeListener touchExplorationListener = on -> accessibility.updateEnabled();
    private float lastDownX = Float.NaN, lastDownY = Float.NaN;

    // canScrollHorizontally / Vertically at the last press (AndroidComposeView): the scroll
    // containers there, from their semantics.
    @Override
    public boolean canScrollHorizontally(int direction) { return accessibility.canScroll(false, direction, lastDownX, lastDownY); }
    @Override
    public boolean canScrollVertically(int direction) { return accessibility.canScroll(true, direction, lastDownX, lastDownY); }

    @Override
    public boolean dispatchHoverEvent(MotionEvent e) {
        flushHoverExit(false);
        if (accessibility.isTouchExplorationEnabled() && accessibility.onHover(e.getActionMasked(), e.getX(), e.getY())) return true;
        return super.dispatchHoverEvent(e);
    }

    @Override
    public boolean onCheckIsTextEditor() { return inputActive; }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo out) {
        if (!inputActive) return null;
        out.inputType = inputType;
        out.imeOptions = imeOptions;
        // EditorInfo.update
        if (!hintLocales.isEmpty()) out.hintLocales = android.os.LocaleList.forLanguageTags(hintLocales);
        int[] sel = KilnNative.imeGetSelection();
        out.initialSelStart = sel[0];
        out.initialSelEnd = sel[1];
        out.setInitialSurroundingText(KilnNative.imeGetText());
        if (acceptsContent) out.contentMimeTypes = new String[] { "*/*", "image/*", "video/*" };
        // EditorInfoCompat.setStylusHandwritingEnabled: the platform flag on 35+, the extras key always.
        if (android.os.Build.VERSION.SDK_INT >= 35) out.setStylusHandwritingEnabled(stylusHandwriting);
        if (out.extras == null) out.extras = new android.os.Bundle();
        out.extras.putBoolean("androidx.core.view.inputmethod.EditorInfoCompat.STYLUS_HANDWRITING_ENABLED", stylusHandwriting);
        if (stylusHandwriting && android.os.Build.VERSION.SDK_INT >= 34) {
            // EditorInfoApi34.setHandwritingGestures
            out.setSupportedHandwritingGestures(java.util.Arrays.asList(
                    android.view.inputmethod.SelectGesture.class, android.view.inputmethod.DeleteGesture.class,
                    android.view.inputmethod.SelectRangeGesture.class, android.view.inputmethod.DeleteRangeGesture.class,
                    android.view.inputmethod.JoinOrSplitGesture.class, android.view.inputmethod.InsertGesture.class,
                    android.view.inputmethod.RemoveSpaceGesture.class));
            out.setSupportedHandwritingGesturePreviews(new java.util.HashSet<>(java.util.Arrays.asList(
                    android.view.inputmethod.SelectGesture.class, android.view.inputmethod.DeleteGesture.class,
                    android.view.inputmethod.SelectRangeGesture.class, android.view.inputmethod.DeleteRangeGesture.class)));
        }
        if (connection != null) connection.closeConnection();
        extractedTextMonitor = false;
        extractedTextToken = 0;
        connection = new KilnInputConnection(this);
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
        AccessibilityManager am = getContext().getSystemService(AccessibilityManager.class);
        if (am != null) {
            am.addAccessibilityStateChangeListener(a11yStateListener);
            am.addTouchExplorationStateChangeListener(touchExplorationListener);
        }
        accessibility.updateEnabled();
        setViewTranslationCallback(accessibility.translationCallback);
    }

    @Override
    protected void onDetachedFromWindow() {
        back.onDetached();
        flushHoverExit(true);
        clearViewTranslationCallback();
        AccessibilityManager am = getContext().getSystemService(AccessibilityManager.class);
        if (am != null) {
            am.removeAccessibilityStateChangeListener(a11yStateListener);
            am.removeTouchExplorationStateChangeListener(touchExplorationListener);
        }
        super.onDetachedFromWindow();
    }

    /** 1: from the IME (FLAG_SOFT_KEYBOARD); 2: a physical alphabetic keyboard
     *  (isFromHardwareSource); 4: a keyboard source. */
    private static int keySource(KeyEvent e) {
        int s = 0;
        final boolean soft = (e.getFlags() & KeyEvent.FLAG_SOFT_KEYBOARD) != 0;
        if (soft) s |= 1;
        android.view.InputDevice d = e.getDevice();
        if (d != null && !d.isVirtual() && d.getKeyboardType() == android.view.InputDevice.KEYBOARD_TYPE_ALPHABETIC && !soft) s |= 2;
        if (e.isFromSource(InputDevice.SOURCE_KEYBOARD)) s |= 4;
        return s;
    }

    // Keys before the IME (dispatchKeyEventPreIme): onInterceptKeyBeforeSoftKeyboard, when focused.
    @Override
    public boolean dispatchKeyEventPreIme(KeyEvent e) {
        if (isFocused() && KilnNative.keyEventPreIme(e.getAction(), e.getKeyCode(), e.getMetaState(), e.getUnicodeChar(),
                e.getRepeatCount(), e.getEventTime(), keySource(e))) return true;
        return super.dispatchKeyEventPreIme(e);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (back.onKey(e)) return true;
        if (KilnNative.keyEvent(e.getAction(), e.getKeyCode(), e.getMetaState(), e.getUnicodeChar(),
                e.getRepeatCount(), e.getEventTime(), keySource(e))) return true;
        return super.dispatchKeyEvent(e);
    }

    // --- View focus integration (AndroidComposeView) -------------------------------------------
    // The focused item is this view's focused rect, so focus search from outside and
    // ViewRootImpl's scroll-into-view see it.
    @Override
    public void getFocusedRect(Rect r) {
        float[] f = KilnNative.focusedRect();
        if (f != null) {
            r.set(Math.round(f[0]), Math.round(f[1]), Math.round(f[2]), Math.round(f[3]));
        } else if (!KilnNative.hasFocusableContent()) {
            r.set(Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE);
        } else {
            super.getFocusedRect(r);
        }
    }

    // Nothing focusable inside: the view is not a stop for focus search.
    @Override
    public void addFocusables(java.util.ArrayList<android.view.View> views, int direction, int focusableMode) {
        if (KilnNative.hasFocusableContent()) super.addFocusables(views, direction, focusableMode);
    }

    // Moving focus from a View (or from this one) compares the next View with the next kiln item.
    @Override
    public android.view.View focusSearch(android.view.View focused, int direction) {
        if (focused == null || isInLayout()) return super.focusSearch(focused, direction);
        android.view.View root = getRootView();
        android.view.View nextView = root instanceof android.view.ViewGroup
                ? android.view.FocusFinder.getInstance().findNextFocus((android.view.ViewGroup) root, focused, direction) : null;
        if (nextView != null && !isDescendant(nextView)) nextView = null;
        float[] focusedBounds = null;
        if (focused == this) focusedBounds = KilnNative.focusedRect();
        if (focusedBounds == null) focusedBounds = rectRelativeToThis(focused);
        float[] found = KilnNative.focusSearch(direction, focusedBounds[0], focusedBounds[1], focusedBounds[2], focusedBounds[3]);
        if (found[0] < 0) return focused;                                       // cancelled
        if (found[0] == 0) return nextView != null ? nextView : super.focusSearch(focused, direction);
        if (nextView == null) return this;
        if (direction == FOCUS_FORWARD || direction == FOCUS_BACKWARD) return this;
        float[] target = { found[1], found[2], found[3], found[4] };
        return KilnNative.isBetterFocusCandidate(direction, target, rectRelativeToThis(nextView), focusedBounds) ? this : nextView;
    }

    private boolean isDescendant(android.view.View v) {
        for (android.view.ViewParent p = v.getParent(); p != null; p = p.getParent()) if (p == this) return true;
        return false;
    }

    private float[] rectRelativeToThis(android.view.View v) {
        int[] a = new int[2], b = new int[2];
        v.getLocationInWindow(a);
        getLocationInWindow(b);
        final float l = a[0] - b[0], t = a[1] - b[1];
        return new float[] { l, t, l + v.getWidth(), t + v.getHeight() };
    }

    // Focus arriving from the View system goes to a kiln item.
    @Override
    public boolean requestFocus(int direction, Rect previouslyFocusedRect) {
        if (isFocused()) return true;
        final Rect p = previouslyFocusedRect;
        return KilnNative.hostRequestFocus(direction, p != null, p != null ? p.left : 0, p != null ? p.top : 0,
                p != null ? p.right : 0, p != null ? p.bottom : 0, hasFocus());
    }

    // requestOwnerFocus: kiln focused an item and needs platform focus for keys and the IME.
    boolean requestOwnerFocus() {
        if (isFocused() || hasFocus()) return true;
        return super.requestFocus(FOCUS_DOWN, null);
    }

    @Override
    protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
        KilnNative.hostFocusChanged(gainFocus);
    }   // the SurfaceView child never sees touches

    @Override
    public void onWindowFocusChanged(boolean hasWindowFocus) {
        super.onWindowFocusChanged(hasWindowFocus);
        KilnNative.windowFocusChanged(hasWindowFocus);
    }

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

    void onHostStart() { KilnNative.setVisible(true); accessibility.onHostStart(); }
    void onHostStop() { accessibility.onHostStop(); KilnNative.setVisible(false); }

    // Translation of the virtual text views (API 31+).
    @Override
    public void onCreateVirtualViewTranslationRequests(long[] virtualIds, int[] supportedFormats,
            java.util.function.Consumer<android.view.translation.ViewTranslationRequest> requestsCollector) {
        accessibility.onCreateTranslationRequests(virtualIds, requestsCollector);
    }

    @Override
    public void onVirtualViewTranslationResponses(android.util.LongSparseArray<android.view.translation.ViewTranslationResponse> response) {
        accessibility.onTranslationResponses(response);
    }

    void onHostConfigurationChanged(Configuration config) {
        KilnNative.setDensity(getResources().getDisplayMetrics().density, config.fontScale);
        KilnNative.setFontWeightAdjustment(fontWeightAdjustment(config));
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
