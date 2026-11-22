package dev.kiln;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.CancellationSignal;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;

import java.util.HashMap;

/** Vibration and location for kiln's ui::Vibration and ui::Location (the biometric prompt is KilnBiometric). */
final class KilnDevice {
    private static Context sContext;
    // Requests in progress by native id: LocationListeners.
    private static final HashMap<Integer, Object> sRequests = new HashMap<>();

    private KilnDevice() {}

    static native void locationResult(int id, boolean hasFix, double[] values, String provider, boolean done);

    static void attach(Context context) { sContext = context.getApplicationContext(); }

    /** ContextCompat.checkSelfPermission. */
    static boolean checkSelfPermission(String permission) {
        return sContext != null && sContext.checkSelfPermission(permission) == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    // --- vibration ---------------------------------------------------------------------------

    private static Vibrator vibrator() {
        VibratorManager vm = sContext.getSystemService(VibratorManager.class);
        return vm != null ? vm.getDefaultVibrator() : null;
    }
    static boolean hasVibrator() { Vibrator v = vibrator(); return v != null && v.hasVibrator(); }
    static boolean hasAmplitudeControl() { Vibrator v = vibrator(); return v != null && v.hasAmplitudeControl(); }
    static void cancelVibration() { Vibrator v = vibrator(); if (v != null) v.cancel(); }

    /** kind: 0 one shot, 1 waveform, 2 predefined, 3 composition. */
    static boolean vibrate(int kind, long[] timings, int[] amplitudes, int repeat, int effect, int[] primitives, float[] scales, int[] delays) {
        Vibrator v = vibrator();
        if (v == null || !v.hasVibrator()) return false;
        try {
            VibrationEffect e;
            switch (kind) {
                case 0: e = VibrationEffect.createOneShot(timings[0], amplitudes.length > 0 ? amplitudes[0] : VibrationEffect.DEFAULT_AMPLITUDE); break;
                case 1: e = amplitudes.length > 0 ? VibrationEffect.createWaveform(timings, amplitudes, repeat) : VibrationEffect.createWaveform(timings, repeat); break;
                case 2: e = VibrationEffect.createPredefined(effect); break;
                case 3: {
                    if (!v.areAllPrimitivesSupported(primitives)) return false;
                    VibrationEffect.Composition c = VibrationEffect.startComposition();
                    for (int i = 0; i < primitives.length; ++i) c.addPrimitive(primitives[i], scales[i], delays[i]);
                    e = c.compose();
                    break;
                }
                default: return false;
            }
            v.vibrate(e);
            return true;
        } catch (IllegalArgumentException | SecurityException ex) {
            android.util.Log.w("kiln", "vibration: " + ex.getMessage());
            return false;
        }
    }

    // --- location ----------------------------------------------------------------------------

    private static LocationManager locations() { return sContext.getSystemService(LocationManager.class); }

    private static boolean permitted() {
        return sContext.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
                || sContext.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * The best enabled provider: fused, then GPS, then network. GPS only with precise location:
     * with approximate location alone the system refuses it (SecurityException).
     */
    private static String provider(LocationManager lm, String asked) {
        if (!asked.isEmpty()) return asked;
        if (lm.hasProvider(LocationManager.FUSED_PROVIDER) && lm.isProviderEnabled(LocationManager.FUSED_PROVIDER)) return LocationManager.FUSED_PROVIDER;
        boolean fine = sContext.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
        if (fine && lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) return LocationManager.GPS_PROVIDER;
        return LocationManager.NETWORK_PROVIDER;
    }

    /** {lat, lon, altitude, accuracy, speed, bearing, time}; NaN where the fix has none. */
    private static double[] pack(Location l) {
        return new double[] { l.getLatitude(), l.getLongitude(), l.hasAltitude() ? l.getAltitude() : Double.NaN,
                l.hasAccuracy() ? l.getAccuracy() : Double.NaN, l.hasSpeed() ? l.getSpeed() : Double.NaN,
                l.hasBearing() ? l.getBearing() : Double.NaN, l.getTime() };
    }

    static boolean locationEnabled() { LocationManager lm = locations(); return lm != null && lm.isLocationEnabled(); }

    /** getLastKnownLocation over the providers, the newest. */
    static Object[] lastKnown() {
        LocationManager lm = locations();
        if (lm == null || !permitted()) return null;
        Location best = null;
        try {
            for (String p : lm.getProviders(true)) {
                Location l = lm.getLastKnownLocation(p);
                if (l != null && (best == null || l.getTime() > best.getTime())) best = l;
            }
        } catch (SecurityException e) {
            return null;
        }
        return best == null ? null : new Object[] { pack(best), best.getProvider() };
    }

    static boolean current(int id, String asked) {
        LocationManager lm = locations();
        if (lm == null || !permitted()) return false;
        CancellationSignal signal = new CancellationSignal();
        try {
            String p = provider(lm, asked);
            sRequests.put(id, signal);
            lm.getCurrentLocation(p, signal, sContext.getMainExecutor(), l -> {
                sRequests.remove(id);
                locationResult(id, l != null, l != null ? pack(l) : null, l != null ? l.getProvider() : null, true);
            });
            return true;
        } catch (SecurityException | IllegalArgumentException e) {
            sRequests.remove(id);
            return false;
        }
    }

    static boolean updates(int id, String asked, long intervalMs, float minDistance) {
        LocationManager lm = locations();
        if (lm == null || !permitted()) return false;
        LocationListener listener = l -> locationResult(id, true, pack(l), l.getProvider(), false);
        try {
            lm.requestLocationUpdates(provider(lm, asked), intervalMs, minDistance, listener, Looper.getMainLooper());
            sRequests.put(id, listener);
            return true;
        } catch (SecurityException | IllegalArgumentException e) {
            return false;
        }
    }

    /** Cancels a location request. */
    static void cancel(int id) {
        Object o = sRequests.remove(id);
        if (o instanceof LocationListener) {
            LocationManager lm = locations();
            if (lm != null) lm.removeUpdates((LocationListener) o);
        }
    }
}
