package dev.kiln;

import android.content.ContentResolver;
import android.content.Context;
import android.database.ContentObserver;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.accessibility.AccessibilityManager;

/**
 * The high-contrast text setting for kiln's text drawing: AccessibilityManager's state and
 * listener on API 36+, the secure setting before (where the platform draws only the outline).
 */
final class KilnHighContrast {
    private static final String SETTING = "high_text_contrast_enabled";

    private final Context context;
    private Object listener;   // AccessibilityManager.HighContrastTextStateChangeListener (36+)
    private ContentObserver observer;

    private KilnHighContrast(Context context) { this.context = context.getApplicationContext(); }

    static KilnHighContrast start(Context context) {
        KilnHighContrast h = new KilnHighContrast(context);
        h.register();
        return h;
    }

    private void register() {
        if (Build.VERSION.SDK_INT >= 36) {
            Api36.register(this);
            return;
        }
        ContentResolver resolver = context.getContentResolver();
        Uri uri = Settings.Secure.getUriFor(SETTING);
        observer = new ContentObserver(new Handler(Looper.getMainLooper())) {
            @Override public void onChange(boolean selfChange) { push(readSetting()); }
        };
        try {
            resolver.registerContentObserver(uri, false, observer);
        } catch (SecurityException e) {
            observer = null;
        }
        push(readSetting());
    }

    void stop() {
        if (Build.VERSION.SDK_INT >= 36) {
            Api36.unregister(this);
        } else if (observer != null) {
            context.getContentResolver().unregisterContentObserver(observer);
            observer = null;
        }
    }

    private boolean readSetting() {
        try {
            return Settings.Secure.getInt(context.getContentResolver(), SETTING, 0) != 0;
        } catch (SecurityException e) {
            return false;
        }
    }

    private static void push(boolean enabled) {
        KilnNative.setHighContrastText(enabled, Build.VERSION.SDK_INT >= 36);
    }

    /** Kept apart so older devices never load the API 36 listener type. */
    private static final class Api36 {
        static void register(KilnHighContrast h) {
            AccessibilityManager am = h.context.getSystemService(AccessibilityManager.class);
            if (am == null) return;
            AccessibilityManager.HighContrastTextStateChangeListener l = KilnHighContrast::push;
            h.listener = l;
            am.addHighContrastTextStateChangeListener(h.context.getMainExecutor(), l);
            push(am.isHighContrastTextEnabled());
        }
        static void unregister(KilnHighContrast h) {
            AccessibilityManager am = h.context.getSystemService(AccessibilityManager.class);
            if (am != null && h.listener != null)
                am.removeHighContrastTextStateChangeListener((AccessibilityManager.HighContrastTextStateChangeListener) h.listener);
            h.listener = null;
        }
    }
}
