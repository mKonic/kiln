package dev.kiln;

import android.app.Activity;
import android.app.LocaleManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.os.Build;
import android.os.LocaleList;

import java.lang.ref.WeakReference;

/**
 * ui::SetApplicationLocales (AppCompatDelegate.setApplicationLocales): LocaleManager on Android 13
 * and later; before, the locales are stored and applied to each activity's configuration as it
 * starts (attachBaseContext), and the running activity is recreated, as AppCompat does.
 */
final class KilnLocales {
    private static final String PREFS = "kiln_app_locales";
    private static Context sContext;
    private static WeakReference<Activity> sActivity = new WeakReference<>(null);

    private KilnLocales() {}

    static void attach(Activity activity) {
        sContext = activity.getApplicationContext();
        sActivity = new WeakReference<>(activity);
    }

    private static String stored(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("locales", "");
    }

    static String get() {
        Context c = sContext;
        if (c == null) return "";
        if (Build.VERSION.SDK_INT >= 33) {
            LocaleManager lm = c.getSystemService(LocaleManager.class);
            return lm == null ? "" : lm.getApplicationLocales().toLanguageTags();
        }
        return stored(c);
    }

    static void set(String tags) {
        Context c = sContext;
        if (c == null) return;
        if (Build.VERSION.SDK_INT >= 33) {
            // The system keeps it and sends the activity a locale configuration change.
            LocaleManager lm = c.getSystemService(LocaleManager.class);
            if (lm != null) lm.setApplicationLocales(LocaleList.forLanguageTags(tags));
            return;
        }
        if (tags.equals(stored(c))) return;
        SharedPreferences.Editor e = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit();
        e.putString("locales", tags);
        e.commit();
        Activity a = sActivity.get();
        if (a != null) a.recreate();
    }

    /**
     * Before Android 13: the override the activity applies in attachBaseContext
     * (applyOverrideConfiguration, a configuration with only the locales set), or null.
     */
    static Configuration override(Context base) {
        if (Build.VERSION.SDK_INT >= 33) return null;
        String tags = stored(base);
        if (tags.isEmpty()) return null;
        Configuration config = new Configuration();
        config.setLocales(LocaleList.forLanguageTags(tags));
        LocaleList.setDefault(config.getLocales());
        return config;
    }
}
