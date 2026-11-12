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

    /** The window this view shows: 0 for the activity's, a dialog's id otherwise. */
    final int windowId;
    /** A dialog view's window (FLAG_SECURE goes there); null for the activity's view. */
    android.view.Window hostWindow;
    /** The dialog this view is the content of; null for the activity's view. */
    KilnDialog dialog;

    public KilnView(Context context) { this(context, null); }

    /** savedState: the bytes from {@link #saveState()} of the previous instance, or null. */
    public KilnView(Context context, byte[] savedState) { this(context, 0, null, savedState); }

    /** A dialog window's view: its own owner on the native side, the app's services shared. */
    KilnView(Context context, int windowId, KilnView parent) { this(context, windowId, parent, null); }

    private KilnView(Context context, int windowId, KilnView parent, byte[] savedState) {
        super(context);
        this.windowId = windowId;
        textServices = parent != null ? parent.textServices : new KilnTextServices(context, new Handler(Looper.getMainLooper()));
        setOnDragListener(dragListener);
        setFocusable(true);
        setFocusableInTouchMode(true);
        setDefaultFocusHighlightEnabled(false);
        setImportantForAutofill(IMPORTANT_FOR_AUTOFILL_YES);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        setForceDarkAllowed(false);
        setWillNotDraw(false);

        surfaceView = new SurfaceView(context);
        surfaceView.getHolder().addCallback(this);
        // A dialog window shows what is behind it where the content draws nothing.
        if (windowId != 0) surfaceView.getHolder().setFormat(android.graphics.PixelFormat.TRANSLUCENT);
        addView(surfaceView, new LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));

        text = parent != null ? parent.text : new KilnText(context);
        if (windowId != 0) {
            // The app-wide state (settings, configuration) comes from the activity's view.
            KilnNative.windowFocusChanged(windowId, hasWindowFocus());
            installInsets();
            return;
        }
        pushConfiguration(getResources().getConfiguration());
        push24HourFormat();
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        KilnNative.create(this, text, context.getAssets(), metrics.density, getResources().getConfiguration().fontScale, savedState);
        KilnNative.setCacheDir(context.getCacheDir().getPath());
        KilnNative.setFontWeightAdjustment(fontWeightAdjustment(getResources().getConfiguration()));
        KilnNative.windowFocusChanged(windowId, hasWindowFocus());

        // ANIMATOR_DURATION_SCALE, observed for changes (WindowRecomposer:95-135).
        animationScaleObserver = new ContentObserver(new Handler(Looper.getMainLooper())) {
            @Override public void onChange(boolean selfChange) { pushAnimationScale(); }
        };
        context.getContentResolver().registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE), false,
                animationScaleObserver);
        pushAnimationScale();
        // The show-passwords setting for BasicSecureTextField (PasswordVisibilitySetting).
        Handler main = new Handler(Looper.getMainLooper());
        if (android.os.Build.VERSION.SDK_INT >= 37) {
            unregisterPasswordObserver = android.text.ShowSecretsSetting.registerCallback(context,
                    () -> main.post(this::pushPasswordVisibility));
        } else {
            ContentObserver o = new ContentObserver(main) {
                @Override public void onChange(boolean selfChange) { pushPasswordVisibility(); }
            };
            context.getContentResolver().registerContentObserver(
                    Settings.System.getUriFor(Settings.System.TEXT_SHOW_PASSWORD), false, o);
            unregisterPasswordObserver = () -> context.getContentResolver().unregisterContentObserver(o);
        }
        pushPasswordVisibility();
        registerAccessibilityServiceListeners();
        pushViewConfiguration();
        installInsets();
    }

    private Runnable unregisterPasswordObserver;
    private final KilnInputDevices inputDevices = new KilnInputDevices(this);
    void setInputDeviceListenerEnabled(boolean enabled) { inputDevices.setEnabled(enabled); }

    @SuppressWarnings("deprecation")
    private void pushPasswordVisibility() {
        boolean touch, physical;
        if (android.os.Build.VERSION.SDK_INT >= 37) {
            touch = android.text.ShowSecretsSetting.shouldShowTouchInput(getContext());
            physical = android.text.ShowSecretsSetting.shouldShowPhysicalInput(getContext());
        } else {
            boolean show;
            try {
                show = Settings.System.getInt(getContext().getContentResolver(), Settings.System.TEXT_SHOW_PASSWORD) > 0;
            } catch (Exception e) {
                android.util.Log.w("BasicSecureTextField", "Failed to fetch show password setting, using value: true", e);
                show = true;
            }
            touch = physical = show;
        }
        KilnNative.setPasswordVisibility((touch ? 1 : 0) | (physical ? 2 : 0));
    }

    // --- accessibility services (rememberAccessibilityServiceState's listeners) -----------------
    private AccessibilityManager accessibilityManager;
    private Object a11yServicesListener;   // AccessibilityServicesStateChangeListener (API 33)

    private void registerAccessibilityServiceListeners() {
        accessibilityManager = getContext().getSystemService(AccessibilityManager.class);
        if (accessibilityManager == null) return;
        if (android.os.Build.VERSION.SDK_INT >= 33) {
            AccessibilityManager.AccessibilityServicesStateChangeListener l = am -> pushAccessibilityServices();
            accessibilityManager.addAccessibilityServicesStateChangeListener(l);
            a11yServicesListener = l;
        }
        pushAccessibilityServices();
    }

    private void unregisterAccessibilityServiceListeners() {
        if (accessibilityManager == null) return;
        if (android.os.Build.VERSION.SDK_INT >= 33 && a11yServicesListener != null)
            accessibilityManager.removeAccessibilityServicesStateChangeListener(
                    (AccessibilityManager.AccessibilityServicesStateChangeListener) a11yServicesListener);
    }

    // switchAccessEnabled / voiceAccessEnabled: an enabled generic-feedback service whose
    // settings activity names it.
    private boolean serviceEnabled(String activityName) {
        for (android.accessibilityservice.AccessibilityServiceInfo info :
                accessibilityManager.getEnabledAccessibilityServiceList(android.accessibilityservice.AccessibilityServiceInfo.FEEDBACK_GENERIC)) {
            String s = info.getSettingsActivityName();
            if (s != null && s.toLowerCase(java.util.Locale.ROOT).contains(activityName.toLowerCase(java.util.Locale.ROOT))) return true;
        }
        return false;
    }

    private void pushAccessibilityServices() {
        AccessibilityManager am = accessibilityManager;
        boolean sdk33 = android.os.Build.VERSION.SDK_INT >= 33;
        KilnNative.setAccessibilityServices(am.isEnabled(), am.isTouchExplorationEnabled(),
                sdk33 && serviceEnabled("SwitchAccess"), sdk33 && serviceEnabled("VoiceAccess"));
    }

    /** AccessibilityManager.getRecommendedTimeoutMillis. */
    int recommendedTimeoutMillis(int originalTimeout, int uiContentFlags) {
        return accessibilityManager != null ? accessibilityManager.getRecommendedTimeoutMillis(originalTimeout, uiContentFlags) : originalTimeout;
    }

    /** The activity is going: settings observers and the text services stop. */
    void release() {
        inputDevices.setEnabled(false);
        if (windowId != 0) return;
        unregisterAccessibilityServiceListeners();
        if (animationScaleObserver != null) getContext().getContentResolver().unregisterContentObserver(animationScaleObserver);
        if (unregisterPasswordObserver != null) unregisterPasswordObserver.run();
        unregisterPasswordObserver = null;
        textServices.shutdown();
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
        KilnNative.setWindowInsets(windowId, data);
    }

    private void sendAnimation(int typeMask, WindowInsetsAnimation a, android.graphics.Insets[] positions) {
        for (int i = 0; i < TYPES.length - 1; i++) {
            if ((typeMask & TYPES[i]) == 0) continue;
            int[] bounds = null;
            if (positions != null) {
                android.graphics.Insets s = positions[0], t = positions[1];
                bounds = new int[] { s.left, s.top, s.right, s.bottom, t.left, t.top, t.right, t.bottom };
            }
            if (a == null) KilnNative.setInsetsAnimation(windowId, i, false, null, 0, 1, 0);
            else KilnNative.setInsetsAnimation(windowId, i, true, bounds, a.getInterpolatedFraction(), a.getAlpha(), a.getDurationMillis());
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

    /** enableEdgeToEdge: modes 0 auto, 1 dark, 2 light (SystemBarStyle.auto / dark / light). */
    void enableEdgeToEdge(int statusMode, int statusLight, int statusDark, int navMode, int navLight, int navDark) {
        if (!(getContext() instanceof KilnActivity)) return;
        ((KilnActivity) getContext()).enableEdgeToEdge(style(statusMode, statusLight, statusDark), style(navMode, navLight, navDark));
    }

    private static KilnEdgeToEdge.Style style(int mode, int light, int dark) {
        if (mode == 1) return KilnEdgeToEdge.Style.dark(light);
        if (mode == 2) return KilnEdgeToEdge.Style.light(light, dark);
        return KilnEdgeToEdge.Style.auto(light, dark);
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
                    KilnNative.insetsControlReady(windowId, new int[] { h.left, h.top, h.right, h.bottom, s.left, s.top, s.right, s.bottom,
                                                              cur.left, cur.top, cur.right, cur.bottom });
                }
                @Override public void onFinished(android.view.WindowInsetsAnimationController controller) { ended(); }
                @Override public void onCancelled(android.view.WindowInsetsAnimationController controller) { ended(); }
                private void ended() {
                    insetsController = null;
                    KilnNative.insetsControlEnded(windowId);
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
        KilnNative.touchEvent(windowId, e.getActionMasked(), e.getActionIndex(), e.getEventTime(), idsN, xyN, toolsN,
                e.getButtonState(), e.getMetaState(), e.getClassification(),
                e.getAxisValue(MotionEvent.AXIS_HSCROLL), e.getAxisValue(MotionEvent.AXIS_VSCROLL),
                historyTimes, historyXy, pan, e.getSource());
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        // A dialog's view is its content's DialogLayout: a press outside the content is not
        // this view's, so the dialog sees it (and dismisses on its release).
        if (windowId != 0 && e.getActionMasked() == MotionEvent.ACTION_DOWN && !insideContent(e.getX(), e.getY())) return false;
        return forward(e);
    }

    /** DialogLayout.isInsideContent. */
    boolean insideContent(float x, float y) {
        if (!Float.isFinite(x) || !Float.isFinite(y)) return false;
        int[] b = KilnNative.windowContentBounds(windowId);
        if (b == null) return true;
        int rx = Math.round(x), ry = Math.round(y);
        return rx >= b[0] && rx <= b[2] && ry >= b[1] && ry <= b[3];
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        if (windowId == 0 || dialog == null) { super.onMeasure(widthMeasureSpec, heightMeasureSpec); return; }
        // DialogLayout.internalOnMeasure: the content measured within the window's room; a
        // full-width wrap-content dialog may use the height the system bars leave.
        final int height = MeasureSpec.getSize(heightMeasureSpec);
        final int heightMode = MeasureSpec.getMode(heightMeasureSpec);
        final android.view.Window window = dialog.getWindow();
        final boolean wrapHeight = window.getAttributes().height == android.view.ViewGroup.LayoutParams.WRAP_CONTENT;
        int targetHeight = height;
        if (heightMode == MeasureSpec.AT_MOST && !dialog.usesPlatformDefaultWidth() && wrapHeight) {
            if (dialog.decorFitsSystemWindows) {
                if (android.os.Build.VERSION.SDK_INT < 32) {
                    android.view.WindowMetrics m = window.getWindowManager().getCurrentWindowMetrics();
                    android.graphics.Insets bars = m.getWindowInsets().getInsets(WindowInsets.Type.systemBars());
                    targetHeight = m.getBounds().height() - bars.top - bars.bottom;
                }
            } else {
                targetHeight = height + 1;
            }
        }
        int[] size = KilnNative.windowMeasure(windowId, widthMeasureSpec, MeasureSpec.makeMeasureSpec(targetHeight, heightMode));
        int w = size != null ? size[0] : 0, h = size != null ? size[1] : 0;
        if (heightMode == MeasureSpec.AT_MOST) h = Math.min(h, height);
        setMeasuredDimension(w, h);
        measureChildren(MeasureSpec.makeMeasureSpec(w, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(h, MeasureSpec.EXACTLY));
        // Taller than the window without fitting the system bars: drawn behind them.
        if (!dialog.decorFitsSystemWindows && size != null && size[2] > height && wrapHeight) {
            window.addFlags(android.view.WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
            if (!dialog.usesPlatformDefaultWidth())
                window.setLayout(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT);
        }
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        final int a = e.getActionMasked();
        // Rotary encoders (AndroidComposeView.handleRotaryEvent): AXIS_SCROLL, inverted, in pixels.
        if (a == MotionEvent.ACTION_SCROLL && e.isFromSource(android.view.InputDevice.SOURCE_ROTARY_ENCODER)) {
            android.view.ViewConfiguration vc = android.view.ViewConfiguration.get(getContext());
            final float v = -e.getAxisValue(MotionEvent.AXIS_SCROLL);
            if (KilnNative.rotary(windowId, v * vc.getScaledVerticalScrollFactor(), v * vc.getScaledHorizontalScrollFactor(),
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
        final boolean consumed = KilnNative.indirectPointer(windowId, a, e.getActionIndex(), e.getEventTime(), pids, pxy,
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
                    if (navAxis == 1 && Math.abs(vx) > Math.abs(vy)) KilnNative.indirectFling(windowId, vx > 0);
                    else if (navAxis == 2 && Math.abs(vy) > Math.abs(vx)) KilnNative.indirectFling(windowId, vy > 0);
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

    // --- external surfaces (AndroidExternalSurface; called from native) -------------------------
    // Each is a child SurfaceControl of the SurfaceView kiln draws into, layered below its buffer
    // (Behind -2, MediaOverlay -1: kiln clears its frame over them, so this view's surface is
    // translucent while any exist) or above it (OnTop +1), and scaled into its element's bounds.

    private static final class ExternalSurface {
        int left, top, right, bottom, fixedWidth, fixedHeight, zOrder;
        boolean opaque, secure, visible;
        android.view.SurfaceControl control;
        android.view.Surface surface;
        int width = -1, height = -1;
    }
    private final android.util.SparseArray<ExternalSurface> externals = new android.util.SparseArray<>();
    private boolean kilnSurfaceValid;
    private boolean translucentForExternals;

    void externalSurface(int id, int left, int top, int right, int bottom, int fixedWidth, int fixedHeight, boolean opaque, int zOrder,
                         boolean secure, boolean embedded, boolean visible) {
        if (embedded) return;   // embedded surfaces are drawn by kiln itself
        ExternalSurface e = externals.get(id);
        if (e == null) {
            e = new ExternalSurface();
            externals.put(id, e);
        } else if (e.control != null && e.opaque != opaque) {
            destroyExternal(id, e);   // the format is fixed per surface
        }
        e.left = left; e.top = top; e.right = right; e.bottom = bottom;
        e.fixedWidth = fixedWidth; e.fixedHeight = fixedHeight;
        e.opaque = opaque; e.zOrder = zOrder; e.secure = secure; e.visible = visible;
        updateSurfaceMode();
        updateExternalSecure();
        syncExternal(id, e);
    }

    void releaseExternalSurface(int id) {
        ExternalSurface e = externals.get(id);
        if (e == null) return;
        destroyExternal(id, e);
        externals.remove(id);
        updateSurfaceMode();
        updateExternalSecure();
    }

    /** This view's surface lets surfaces below it show through while there are any. */
    private void updateSurfaceMode() {
        if (windowId != 0) return;   // a dialog's surface is translucent already
        boolean below = false;
        for (int i = 0; i < externals.size(); ++i) if (externals.valueAt(i).zOrder != 2) below = true;
        if (below == translucentForExternals) return;
        translucentForExternals = below;
        // RGB_565 is SurfaceView's own default (opaque); the change recreates the surface.
        surfaceView.getHolder().setFormat(below ? android.graphics.PixelFormat.TRANSLUCENT : android.graphics.PixelFormat.RGB_565);
    }

    private static int layerOf(int zOrder) { return zOrder == 2 ? 1 : zOrder == 1 ? -1 : -2; }

    private void syncExternal(int id, ExternalSurface e) {
        if (!kilnSurfaceValid) return;
        android.view.SurfaceControl parent = surfaceView.getSurfaceControl();
        if (parent == null || !parent.isValid()) return;
        final int w = e.right - e.left, h = e.bottom - e.top;
        final int bw = e.fixedWidth > 0 ? e.fixedWidth : w, bh = e.fixedHeight > 0 ? e.fixedHeight : h;
        if (bw <= 0 || bh <= 0) return;   // nothing to show yet
        android.view.SurfaceControl.Transaction t = new android.view.SurfaceControl.Transaction();
        final boolean created = e.control == null;
        if (created) {
            e.control = new android.view.SurfaceControl.Builder()
                    .setName("kiln-external-" + id)
                    .setParent(parent)
                    .setBufferSize(bw, bh)
                    .setFormat(e.opaque ? android.graphics.PixelFormat.OPAQUE : android.graphics.PixelFormat.TRANSLUCENT)
                    .setOpaque(e.opaque)
                    .build();
            e.surface = new android.view.Surface(e.control);
        } else if (bw != e.width || bh != e.height) {
            t.setBufferSize(e.control, bw, bh);
        }
        t.setLayer(e.control, layerOf(e.zOrder));
        t.setGeometry(e.control, null, new Rect(e.left, e.top, e.right, e.bottom), android.view.Surface.ROTATION_0);
        t.setVisibility(e.control, e.visible && w > 0 && h > 0);
        t.apply();
        final boolean resized = !created && (bw != e.width || bh != e.height);
        e.width = bw;
        e.height = bh;
        if (created) KilnNative.externalSurfaceCreated(windowId, id, e.surface, bw, bh);
        else if (resized) KilnNative.externalSurfaceChanged(windowId, id, bw, bh);
    }

    private void destroyExternal(int id, ExternalSurface e) {
        if (e.control == null) return;
        KilnNative.externalSurfaceDestroyed(windowId, id);
        e.surface.release();
        new android.view.SurfaceControl.Transaction().reparent(e.control, null).apply();
        e.control.release();
        e.control = null;
        e.surface = null;
        e.width = e.height = -1;
    }

    /**
     * A secure surface is left out of screenshots and recordings (SurfaceView.setSecure). The
     * per-layer flag is not public API (SurfaceControl.Transaction.setSecure is a blocked hidden
     * method), and SurfaceView's own flag covers only its buffer layer, not children; so while
     * any secure external surface exists the window is FLAG_SECURE, which covers it (and the
     * rest of the window: more is protected, never less).
     */
    private void updateExternalSecure() {
        boolean secure = false;
        for (int i = 0; i < externals.size(); ++i) if (externals.valueAt(i).secure) secure = true;
        if (secure == externalFlagSecure) return;
        externalFlagSecure = secure;
        applyFlagSecure();
    }

    // --- text input (called from native; spec 02 req. 12) -----------------------------------
    private boolean inputActive;
    private int inputType, imeOptions;
    private String hintLocales = "";
    private boolean acceptsContent;
    private boolean imeWasVisible;
    private boolean stylusHandwriting;
    private String privateImeOptions;

    /** ComposeInputMethodManagerImplApi34.prepareStylusHandwritingDelegation (handwritingDetector). */
    void prepareStylusHandwritingDelegation() {
        if (android.os.Build.VERSION.SDK_INT >= 34) imm().prepareStylusHandwritingDelegation(this);
    }

    /** ComposeInputMethodManagerImplApi34.acceptStylusHandwritingDelegation (handwritingHandler). */
    void acceptStylusHandwritingDelegation() {
        if (android.os.Build.VERSION.SDK_INT >= 34) imm().acceptStylusHandwritingDelegation(this);
    }

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
    // Whether onCreateInputConnection made a connection since the session started.
    private boolean inputConnectionCreatedForSession;
    // getExtractedText(GET_EXTRACTED_TEXT_MONITOR): the IME's token for updateExtractedText.
    private boolean extractedTextMonitor;
    private int extractedTextToken;

    private InputMethodManager imm() { return getContext().getSystemService(InputMethodManager.class); }

    /** A field took focus: new EditorInfo, then show the keyboard (coalesced restart + show). */
    void startInput(int type, int options, boolean singleLine, String hintLocales, boolean acceptsContent, boolean stylusHandwriting, String privateImeOptions) {
        this.inputActive = true;
        this.privateImeOptions = privateImeOptions;
        this.stylusHandwriting = stylusHandwriting;
        this.acceptsContent = acceptsContent;
        this.inputType = type;
        this.imeOptions = options;
        this.hintLocales = hintLocales;
        inputConnectionCreatedForSession = false;
        requestOwnerFocus();
        // TextInputServiceAndroid (isSkipRedundantRestartInputOnInitialFocusEnabled): on initial
        // focus isActive() starts input on the view, creating its connection; a restart then
        // would create a second one at once, and keys the IME sends the first are dropped.
        imm().isActive(this);
        if (!inputConnectionCreatedForSession) imm().restartInput(this);
        showSoftInputWhenFocused();
    }

    // The IME ignores a show for a window without focus (a field focused as its window opens):
    // show it once the window has focus.
    private boolean pendingShowSoftInput;

    private void showSoftInputWhenFocused() {
        if (hasWindowFocus()) showSoftInput();
        else pendingShowSoftInput = true;
    }

    // SoftwareKeyboardControllerCompat.show (API 30+): the window's insets controller shows the
    // IME, then a posted showSoftInput (it also reports the tool type); a direct showSoftInput
    // right after restartInput is refused while the IME still serves the previous input.
    // Posted as a whole: a show in the same pass as the window gaining focus loses to the focus change's own
    // hide (softInputMode stateUnspecified), which the system applies after it.
    private void showSoftInput() {
        post(() -> {
            if (!inputActive) return;
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                if (android.os.Build.VERSION.SDK_INT < 33) imm().isActive();   // checkFocus() backport
                android.view.WindowInsetsController c = getWindowInsetsController();
                if (c != null) c.show(WindowInsets.Type.ime());
            }
            if (hasWindowFocus()) post(() -> imm().showSoftInput(this, 0));
        });
    }

    // SoftwareKeyboardControllerCompat.hide (API 30+): through the insets controller, and the
    // InputMethodManager too when the IME insets are not controllable (multi-window).
    private void hideSoftInput() {
        android.view.WindowInsetsController c = android.os.Build.VERSION.SDK_INT >= 30 ? getWindowInsetsController() : null;
        if (c == null) { imm().hideSoftInputFromWindow(getWindowToken(), 0); return; }
        final boolean[] controllable = { false };
        android.view.WindowInsetsController.OnControllableInsetsChangedListener l =
            (controller, typeMask) -> controllable[0] = (typeMask & WindowInsets.Type.ime()) != 0;
        c.addOnControllableInsetsChangedListener(l);
        if (!controllable[0]) imm().hideSoftInputFromWindow(getWindowToken(), 0);
        c.removeOnControllableInsetsChangedListener(l);
        c.hide(WindowInsets.Type.ime());
    }

    void stopInput() {
        pendingShowSoftInput = false;
        if (!inputActive) return;
        inputActive = false;
        hideSoftInput();
        imm().restartInput(this);
    }

    void restartInputFromNative() { if (inputActive) imm().restartInput(this); }

    void showKeyboard() { if (inputActive) showSoftInputWhenFocused(); }

    void hideKeyboard() { pendingShowSoftInput = false; hideSoftInput(); }

    /** The field changed without a restart (AndroidTextInputSession.collectImeNotifications). */
    void imeUpdate(int selStart, int selEnd, int compStart, int compEnd, boolean selectionChanged) {
        if (!inputActive) return;
        if (extractedTextMonitor) {
            imm().updateExtractedText(this, extractedTextToken,
                    KilnInputConnection.extractedText(KilnNative.imeGetText(windowId), new int[] { selStart, selEnd }));
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

    /** The display's highest refresh rate (preferredFrameRate's High category). */
    float maxRefreshRate() {
        android.view.Display d = null;
        try { d = getContext().getDisplay(); } catch (UnsupportedOperationException e) { /* not a visual context */ }
        float max = 60;
        if (d != null) for (android.view.Display.Mode m : d.getSupportedModes()) max = Math.max(max, m.getRefreshRate());
        return max;
    }

    /** UiMediaScope's device features: microphone 1, camera 2, TV 4, automotive 8, docked 16. */
    int mediaFeatures() {
        android.content.pm.PackageManager pm = getContext().getPackageManager();
        int f = 0;
        if (pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_MICROPHONE)) f |= 1;
        if (pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_CAMERA_ANY)) f |= 2;
        if (pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_LEANBACK)) f |= 4;
        if (pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_AUTOMOTIVE)) f |= 8;
        // ACTION_DOCK_EVENT is sticky: registering no receiver returns the current dock state.
        android.content.Intent dock = getContext().registerReceiver(null, new android.content.IntentFilter(android.content.Intent.ACTION_DOCK_EVENT));
        if (dock != null && dock.getIntExtra(android.content.Intent.EXTRA_DOCK_STATE, android.content.Intent.EXTRA_DOCK_STATE_UNDOCKED)
                != android.content.Intent.EXTRA_DOCK_STATE_UNDOCKED) f |= 16;
        return f;
    }

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

    /**
     * Clipboard.setClipEntry: (text, html, uri) per item; null clears the clipboard (API 28+).
     * The MIME types follow the items (text/plain, text/html, the content type of a URI).
     */
    void setClipboardClip(String[] items) {
        android.content.ClipboardManager cm = clipboard();
        if (cm == null) return;
        if (items == null || items.length < 3) {
            if (android.os.Build.VERSION.SDK_INT >= 28) cm.clearPrimaryClip();
            return;
        }
        java.util.LinkedHashSet<String> types = new java.util.LinkedHashSet<>();
        android.content.ClipData.Item[] list = new android.content.ClipData.Item[items.length / 3];
        for (int i = 0; i < list.length; i++) {
            String text = items[3 * i], html = items[3 * i + 1], uri = items[3 * i + 2];
            android.net.Uri u = uri != null ? android.net.Uri.parse(uri) : null;
            list[i] = new android.content.ClipData.Item(text, html, null, u);
            if (html != null) types.add(android.content.ClipDescription.MIMETYPE_TEXT_HTML);
            else if (text != null) types.add(android.content.ClipDescription.MIMETYPE_TEXT_PLAIN);
            if (u != null) {
                String t = getContext().getContentResolver().getType(u);
                types.add(t != null ? t : android.content.ClipDescription.MIMETYPE_TEXT_URILIST);
            }
        }
        android.content.ClipData clip = new android.content.ClipData(new android.content.ClipDescription("", types.toArray(new String[0])), list[0]);
        for (int i = 1; i < list.length; i++) clip.addItem(list[i]);
        cm.setPrimaryClip(clip);
    }

    /** ClipboardManager.setText(AnnotatedString): the span styles as Compose encodes them. */
    void setClipboardStyledText(String text, int[] ints, float[] floats, String[] strings) {
        android.content.ClipboardManager cm = clipboard();
        if (cm != null) cm.setPrimaryClip(android.content.ClipData.newPlainText("plain text", KilnClipSpans.encode(text, ints, floats, strings)));
    }

    /** ClipboardManager.getText: {text, ints, floats, strings} (KilnClipSpans), or null. */
    Object[] getClipboardStyledText() {
        android.content.ClipboardManager cm = clipboard();
        android.content.ClipData clip = cm != null ? cm.getPrimaryClip() : null;
        if (clip == null || clip.getItemCount() == 0) return null;
        return KilnClipSpans.decode(clip.getItemAt(0).getText());
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

    // --- magnifier (PlatformMagnifierFactoryApi29Impl) -------------------------------------
    // The magnifier copies from the SurfaceView kiln draws into (the window has a hole there).
    private final android.util.SparseArray<android.widget.Magnifier> magnifiers = new android.util.SparseArray<>();
    private int nextMagnifier = 1;

    int createMagnifier(boolean useTextDefault, float width, float height, float cornerRadius, float elevation, float initialZoom,
                        boolean clippingEnabled) {
        android.widget.Magnifier m;
        if (useTextDefault) {
            m = new android.widget.Magnifier(surfaceView);
        } else {
            android.widget.Magnifier.Builder b = new android.widget.Magnifier.Builder(surfaceView);
            if (!Float.isNaN(width) && !Float.isNaN(height)) b.setSize(Math.round(width), Math.round(height));
            if (!Float.isNaN(cornerRadius)) b.setCornerRadius(cornerRadius);
            if (!Float.isNaN(elevation)) b.setElevation(elevation);
            if (!Float.isNaN(initialZoom)) b.setInitialZoom(initialZoom);
            b.setClippingEnabled(clippingEnabled);
            m = b.build();
        }
        final int handle = nextMagnifier++;
        magnifiers.put(handle, m);
        return handle;
    }

    void showMagnifier(int handle, float sx, float sy, float mx, float my, float zoom) {
        android.widget.Magnifier m = magnifiers.get(handle);
        if (m == null) return;
        if (!Float.isNaN(zoom)) m.setZoom(zoom);
        // Root pixels to the SurfaceView's.
        final float ox = surfaceView.getLeft(), oy = surfaceView.getTop();
        if (!Float.isNaN(mx) && !Float.isNaN(my)) m.show(sx - ox, sy - oy, mx - ox, my - oy);
        else m.show(sx - ox, sy - oy);
    }

    void updateMagnifierContent(int handle) {
        android.widget.Magnifier m = magnifiers.get(handle);
        if (m != null) m.update();
    }

    void dismissMagnifier(int handle) {
        android.widget.Magnifier m = magnifiers.get(handle);
        if (m != null) m.dismiss();
    }

    void destroyMagnifier(int handle) {
        android.widget.Magnifier m = magnifiers.get(handle);
        if (m == null) return;
        m.dismiss();
        magnifiers.remove(handle);
    }

    int[] magnifierSize(int handle) {
        android.widget.Magnifier m = magnifiers.get(handle);
        return m == null ? new int[] { 0, 0 } : new int[] { m.getWidth(), m.getHeight() };
    }

    // --- drag and drop (AndroidComposeView.startDrag and its OnDragListener) --------------------
    private static final Object LOCAL_DRAG = new Object();   // our drags' local state token
    private android.view.DragEvent currentDragEvent;

    /** startDragAndDrop with native's clip (3 strings per item: text, html, uri) and shadow. */
    boolean startDragFromNative(String label, String[] mimeTypes, String[] items, int flags, int width, int height, byte[] rgba) {
        if (items.length < 3) return false;
        android.content.ClipData clip = null;
        for (int i = 0; i + 2 < items.length; i += 3) {
            android.content.ClipData.Item item = items[i + 2] != null
                    ? new android.content.ClipData.Item(android.net.Uri.parse(items[i + 2]))
                    : items[i + 1] != null ? new android.content.ClipData.Item(items[i], items[i + 1])
                    : new android.content.ClipData.Item(items[i]);
            if (clip == null) clip = new android.content.ClipData(label, mimeTypes, item);
            else clip.addItem(item);
        }
        final android.graphics.Bitmap shadow = rgba != null && rgba.length == width * height * 4
                ? android.graphics.Bitmap.createBitmap(width, height, android.graphics.Bitmap.Config.ARGB_8888) : null;
        if (shadow != null) shadow.copyPixelsFromBuffer(java.nio.ByteBuffer.wrap(rgba));
        final int w = width, h = height;
        android.view.View.DragShadowBuilder builder = new android.view.View.DragShadowBuilder() {
            @Override public void onProvideShadowMetrics(android.graphics.Point size, android.graphics.Point touch) {
                size.set(w, h);
                touch.set(w / 2, h / 2);
            }
            @Override public void onDrawShadow(android.graphics.Canvas canvas) {
                if (shadow != null) canvas.drawBitmap(shadow, 0, 0, null);
            }
        };
        return startDragAndDrop(clip, builder, LOCAL_DRAG, flags);
    }

    /** Activity.requestDragAndDropPermissions for the drop being delivered. */
    boolean requestDragPermissionsFromNative() {
        if (currentDragEvent == null || !(getContext() instanceof android.app.Activity)) return false;
        return ((android.app.Activity) getContext()).requestDragAndDropPermissions(currentDragEvent) != null;
    }

    private final android.view.View.OnDragListener dragListener = (v, event) -> {
        android.content.ClipDescription d = event.getClipDescription();
        String[] mimes = new String[d != null ? d.getMimeTypeCount() : 0];
        for (int i = 0; i < mimes.length; ++i) mimes[i] = d.getMimeType(i);
        String[] items = null;
        String label = d != null && d.getLabel() != null ? d.getLabel().toString() : "";
        android.content.ClipData clip = event.getAction() == android.view.DragEvent.ACTION_DROP ? event.getClipData() : null;
        if (clip != null) {
            items = new String[clip.getItemCount() * 3];
            for (int i = 0; i < clip.getItemCount(); ++i) {
                android.content.ClipData.Item it = clip.getItemAt(i);
                items[i * 3] = it.getText() != null ? it.getText().toString() : null;
                items[i * 3 + 1] = it.getHtmlText();
                items[i * 3 + 2] = it.getUri() != null ? it.getUri().toString() : null;
            }
        }
        currentDragEvent = event;
        try {
            return KilnNative.dragEvent(KilnView.this.windowId, event.getAction(), event.getX(), event.getY(), mimes, label, items,
                    event.getLocalState() == LOCAL_DRAG, event.getResult());
        } finally {
            currentDragEvent = null;
        }
    };

    // --- scroll capture (AndroidComposeView.onScrollCaptureSearch, ComposeScrollCaptureCallback) --
    private final android.util.SparseArray<Object[]> scrollCaptureRequests = new android.util.SparseArray<>();
    private int lastScrollCaptureRequest;

    @Override public void onScrollCaptureSearch(Rect localVisibleRect, android.graphics.Point windowOffset,
                                                java.util.function.Consumer<android.view.ScrollCaptureTarget> targets) {
        int[] c = KilnNative.scrollCaptureSearch();   // {id, left, top, right, bottom} in view pixels
        if (c == null) return;
        int[] location = new int[2];
        getLocationInWindow(location);
        final int id = c[0];
        final Rect inView = new Rect(c[1], c[2], c[3], c[4]);
        final Rect inWindow = new Rect(inView);
        inWindow.offset(location[0], location[1]);
        android.view.ScrollCaptureCallback callback = new android.view.ScrollCaptureCallback() {
            @Override public void onScrollCaptureSearch(android.os.CancellationSignal signal, java.util.function.Consumer<Rect> onReady) {
                onReady.accept(new Rect(inWindow));
            }
            @Override public void onScrollCaptureStart(android.view.ScrollCaptureSession session, android.os.CancellationSignal signal, Runnable onReady) {
                KilnNative.scrollCaptureStart(id, inView.left, inView.top, inView.right, inView.bottom);
                onReady.run();
            }
            @Override public void onScrollCaptureImageRequest(android.view.ScrollCaptureSession session, android.os.CancellationSignal signal,
                                                              Rect captureArea, java.util.function.Consumer<Rect> onComplete) {
                int request = ++lastScrollCaptureRequest;
                scrollCaptureRequests.put(request, new Object[] { session, onComplete });
                signal.setOnCancelListener(() -> {
                    scrollCaptureRequests.remove(request);
                    KilnNative.scrollCaptureCancel(request);
                });
                KilnNative.scrollCaptureImageRequest(request, captureArea.left, captureArea.top, captureArea.right, captureArea.bottom);
            }
            @Override public void onScrollCaptureEnd(Runnable onReady) {
                int request = ++lastScrollCaptureRequest;
                scrollCaptureRequests.put(request, new Object[] { onReady });
                KilnNative.scrollCaptureEnd(request);
            }
        };
        android.view.ScrollCaptureTarget target = new android.view.ScrollCaptureTarget(this, inView, new android.graphics.Point(inWindow.left, inWindow.top), callback);
        target.setScrollBounds(inWindow);
        targets.accept(target);
    }

    /** A strip native rendered: drawn into the session's surface, then the request completes. */
    @SuppressWarnings("unchecked")
    void scrollCaptureImageResult(int request, int left, int top, int right, int bottom, int width, int height, byte[] rgba) {
        Object[] r = scrollCaptureRequests.get(request);
        if (r == null) return;
        scrollCaptureRequests.remove(request);
        android.view.ScrollCaptureSession session = (android.view.ScrollCaptureSession) r[0];
        java.util.function.Consumer<Rect> onComplete = (java.util.function.Consumer<Rect>) r[1];
        if (rgba != null && width > 0 && height > 0 && rgba.length == width * height * 4) {
            android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(width, height, android.graphics.Bitmap.Config.ARGB_8888);
            bitmap.copyPixelsFromBuffer(java.nio.ByteBuffer.wrap(rgba));
            android.view.Surface surface = session.getSurface();
            android.graphics.Canvas canvas = surface.lockHardwareCanvas();
            try {
                canvas.drawColor(android.graphics.Color.TRANSPARENT, android.graphics.PorterDuff.Mode.CLEAR);
                canvas.drawBitmap(bitmap, 0, 0, null);
            } finally {
                surface.unlockCanvasAndPost(canvas);
            }
            bitmap.recycle();
        }
        onComplete.accept(new Rect(left, top, right, bottom));
    }

    void scrollCaptureEnded(int request) {
        Object[] r = scrollCaptureRequests.get(request);
        if (r == null) return;
        scrollCaptureRequests.remove(request);
        ((Runnable) r[0]).run();
    }

    /** RectListNode: kind 0 systemGestureExclusionRects, 1 preferKeepClearRects (4 ints each). */
    void setViewRectsFromNative(int kind, int[] v) {
        java.util.ArrayList<Rect> rects = new java.util.ArrayList<>();
        for (int i = 0; i + 3 < v.length; i += 4) rects.add(new Rect(v[i], v[i + 1], v[i + 2], v[i + 3]));
        if (kind == 0) setSystemGestureExclusionRects(rects);
        else if (android.os.Build.VERSION.SDK_INT >= 33) setPreferKeepClearRects(rects);
    }

    // --- activity results (the activity's registry; none outside a KilnActivity) ---------------
    private KilnActivityResults activityResults() {
        return getContext() instanceof KilnActivity ? ((KilnActivity) getContext()).activityResults : null;
    }
    void activityResultRegister(String key) { KilnActivityResults r = activityResults(); if (r != null) r.register(key); }
    void activityResultUnregister(String key) { KilnActivityResults r = activityResults(); if (r != null) r.unregister(key); }
    boolean activityResultLaunch(String key, Object[] intent) { KilnActivityResults r = activityResults(); return r != null && r.launch(key, intent); }
    boolean checkSelfPermissionFromNative(String permission) {
        return getContext().checkSelfPermission(permission) == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    // keepScreenOn / sensitiveContent counts reached zero or one.
    void setKeepScreenOnFromNative(boolean on) { setKeepScreenOn(on); }
    void setContentSensitiveFromNative(boolean sensitive) {
        if (android.os.Build.VERSION.SDK_INT >= 35)
            setContentSensitivity(sensitive ? CONTENT_SENSITIVITY_SENSITIVE : CONTENT_SENSITIVITY_AUTO);
    }

    /** PlatformHapticFeedback: View.performHapticFeedback(HapticFeedbackConstants.*). */
    void performHaptic(int constant) {
        // HapticFeedbackConstantsCompat.getFeedbackConstantOrFallback (minSdk 31: the API 34 set).
        if (constant < 0) return;   // NO_HAPTICS
        if (android.os.Build.VERSION.SDK_INT < 34) {
            switch (constant) {
                case 25: constant = 0; break;                       // DRAG_START -> LONG_PRESS
                case 21: case 26: case 23: constant = 6; break;     // TOGGLE_ON, SEGMENT_TICK, GESTURE_THRESHOLD_ACTIVATE -> CONTEXT_CLICK
                case 22: case 27: case 24: constant = 4; break;     // TOGGLE_OFF, SEGMENT_FREQUENT_TICK, GESTURE_THRESHOLD_DEACTIVATE -> CLOCK_TICK
                default: break;
            }
        }
        performHapticFeedback(constant);
    }

    // AndroidTextContextMenuToolbarProvider: a floating ActionMode over the content rect with
    // native's entries (kind 0: an item with a standard id 1..6 or a label; kind 1: a
    // classification action). A separator starts a new group; every item shows as an action.
    private android.view.ActionMode toolbarMode;
    private final Rect toolbarRect = new Rect();
    private int[] toolbarKinds = new int[0], toolbarIds = new int[0], toolbarGroups = new int[0], toolbarClassifications = new int[0],
            toolbarIndexes = new int[0];
    private String[] toolbarLabels = new String[0];
    private boolean toolbarDataChanged;
    KilnTextServices textServices;

    private void setToolbarEntries(int[] kinds, int[] ids, String[] labels, int[] groups, int[] classifications, int[] indexes) {
        toolbarKinds = kinds;
        toolbarIds = ids;
        toolbarLabels = labels;
        toolbarGroups = groups;
        toolbarClassifications = classifications;
        toolbarIndexes = indexes;
        toolbarDataChanged = true;
    }

    boolean showTextToolbar(int left, int top, int right, int bottom, int[] kinds, int[] ids, String[] labels, int[] groups,
                            int[] classifications, int[] indexes) {
        toolbarRect.set(left, top, right, bottom);
        setToolbarEntries(kinds, ids, labels, groups, classifications, indexes);
        if (toolbarMode != null) {
            android.view.ActionMode previous = toolbarMode;
            toolbarMode = null;
            previous.finish();
        }
        toolbarMode = startActionMode(toolbarCallback, android.view.ActionMode.TYPE_FLOATING);
        return toolbarMode != null;
    }

    void updateTextToolbar(int[] kinds, int[] ids, String[] labels, int[] groups, int[] classifications, int[] indexes) {
        setToolbarEntries(kinds, ids, labels, groups, classifications, indexes);
        if (toolbarMode != null) toolbarMode.invalidate();
    }

    void moveTextToolbar(int left, int top, int right, int bottom) {
        toolbarRect.set(left, top, right, bottom);
        if (toolbarMode != null) toolbarMode.invalidateContentRect();
    }

    void hideTextToolbar() {
        android.view.ActionMode mode = toolbarMode;
        toolbarMode = null;
        if (mode != null) mode.finish();
    }

    // --- autofill (AndroidComposeView's autofill overrides) --------------------------------------
    private final KilnAutofill autofill = new KilnAutofill(this);
    private boolean isDispatchingAutofillStructure;

    void autofillNotifyEntered(int id, int l, int t, int r, int b) { autofill.notifyEntered(id, l, t, r, b); }
    void autofillNotifyExited(int id) { autofill.notifyExited(id); }
    void autofillNotifyValue(int id, int kind, String text, int intValue, long date) { autofill.notifyValue(id, kind, text, intValue, date); }
    void autofillNotifyVisibility(int id, boolean visible) { autofill.notifyVisibility(id, visible); }
    void autofillCommit() { autofill.commit(); }
    void autofillCancel() { autofill.cancel(); }
    void autofillRequest(int id, int l, int t, int r, int b) { autofill.request(id, l, t, r, b); }

    @Override public void onProvideAutofillVirtualStructure(android.view.ViewStructure structure, int flags) {
        if (structure != null && !isDispatchingAutofillStructure) autofill.populate(structure);
    }

    @Override public void dispatchProvideAutofillStructure(android.view.ViewStructure structure, int flags) {
        // Hosted child views first; the framework stops traversal once virtual children exist.
        isDispatchingAutofillStructure = true;
        try {
            super.dispatchProvideAutofillStructure(structure, flags);
        } finally {
            isDispatchingAutofillStructure = false;
        }
        autofill.populate(structure);
    }

    @Override public void autofill(android.util.SparseArray<android.view.autofill.AutofillValue> values) {
        autofill.autofill(values);
    }

    // The text services native calls (PlatformSelectionBehaviors, ProcessText, LookUpText).
    int createClassifierSession(int type) { return textServices.createSession(type); }
    void destroyClassifierSession(int id) { textServices.destroySession(id); }
    void suggestSelection(int session, int request, String text, int start, int end, String[] locales) {
        textServices.suggestSelection(session, request, text, start, end, locales);
    }
    void classifyText(int session, int request, String text, int start, int end, String[] locales) {
        textServices.classifyText(session, request, text, start, end, locales);
    }
    void releaseClassification(int handle) { textServices.releaseClassification(handle); }
    String[] processTextActivities() { return textServices.processTextActivities(); }
    void processText(int index, boolean editable, String selected) { textServices.processText(index, editable, selected); }
    boolean canLookUp(String selected) { return textServices.canLookUp(selected); }
    void lookUp(String selected) { textServices.lookUp(selected); }
    String menuStandardLabel(int id) { return KilnTextServices.standardLabel(getContext(), id); }
    int[] menuStandardIcon(int id, int size) { return textServices.themeIcon(KilnTextServices.standardIconAttr(id), size); }
    int[] menuThemeIcon(int attr, int size) { return textServices.themeIcon(attr, size); }
    String menuClassificationLabel(int handle, int index) { return textServices.classificationLabel(handle, index); }
    int[] menuClassificationIcon(int handle, int index, int size) { return textServices.classificationIcon(handle, index, size); }
    void menuPerformClassification(int handle, int index) { textServices.performClassification(handle, index); }
    int[] menuColors() { return textServices.contextMenuColors(); }

    // updateMenuItems: false when the data did not change.
    private boolean fillToolbar(android.view.Menu menu) {
        if (!toolbarDataChanged) return false;
        toolbarDataChanged = false;
        menu.clear();
        int order = 1;
        for (int i = 0; i < toolbarKinds.length; ++i) {
            final int position = i;
            final int orderId = order++;
            if (toolbarKinds[i] == 1) {
                if (textServices != null) textServices.addClassificationItem(menu, orderId, toolbarClassifications[i], toolbarIndexes[i]);
                continue;
            }
            int itemId = KilnTextServices.standardItemId(toolbarIds[i]);
            if (itemId == 0) itemId = orderId;
            String title = toolbarLabels[i] != null && !toolbarLabels[i].isEmpty() ? toolbarLabels[i]
                         : KilnTextServices.standardLabel(getContext(), toolbarIds[i]);
            android.view.MenuItem item = menu.add(toolbarGroups[i], itemId, orderId, title);
            item.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS);
            item.setOnMenuItemClickListener(m -> { KilnNative.textToolbarItem(windowId, position); return true; });
        }
        return true;
    }

    private final android.view.ActionMode.Callback2 toolbarCallback = new android.view.ActionMode.Callback2() {
        @Override public boolean onCreateActionMode(android.view.ActionMode mode, android.view.Menu menu) {
            toolbarDataChanged = true;
            fillToolbar(menu);
            return menu.size() > 0;
        }
        @Override public boolean onPrepareActionMode(android.view.ActionMode mode, android.view.Menu menu) {
            return fillToolbar(menu);
        }
        @Override public boolean onActionItemClicked(android.view.ActionMode mode, android.view.MenuItem item) {
            return false;
        }
        @Override public void onDestroyActionMode(android.view.ActionMode mode) {
            if (mode != toolbarMode) return;   // finished by native
            toolbarMode = null;
            KilnNative.textToolbarDestroyed(windowId);
        }
        @Override public void onGetContentRect(android.view.ActionMode mode, android.view.View view, Rect outRect) {
            outRect.set(toolbarRect);
        }
    };

    // View.playSoundEffect; the audio service may be gone (DeadSystemException).
    void playSound(int constant) {
        try { playSoundEffect(constant); } catch (RuntimeException ignored) {}
    }

    // AudioManager.playSoundEffect (a negative volume: the default).
    void playAudioEffect(int effect, float volume) {
        try {
            android.media.AudioManager audio = (android.media.AudioManager) getContext().getSystemService(android.content.Context.AUDIO_SERVICE);
            if (audio == null) return;
            if (volume < 0) audio.playSoundEffect(effect);
            else audio.playSoundEffect(effect, volume);
        } catch (RuntimeException ignored) {}
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

    private final AccessibilityManager.AccessibilityStateChangeListener a11yStateListener = on -> { accessibility.updateEnabled(); if (KilnView.this.windowId == 0) pushAccessibilityServices(); };
    private final AccessibilityManager.TouchExplorationStateChangeListener touchExplorationListener = on -> { accessibility.updateEnabled(); if (KilnView.this.windowId == 0) pushAccessibilityServices(); };
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
        if (privateImeOptions != null) out.privateImeOptions = privateImeOptions;
        if (!hintLocales.isEmpty()) out.hintLocales = android.os.LocaleList.forLanguageTags(hintLocales);
        int[] sel = KilnNative.imeGetSelection(windowId);
        out.initialSelStart = sel[0];
        out.initialSelEnd = sel[1];
        out.setInitialSurroundingText(KilnNative.imeGetText(windowId));
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
        inputConnectionCreatedForSession = true;
        extractedTextMonitor = false;
        extractedTextToken = 0;
        connection = new KilnInputConnection(this);
        return connection;
    }

    // --- back (spec 15 req. 2) --------------------------------------------------------------
    final KilnBack back = new KilnBack(this);

    /** From native when the first handler becomes enabled or the last one disabled. */
    void setBackEnabled(boolean enabled) { back.setEnabled(enabled); }

    // ThreadedRenderer's light for this window (elevation shadows): over the display's horizontal
    // centre, raised with the display's size, with the theme's shadow alphas.
    private float lastLightX = Float.NaN, lastLightY = Float.NaN, lastLightZ = Float.NaN;
    private void pushShadowLight() {
        android.content.res.Resources sys = android.content.res.Resources.getSystem();
        float density = getResources().getDisplayMetrics().density;
        float lightY = systemDimen(sys, "light_y", 0f * density);
        float lightZ = systemDimen(sys, "light_z", 500f * density);
        float lightRadius = systemDimen(sys, "light_radius", 800f * density);
        android.content.res.TypedArray a = getContext().obtainStyledAttributes(
                new int[] { android.R.attr.ambientShadowAlpha, android.R.attr.spotShadowAlpha });
        float ambient = a.getFloat(0, 0), spot = a.getFloat(1, 0);
        a.recycle();
        android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
        if (getDisplay() == null) return;
        getDisplay().getRealMetrics(dm);
        int[] at = new int[2];
        getRootView().getLocationOnScreen(at);   // the window's left / top on the display
        float x = dm.widthPixels / 2f - at[0];
        float y = lightY - at[1];
        float zRatio = Math.min(dm.widthPixels, dm.heightPixels) / (450f * dm.density);
        float z = lightZ * ((zRatio + 2) / 3f);
        if (x == lastLightX && y == lastLightY && z == lastLightZ) return;
        lastLightX = x; lastLightY = y; lastLightZ = z;
        KilnNative.setShadowLight(windowId, x, y, z, lightRadius, ambient, spot);
    }
    private static float systemDimen(android.content.res.Resources sys, String name, float fallback) {
        int id = sys.getIdentifier(name, "dimen", "android");
        return id != 0 ? sys.getDimension(id) : fallback;
    }

    // The input mode follows the system's touch mode (focus rings and SystemDefined focusability).
    private final android.view.ViewTreeObserver.OnTouchModeChangeListener touchModeListener = this::onTouchModeChanged;

    private void onTouchModeChanged(boolean inTouchMode) {
        KilnNative.setInputMode(windowId, inTouchMode);
    }

    // The size before the surface's arrives (the window's first focus comes after layout).
    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        KilnNative.viewSizeChanged(windowId, w, h);
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        super.onLayout(changed, left, top, right, bottom);
        pushShadowLight();
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        pushShadowLight();
        back.onAttached();
        KilnNative.setInputMode(windowId, isInTouchMode());
        getViewTreeObserver().addOnTouchModeChangeListener(touchModeListener);
        inputDevices.onAttached();
        if (windowId == 0) KilnNative.setLayoutDirection(getLayoutDirection() == LAYOUT_DIRECTION_RTL);
        AccessibilityManager am = getContext().getSystemService(AccessibilityManager.class);
        if (am != null) {
            am.addAccessibilityStateChangeListener(a11yStateListener);
            am.addTouchExplorationStateChangeListener(touchExplorationListener);
        }
        accessibility.updateEnabled();
        setViewTranslationCallback(accessibility.translationCallback);
        if (windowId != 0) return;
        getContext().registerReceiver(timeFormatReceiver,
                new android.content.IntentFilter(android.content.Intent.ACTION_TIME_CHANGED), Context.RECEIVER_NOT_EXPORTED);
        getContext().getContentResolver().registerContentObserver(
                Settings.System.getUriFor(Settings.System.TIME_12_24), false, timeFormatObserver);
        push24HourFormat();
        pushFlagSecure();
    }

    @Override
    protected void onDetachedFromWindow() {
        getViewTreeObserver().removeOnTouchModeChangeListener(touchModeListener);
        back.onDetached();
        inputDevices.onDetached();
        flushHoverExit(true);
        clearViewTranslationCallback();
        if (windowId == 0) {
            getContext().unregisterReceiver(timeFormatReceiver);
            getContext().getContentResolver().unregisterContentObserver(timeFormatObserver);
        }
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
        if (isFocused() && KilnNative.keyEventPreIme(windowId, e.getAction(), e.getKeyCode(), e.getMetaState(), e.getUnicodeChar(),
                e.getRepeatCount(), e.getEventTime(), keySource(e))) return true;
        return super.dispatchKeyEventPreIme(e);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (back.onKey(e)) return true;
        if (KilnNative.keyEvent(windowId, e.getAction(), e.getKeyCode(), e.getMetaState(), e.getUnicodeChar(),
                e.getRepeatCount(), e.getEventTime(), keySource(e))) return true;
        return super.dispatchKeyEvent(e);
    }

    // --- View focus integration (AndroidComposeView) -------------------------------------------
    // The focused item is this view's focused rect, so focus search from outside and
    // ViewRootImpl's scroll-into-view see it.
    @Override
    public void getFocusedRect(Rect r) {
        float[] f = KilnNative.focusedRect(windowId);
        if (f != null) {
            r.set(Math.round(f[0]), Math.round(f[1]), Math.round(f[2]), Math.round(f[3]));
        } else if (!KilnNative.hasFocusableContent(windowId)) {
            r.set(Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE);
        } else {
            super.getFocusedRect(r);
        }
    }

    // Nothing focusable inside: the view is not a stop for focus search.
    @Override
    public void addFocusables(java.util.ArrayList<android.view.View> views, int direction, int focusableMode) {
        if (KilnNative.hasFocusableContent(windowId)) super.addFocusables(views, direction, focusableMode);
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
        if (focused == this) focusedBounds = KilnNative.focusedRect(windowId);
        if (focusedBounds == null) focusedBounds = rectRelativeToThis(focused);
        float[] found = KilnNative.focusSearch(windowId, direction, focusedBounds[0], focusedBounds[1], focusedBounds[2], focusedBounds[3]);
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
        return KilnNative.hostRequestFocus(windowId, direction, p != null, p != null ? p.left : 0, p != null ? p.top : 0,
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
        KilnNative.hostFocusChanged(windowId, gainFocus);
    }   // the SurfaceView child never sees touches

    @Override
    public void onWindowFocusChanged(boolean hasWindowFocus) {
        super.onWindowFocusChanged(hasWindowFocus);
        KilnNative.windowFocusChanged(windowId, hasWindowFocus);
        if (hasWindowFocus && pendingShowSoftInput) {
            pendingShowSoftInput = false;
            if (inputActive) showSoftInput();
        }
    }

    private ContentObserver animationScaleObserver;   // the activity's view only

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

    // LocalConfiguration (and Locale.current from its locales).
    private void pushConfiguration(Configuration c) {
        android.os.LocaleList list = c.getLocales();
        String[] locales = new String[list.size()];
        for (int i = 0; i < locales.length; i++) locales[i] = list.get(i).toLanguageTag();
        KilnNative.setConfiguration(new int[] {
                c.densityDpi, fontWeightAdjustment(c), c.uiMode,
                c.screenWidthDp, c.screenHeightDp, c.smallestScreenWidthDp,
                c.orientation, c.screenLayout, c.touchscreen, c.keyboard,
                c.keyboardHidden, c.hardKeyboardHidden, c.navigation, c.navigationHidden, c.colorMode,
        }, c.fontScale, locales, KilnSystemColors.palette(getContext()), KilnSystemColors.roles(getContext()));
    }

    // DateFormat.is24HourFormat; the Settings app broadcasts ACTION_TIME_CHANGED when it flips,
    // and the setting itself is observed for other writers.
    private final ContentObserver timeFormatObserver = new ContentObserver(new Handler(Looper.getMainLooper())) {
        @Override public void onChange(boolean selfChange) { push24HourFormat(); }
    };
    private void push24HourFormat() {
        KilnNative.set24HourFormat(android.text.format.DateFormat.is24HourFormat(getContext()));
    }
    private final android.content.BroadcastReceiver timeFormatReceiver = new android.content.BroadcastReceiver() {
        @Override public void onReceive(Context context, android.content.Intent intent) { push24HourFormat(); }
    };

    // FLAG_SECURE on the window: the native side asks, and reads the window's own flag on attach.
    // Secure external surfaces keep it set too (updateExternalSecure).
    private boolean appFlagSecure, externalFlagSecure;
    void setFlagSecureFromNative(boolean secure) {
        appFlagSecure = secure;
        applyFlagSecure();
    }
    private void applyFlagSecure() {
        android.view.Window w = hostWindow != null ? hostWindow
                : getContext() instanceof android.app.Activity ? ((android.app.Activity) getContext()).getWindow() : null;
        if (w == null) return;
        if (appFlagSecure || externalFlagSecure) w.addFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE);
        else w.clearFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE);
    }
    private void pushFlagSecure() {
        android.view.ViewGroup.LayoutParams lp = getRootView().getLayoutParams();
        if (lp instanceof android.view.WindowManager.LayoutParams)
            KilnNative.flagSecureChanged((((android.view.WindowManager.LayoutParams) lp).flags
                    & android.view.WindowManager.LayoutParams.FLAG_SECURE) != 0);
    }

    void onHostConfigurationChanged(Configuration config) {
        pushConfiguration(config);
        KilnNative.setDensity(getResources().getDisplayMetrics().density, config.fontScale);
        KilnNative.setFontWeightAdjustment(fontWeightAdjustment(config));
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        KilnNative.surfaceCreated(windowId, holder.getSurface());
        kilnSurfaceValid = true;
        for (int i = 0; i < externals.size(); ++i) syncExternal(externals.keyAt(i), externals.valueAt(i));
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        KilnNative.surfaceChanged(windowId, width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        // External surfaces are children of this one: they go with it and come back after.
        kilnSurfaceValid = false;
        for (int i = 0; i < externals.size(); ++i) destroyExternal(externals.keyAt(i), externals.valueAt(i));
        KilnNative.surfaceDestroyed(windowId);
    }
}
