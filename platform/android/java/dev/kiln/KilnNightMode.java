package dev.kiln;

import android.Manifest;
import android.app.Activity;
import android.app.UiModeManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.location.Location;
import android.location.LocationManager;
import android.os.PowerManager;

import java.lang.ref.WeakReference;
import java.util.Calendar;

/**
 * ui::SetDefaultNightMode (AppCompatDelegate.setDefaultNightMode): the app's night mode, kept in
 * the process. Activities starting take it as an override configuration (attachBaseContext);
 * the running one, which handles uiMode changes itself, gets its resources updated and
 * onConfigurationChanged (AppCompatDelegateImpl.updateAppConfiguration). The automatic modes
 * follow battery saver (AutoBatteryNightModeManager) or the time of day (TwilightManager).
 */
final class KilnNightMode {
    // AppCompatDelegate.MODE_NIGHT_*
    static final int FOLLOW_SYSTEM = -1, AUTO_TIME = 0, NO = 1, YES = 2, AUTO_BATTERY = 3;

    private static int sMode = FOLLOW_SYSTEM;
    private static WeakReference<Activity> sActivity = new WeakReference<>(null);
    private static BroadcastReceiver sReceiver;
    private static int sReceiverMode = FOLLOW_SYSTEM;
    private static boolean sTwilightNight;
    private static long sTwilightNextUpdate;

    private KilnNightMode() {}

    static void attach(Activity activity) {
        sActivity = new WeakReference<>(activity);
        apply();
    }

    static int get() { return sMode; }

    static void set(int mode) {
        if (mode != FOLLOW_SYSTEM && mode != AUTO_TIME && mode != NO && mode != YES && mode != AUTO_BATTERY) return;
        if (sMode == mode) return;
        sMode = mode;
        apply();
    }

    /** AppCompatDelegateImpl.mapNightMode, as UI_MODE_NIGHT_YES / _NO, or 0 to follow the system. */
    private static int nightBits(Context c) {
        switch (sMode) {
            case YES: return Configuration.UI_MODE_NIGHT_YES;
            case NO: return Configuration.UI_MODE_NIGHT_NO;
            case AUTO_BATTERY: {
                PowerManager pm = c.getSystemService(PowerManager.class);
                return pm != null && pm.isPowerSaveMode() ? Configuration.UI_MODE_NIGHT_YES : Configuration.UI_MODE_NIGHT_NO;
            }
            case AUTO_TIME: {
                // The system's own automatic night mode already does it.
                UiModeManager ui = c.getSystemService(UiModeManager.class);
                if (ui != null && ui.getNightMode() == UiModeManager.MODE_NIGHT_AUTO) return 0;
                return isNight(c) ? Configuration.UI_MODE_NIGHT_YES : Configuration.UI_MODE_NIGHT_NO;
            }
            default: return 0;
        }
    }

    /** The uiMode the app's configuration carries, from the system's. */
    static int uiMode(Context c, int systemUiMode) {
        int bits = nightBits(c);
        return bits == 0 ? systemUiMode : (systemUiMode & ~Configuration.UI_MODE_NIGHT_MASK) | bits;
    }

    /** The system's uiMode: the application's configuration, which no override touches. */
    private static int systemUiMode(Context c) {
        return c.getApplicationContext().getResources().getConfiguration().uiMode;
    }

    /** For attachBaseContext: an override with the app's night mode, or null to follow the system. */
    static Configuration override(Context base) {
        int bits = nightBits(base);
        if (bits == 0) return null;
        Configuration config = new Configuration();
        config.uiMode = (systemUiMode(base) & ~Configuration.UI_MODE_NIGHT_MASK) | bits;
        return config;
    }

    /**
     * For onConfigurationChanged: the configuration with the app's night mode (an override from
     * attachBaseContext may be stale after a change), the activity's resources updated to it.
     */
    static Configuration correct(Activity a, Configuration config) {
        int wanted = uiMode(a, systemUiMode(a));
        if ((config.uiMode & Configuration.UI_MODE_NIGHT_MASK) == (wanted & Configuration.UI_MODE_NIGHT_MASK)) return config;
        Configuration fixed = new Configuration(config);
        fixed.uiMode = (config.uiMode & ~Configuration.UI_MODE_NIGHT_MASK) | (wanted & Configuration.UI_MODE_NIGHT_MASK);
        updateResources(a, fixed);
        return fixed;
    }

    @SuppressWarnings("deprecation")
    private static void updateResources(Activity a, Configuration config) {
        Resources res = a.getResources();
        res.updateConfiguration(config, null);
    }

    /** applyDayNight: the running activity follows the mode now. */
    static void apply() {
        Activity a = sActivity.get();
        if (a == null) return;
        updateReceiver(a);
        Resources res = a.getResources();
        int current = res.getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
        int wanted = uiMode(a, systemUiMode(a)) & Configuration.UI_MODE_NIGHT_MASK;
        if (current == wanted) return;
        // updateResourcesConfiguration, then the activity's own handling (it declares uiMode).
        Configuration conf = new Configuration(res.getConfiguration());
        conf.uiMode = wanted | (conf.uiMode & ~Configuration.UI_MODE_NIGHT_MASK);
        updateResources(a, conf);
        a.onConfigurationChanged(conf);
    }

    /** AutoNightModeManager: re-applied on battery saver or time changes while a mode needs it. */
    private static void updateReceiver(Context c) {
        Context app = c.getApplicationContext();
        if (sReceiver != null && sReceiverMode == sMode) return;
        if (sReceiver != null) {
            app.unregisterReceiver(sReceiver);
            sReceiver = null;
        }
        IntentFilter filter = new IntentFilter();
        if (sMode == AUTO_BATTERY) {
            filter.addAction(PowerManager.ACTION_POWER_SAVE_MODE_CHANGED);
        } else if (sMode == AUTO_TIME) {
            filter.addAction(Intent.ACTION_TIME_CHANGED);
            filter.addAction(Intent.ACTION_TIMEZONE_CHANGED);
            filter.addAction(Intent.ACTION_TIME_TICK);
        } else {
            sReceiverMode = sMode;
            return;
        }
        sReceiver = new BroadcastReceiver() {
            @Override public void onReceive(Context context, Intent intent) { apply(); }
        };
        sReceiverMode = sMode;
        app.registerReceiver(sReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
    }

    // --- TwilightManager -------------------------------------------------------------------------

    private static boolean isNight(Context c) {
        long now = System.currentTimeMillis();
        if (sTwilightNextUpdate > now) return sTwilightNight;
        Location location = lastKnownLocation(c);
        if (location != null) {
            updateTwilight(location, now);
            return sTwilightNight;
        }
        // No location (no permission): fixed sunrise and sunset.
        int hour = Calendar.getInstance().get(Calendar.HOUR_OF_DAY);
        return hour < 6 || hour >= 22;
    }

    private static Location lastKnownLocation(Context c) {
        LocationManager lm = c.getSystemService(LocationManager.class);
        if (lm == null) return null;
        Location coarse = null, fine = null;
        if (c.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED)
            coarse = lastKnown(lm, LocationManager.NETWORK_PROVIDER);
        if (c.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED)
            fine = lastKnown(lm, LocationManager.GPS_PROVIDER);
        if (fine != null && coarse != null) return fine.getTime() > coarse.getTime() ? fine : coarse;
        return fine != null ? fine : coarse;
    }

    @SuppressWarnings("MissingPermission")
    private static Location lastKnown(LocationManager lm, String provider) {
        try {
            if (lm.isProviderEnabled(provider)) return lm.getLastKnownLocation(provider);
        } catch (Exception ignored) {
        }
        return null;
    }

    private static final long DAY = 24L * 60 * 60 * 1000, HOUR = 60L * 60 * 1000, MINUTE = 60L * 1000;

    private static void updateTwilight(Location location, long now) {
        double lat = location.getLatitude(), lng = location.getLongitude();
        long[] today = calculateTwilight(now, lat, lng);
        boolean night = today[2] == 1;
        long todaySunrise = today[0], todaySunset = today[1];
        long tomorrowSunrise = calculateTwilight(now + DAY, lat, lng)[0];
        long next;
        if (todaySunrise == -1 || todaySunset == -1) {
            next = now + 12 * HOUR;
        } else {
            next = now > todaySunset ? tomorrowSunrise : now > todaySunrise ? todaySunset : todaySunrise;
            next += MINUTE;
        }
        sTwilightNight = night;
        sTwilightNextUpdate = next;
    }

    // TwilightCalculator.calculateTwilight: { sunrise, sunset, 1 if night }.
    private static final float DEGREES_TO_RADIANS = (float) (Math.PI / 180.0f);
    private static final float J0 = 0.0009f;
    private static final float ALTIDUTE_CORRECTION_CIVIL_TWILIGHT = -0.104719755f;
    private static final float C1 = 0.0334196f, C2 = 0.000349066f, C3 = 0.000005236f;
    private static final float OBLIQUITY = 0.40927971f;
    private static final long UTC_2000 = 946728000000L;

    static long[] calculateTwilight(long time, double latitude, double longitude) {
        final float daysSince2000 = (float) (time - UTC_2000) / DAY;
        final float meanAnomaly = 6.240059968f + daysSince2000 * 0.01720197f;
        final double trueAnomaly = meanAnomaly + C1 * Math.sin(meanAnomaly) + C2 * Math.sin(2 * meanAnomaly)
                + C3 * Math.sin(3 * meanAnomaly);
        final double solarLng = trueAnomaly + 1.796593063d + Math.PI;
        final double arcLongitude = -longitude / 360;
        float n = Math.round(daysSince2000 - J0 - arcLongitude);
        double solarTransitJ2000 = n + J0 + arcLongitude + 0.0053d * Math.sin(meanAnomaly) + -0.0069d * Math.sin(2 * solarLng);
        double solarDec = Math.asin(Math.sin(solarLng) * Math.sin(OBLIQUITY));
        final double latRad = latitude * DEGREES_TO_RADIANS;
        double cosHourAngle = (Math.sin(ALTIDUTE_CORRECTION_CIVIL_TWILIGHT) - Math.sin(latRad) * Math.sin(solarDec))
                / (Math.cos(latRad) * Math.cos(solarDec));
        if (cosHourAngle >= 1) return new long[] { -1, -1, 1 };
        if (cosHourAngle <= -1) return new long[] { -1, -1, 0 };
        float hourAngle = (float) (Math.acos(cosHourAngle) / (2 * Math.PI));
        long sunset = Math.round((solarTransitJ2000 + hourAngle) * DAY) + UTC_2000;
        long sunrise = Math.round((solarTransitJ2000 - hourAngle) * DAY) + UTC_2000;
        return new long[] { sunrise, sunset, sunrise < time && sunset > time ? 0 : 1 };
    }
}
