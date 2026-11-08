package dev.kiln;

import android.app.Activity;
import android.app.UiModeManager;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.drawable.ColorDrawable;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsAnimation;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.util.HashMap;
import java.util.List;

/**
 * activity EdgeToEdge.kt (enableEdgeToEdge and SystemBarStyle) for API 31+: transparent bars, the
 * navigation bar's contrast scrim left to the system for automatic styles, icon appearance from
 * the styles' dark mode, and on API 35+ colour protections behind the bars for scrims that are
 * not transparent (core ProtectionLayout / ColorProtection / SystemBarStateMonitor).
 */
final class KilnEdgeToEdge {
    static final int DEFAULT_LIGHT_SCRIM = Color.argb(0xe6, 0xFF, 0xFF, 0xFF);
    static final int DEFAULT_DARK_SCRIM = Color.argb(0x80, 0x1b, 0x1b, 0x1b);

    /** SystemBarStyle: auto follows the configuration's night mode; dark and light are fixed. */
    static final class Style {
        final int lightScrim, darkScrim, nightMode;
        Style(int lightScrim, int darkScrim, int nightMode) {
            this.lightScrim = lightScrim;
            this.darkScrim = darkScrim;
            this.nightMode = nightMode;
        }
        static Style auto(int lightScrim, int darkScrim) { return new Style(lightScrim, darkScrim, UiModeManager.MODE_NIGHT_AUTO); }
        static Style dark(int scrim) { return new Style(scrim, scrim, UiModeManager.MODE_NIGHT_YES); }
        static Style light(int scrim, int darkScrim) { return new Style(scrim, darkScrim, UiModeManager.MODE_NIGHT_NO); }

        boolean detectDarkMode(Context context) {
            if (nightMode == UiModeManager.MODE_NIGHT_YES) return true;
            if (nightMode == UiModeManager.MODE_NIGHT_NO) return false;
            return (context.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                    == Configuration.UI_MODE_NIGHT_YES;
        }
        int scrimWithEnforcedContrast(boolean isDark) {
            if (nightMode == UiModeManager.MODE_NIGHT_AUTO) return Color.TRANSPARENT;
            return isDark ? darkScrim : lightScrim;
        }
    }

    static Style defaultStatus() { return Style.auto(Color.TRANSPARENT, Color.TRANSPARENT); }
    static Style defaultNavigation() { return Style.auto(DEFAULT_LIGHT_SCRIM, DEFAULT_DARK_SCRIM); }

    private KilnEdgeToEdge() {}

    /** enableEdgeToEdge(statusBarStyle, navigationBarStyle). */
    @SuppressWarnings("deprecation")
    static void enable(Activity activity, Style status, Style navigation) {
        Window window = activity.getWindow();
        View decor = window.getDecorView();
        boolean statusDark = status.detectDarkMode(decor.getContext());
        boolean navigationDark = navigation.detectDarkMode(decor.getContext());
        window.setDecorFitsSystemWindows(false);
        int statusColor = status.scrimWithEnforcedContrast(statusDark);
        int navigationColor = navigation.scrimWithEnforcedContrast(navigationDark);
        if (Build.VERSION.SDK_INT >= 35) {
            // The platform ignores bar colours under enforced edge to edge: protections instead.
            window.setStatusBarColor(Color.TRANSPARENT);
            window.setNavigationBarColor(Color.TRANSPARENT);
            Protections existing = Protections.find(decor);
            if (existing != null) existing.setColors(statusColor, navigationColor);
            else if (statusColor != Color.TRANSPARENT || navigationColor != Color.TRANSPARENT)
                ((ViewGroup) decor).addView(new Protections(decor.getContext(), statusColor, navigationColor));
        } else {
            window.setStatusBarColor(statusColor);
            window.setNavigationBarColor(navigationColor);
            window.setStatusBarContrastEnforced(false);
        }
        window.setNavigationBarContrastEnforced(navigation.nightMode == UiModeManager.MODE_NIGHT_AUTO);
        WindowInsetsController controller = decor.getWindowInsetsController();
        if (controller != null) {
            final int light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS;
            final int lightNav = WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            controller.setSystemBarsAppearance((statusDark ? 0 : light) | (navigationDark ? 0 : lightNav), light | lightNav);
        }
        // EdgeToEdgeApi30.adjustLayoutInDisplayCutoutMode
        WindowManager.LayoutParams attrs = window.getAttributes();
        attrs.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        window.setAttributes(attrs);
    }

    /**
     * ProtectionLayout with four ColorProtections (top: the status colour; left, right, bottom:
     * the navigation colour). Each covers its bar (the system bar insets, at most the tappable
     * ones), sized by the insets ignoring visibility, hidden where there is no bar, and following
     * the bars' show / hide animations by inset amount and alpha.
     */
    static final class Protections extends FrameLayout {
        private static final int LEFT = 1, TOP = 2, RIGHT = 4, BOTTOM = 8;
        // ProtectionGroup order: the list given (all occupy corners), processed last to first.
        private final int[] sides = { TOP, LEFT, RIGHT, BOTTOM };
        private final View[] views = new View[4];
        private final ColorDrawable[] drawables = new ColorDrawable[4];
        private final float[] systemAlpha = { 1, 1, 1, 1 }, insetAmount = { 1, 1, 1, 1 };
        private final int[] thickness = new int[4];
        private Insets insets = Insets.NONE, stable = Insets.NONE;
        private int animationCount;
        private final HashMap<WindowInsetsAnimation, Integer> animationSides = new HashMap<>();

        Protections(Context context, int statusColor, int navigationColor) {
            super(context);
            setTag(Protections.class);
            for (int i = 0; i < 4; i++) {
                drawables[i] = new ColorDrawable();
                View v = new View(context);
                v.setBackground(drawables[i]);
                v.setVisibility(View.GONE);
                views[i] = v;
                addView(v, new LayoutParams(0, 0, gravity(sides[i])));
            }
            setColors(statusColor, navigationColor);
            View detector = new View(context);   // SystemBarStateMonitor's detector
            detector.setVisibility(View.GONE);
            detector.setWillNotDraw(true);
            detector.setOnApplyWindowInsetsListener((view, w) -> {
                Insets now = barInsets(w, false), now2 = barInsets(w, true);
                if (!now.equals(insets) || !now2.equals(stable)) {
                    insets = now;
                    stable = now2;
                    if (animationCount == 0) updateInsets();
                }
                return w;
            });
            detector.setWindowInsetsAnimationCallback(new WindowInsetsAnimation.Callback(WindowInsetsAnimation.Callback.DISPATCH_MODE_STOP) {
                @Override public void onPrepare(WindowInsetsAnimation a) {
                    if (animatesSystemBars(a)) animationCount++;
                }
                @Override public WindowInsetsAnimation.Bounds onStart(WindowInsetsAnimation a, WindowInsetsAnimation.Bounds bounds) {
                    if (!animatesSystemBars(a)) return bounds;
                    Insets upper = bounds.getUpperBound(), lower = bounds.getLowerBound();
                    int s = 0;
                    if (upper.left != lower.left) s |= LEFT;
                    if (upper.top != lower.top) s |= TOP;
                    if (upper.right != lower.right) s |= RIGHT;
                    if (upper.bottom != lower.bottom) s |= BOTTOM;
                    animationSides.put(a, s);
                    return bounds;
                }
                @Override public WindowInsets onProgress(WindowInsets w, List<WindowInsetsAnimation> running) {
                    float[] alpha = { 1, 1, 1, 1 };   // per side index in `sides`
                    int animating = 0;
                    for (int i = running.size() - 1; i >= 0; i--) {
                        Integer s = animationSides.get(running.get(i));
                        if (s == null) continue;
                        for (int k = 0; k < 4; k++) if ((s & sides[k]) != 0) alpha[k] = running.get(i).getAlpha();
                        animating |= s;
                    }
                    Insets now = barInsets(w, false);
                    for (int k = 3; k >= 0; k--) {
                        if ((sides[k] & animating) == 0) continue;
                        views[k].setVisibility(View.VISIBLE);
                        int st = side(stable, sides[k]);
                        if (st > 0) insetAmount[k] = side(now, sides[k]) / (float) st;
                        systemAlpha[k] = alpha[k];
                        apply(k);
                    }
                    return w;
                }
                @Override public void onEnd(WindowInsetsAnimation a) {
                    if (!animatesSystemBars(a)) return;
                    animationSides.remove(a);
                    boolean was = animationCount > 0;
                    animationCount--;
                    if (was && animationCount == 0) updateInsets();
                }
            });
            addView(detector, 0, new LayoutParams(0, 0));
        }

        @Override protected void onAttachedToWindow() {
            super.onAttachedToWindow();
            requestApplyInsets();   // ProtectionLayout.onAttachedToWindow
        }

        static Protections find(View decor) {
            if (!(decor instanceof ViewGroup)) return null;
            ViewGroup g = (ViewGroup) decor;
            for (int i = 0; i < g.getChildCount(); i++)
                if (g.getChildAt(i) instanceof Protections) return (Protections) g.getChildAt(i);
            return null;
        }

        void setColors(int statusColor, int navigationColor) {
            for (int i = 0; i < 4; i++) drawables[i].setColor(sides[i] == TOP ? statusColor : navigationColor);
        }

        private static boolean animatesSystemBars(WindowInsetsAnimation a) {
            return (a.getTypeMask() & WindowInsets.Type.systemBars()) != 0;
        }
        private static Insets barInsets(WindowInsets w, boolean ignoringVisibility) {
            Insets bars = ignoringVisibility ? w.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars()) : w.getInsets(WindowInsets.Type.systemBars());
            Insets tappable = ignoringVisibility ? w.getInsetsIgnoringVisibility(WindowInsets.Type.tappableElement())
                                                 : w.getInsets(WindowInsets.Type.tappableElement());
            return Insets.min(bars, tappable);
        }
        private static int side(Insets i, int s) {
            return s == LEFT ? i.left : s == TOP ? i.top : s == RIGHT ? i.right : i.bottom;
        }
        private static int gravity(int s) {
            return s == LEFT ? Gravity.LEFT : s == TOP ? Gravity.TOP : s == RIGHT ? Gravity.RIGHT : Gravity.BOTTOM;
        }

        // ProtectionGroup.updateInsets: each protection takes the margins the ones after it consumed.
        private void updateInsets() {
            Insets consumed = Insets.NONE;
            for (int k = 3; k >= 0; k--) {
                int s = sides[k];
                LayoutParams lp = (LayoutParams) views[k].getLayoutParams();
                lp.leftMargin = consumed.left;
                lp.topMargin = consumed.top;
                lp.rightMargin = consumed.right;
                lp.bottomMargin = consumed.bottom;
                thickness[k] = side(stable, s);
                if (s == LEFT || s == RIGHT) { lp.width = thickness[k]; lp.height = ViewGroup.LayoutParams.MATCH_PARENT; }
                else { lp.height = thickness[k]; lp.width = ViewGroup.LayoutParams.MATCH_PARENT; }
                views[k].setLayoutParams(lp);
                int inset = side(insets, s);
                views[k].setVisibility(inset > 0 ? View.VISIBLE : View.GONE);
                systemAlpha[k] = inset > 0 ? 1 : 0;
                insetAmount[k] = inset > 0 ? 1 : 0;
                apply(k);
                Insets mine = s == LEFT ? Insets.of(inset, 0, 0, 0) : s == TOP ? Insets.of(0, inset, 0, 0)
                        : s == RIGHT ? Insets.of(0, 0, inset, 0) : Insets.of(0, 0, 0, inset);
                consumed = Insets.max(consumed, mine);
            }
        }

        // Protection.updateAlpha / updateInsetAmount: slid out by the part of the bar not shown.
        private void apply(int k) {
            View v = views[k];
            v.setAlpha(systemAlpha[k]);
            float hidden = 1f - insetAmount[k];
            switch (sides[k]) {
                case LEFT: v.setTranslationX(-hidden * thickness[k]); break;
                case TOP: v.setTranslationY(-hidden * thickness[k]); break;
                case RIGHT: v.setTranslationX(hidden * thickness[k]); break;
                default: v.setTranslationY(hidden * thickness[k]); break;
            }
        }
    }
}
