package dev.kiln;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.util.Log;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.Random;

/**
 * The host half of androidx.activity's ActivityResultRegistry with ComponentActivity's onLaunch:
 * request codes bound to native launcher keys, the launched keys, and results that arrive while
 * no callback is registered, all kept across activity recreation; permission requests go through
 * requestPermissions, everything else through startActivityForResult.
 */
final class KilnActivityResults {
    private static final String LOG_TAG = "ActivityResultRegistry";
    private static final int INITIAL_REQUEST_CODE_VALUE = 0x00010000;
    private static final String KEY_RCS = "KEY_COMPONENT_ACTIVITY_REGISTERED_RCS";
    private static final String KEY_KEYS = "KEY_COMPONENT_ACTIVITY_REGISTERED_KEYS";
    private static final String KEY_LAUNCHED = "KEY_COMPONENT_ACTIVITY_LAUNCHED_KEYS";
    private static final String KEY_PENDING = "KEY_COMPONENT_ACTIVITY_PENDING_RESULT";
    static final String ACTION_REQUEST_PERMISSIONS = "androidx.activity.result.contract.action.REQUEST_PERMISSIONS";
    static final String EXTRA_PERMISSIONS = "androidx.activity.result.contract.extra.PERMISSIONS";
    static final String EXTRA_PERMISSION_GRANT_RESULTS = "androidx.activity.result.contract.extra.PERMISSION_GRANT_RESULTS";
    static final String ACTION_PICK_VISUAL_MEDIA = "dev.kiln.action.PICK_VISUAL_MEDIA";
    // PickVisualMedia's system fallback picker.
    static final String ACTION_SYSTEM_FALLBACK_PICK_IMAGES = "androidx.activity.result.contract.action.PICK_IMAGES";
    static final String EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_MAX = "androidx.activity.result.contract.extra.PICK_IMAGES_MAX";
    static final String EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_LAUNCH_TAB = "androidx.activity.result.contract.extra.PICK_IMAGES_LAUNCH_TAB";
    static final String EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_IN_ORDER = "androidx.activity.result.contract.extra.PICK_IMAGES_IN_ORDER";
    static final String EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_ACCENT_COLOR = "androidx.activity.result.contract.extra.PICK_IMAGES_ACCENT_COLOR";

    private final Activity activity;
    private final Map<Integer, String> rcToKey = new HashMap<>();
    private final Map<String, Integer> keyToRc = new HashMap<>();
    private final ArrayList<String> launchedKeys = new ArrayList<>();
    private final java.util.HashSet<String> keysWithCallback = new java.util.HashSet<>();
    private final Bundle pendingResults = new Bundle();   // key -> {code, data}
    private final Random random = new Random();

    KilnActivityResults(Activity activity) { this.activity = activity; }

    // --- register / unregister (the native launcher's DisposableEffect) ------------------------

    void register(String key) {
        registerKey(key);
        keysWithCallback.add(key);
        Bundle pending = pendingResults.getBundle(key);
        if (pending != null) {
            pendingResults.remove(key);
            @SuppressWarnings("deprecation") Intent data = pending.getParcelable("data");
            KilnNative.activityResult(key, pending.getInt("code"), fromIntent(data));
        }
    }

    void unregister(String key) {
        if (!launchedKeys.contains(key)) {
            Integer rc = keyToRc.remove(key);
            if (rc != null) rcToKey.remove(rc);
        }
        keysWithCallback.remove(key);
        if (pendingResults.containsKey(key)) {
            Log.w(LOG_TAG, "Dropping pending result for request " + key);
            pendingResults.remove(key);
        }
    }

    private void registerKey(String key) {
        if (keyToRc.containsKey(key)) return;
        int rc;
        do {
            rc = random.nextInt(Integer.MAX_VALUE - INITIAL_REQUEST_CODE_VALUE + 1) + INITIAL_REQUEST_CODE_VALUE;
        } while (rcToKey.containsKey(rc));
        rcToKey.put(rc, key);
        keyToRc.put(key, rc);
    }

    // --- launch (onLaunch) -------------------------------------------------------------------

    /** False when no activity handles the intent (ActivityNotFoundException). */
    @SuppressWarnings("deprecation")
    boolean launch(String key, Object[] spec) {
        Integer rc = keyToRc.get(key);
        if (rc == null) throw new IllegalStateException("Attempting to launch an unregistered ActivityResultLauncher");
        launchedKeys.add(key);
        Intent intent = toIntent(spec);
        try {
            if (ACTION_REQUEST_PERMISSIONS.equals(intent.getAction())) {
                String[] permissions = intent.getStringArrayExtra(EXTRA_PERMISSIONS);
                if (permissions == null) permissions = new String[0];
                activity.requestPermissions(permissions, rc);
            } else {
                if (ACTION_PICK_VISUAL_MEDIA.equals(intent.getAction())) intent = pickVisualMediaIntent(intent);
                activity.startActivityForResult(intent, rc);
            }
            return true;
        } catch (ActivityNotFoundException e) {
            launchedKeys.remove(key);
            Log.w(LOG_TAG, "No activity for " + intent, e);
            return false;
        }
    }

    // --- results -----------------------------------------------------------------------------

    boolean dispatchResult(int requestCode, int resultCode, Intent data) {
        String key = rcToKey.get(requestCode);
        if (key == null) return false;
        // doDispatch: to the callback for a launched key, otherwise kept.
        if (keysWithCallback.contains(key) && launchedKeys.contains(key)) {
            launchedKeys.remove(key);
            KilnNative.activityResult(key, resultCode, fromIntent(data));
        } else {
            Bundle pending = new Bundle();
            pending.putInt("code", resultCode);
            pending.putParcelable("data", data);
            pendingResults.putBundle(key, pending);
        }
        return true;
    }

    boolean dispatchPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        return dispatchResult(requestCode, Activity.RESULT_OK,
                new Intent().putExtra(EXTRA_PERMISSIONS, permissions).putExtra(EXTRA_PERMISSION_GRANT_RESULTS, grantResults));
    }

    // --- saved state -------------------------------------------------------------------------

    void onSaveInstanceState(Bundle out) {
        ArrayList<Integer> rcs = new ArrayList<>();
        ArrayList<String> keys = new ArrayList<>();
        for (Map.Entry<String, Integer> e : keyToRc.entrySet()) {
            keys.add(e.getKey());
            rcs.add(e.getValue());
        }
        out.putIntegerArrayList(KEY_RCS, rcs);
        out.putStringArrayList(KEY_KEYS, keys);
        out.putStringArrayList(KEY_LAUNCHED, new ArrayList<>(launchedKeys));
        out.putBundle(KEY_PENDING, new Bundle(pendingResults));
    }

    void onRestoreInstanceState(Bundle saved) {
        if (saved == null) return;
        ArrayList<Integer> rcs = saved.getIntegerArrayList(KEY_RCS);
        ArrayList<String> keys = saved.getStringArrayList(KEY_KEYS);
        if (keys == null || rcs == null) return;
        ArrayList<String> launched = saved.getStringArrayList(KEY_LAUNCHED);
        if (launched != null) launchedKeys.addAll(launched);
        Bundle pending = saved.getBundle(KEY_PENDING);
        if (pending != null) pendingResults.putAll(pending);
        for (int i = 0; i < keys.size(); ++i) {
            String key = keys.get(i);
            if (keyToRc.containsKey(key)) {
                Integer newRc = keyToRc.remove(key);
                if (!pendingResults.containsKey(key)) rcToKey.remove(newRc);
            }
            rcToKey.put(rcs.get(i), key);
            keyToRc.put(key, rcs.get(i));
        }
    }

    // --- PickVisualMedia.createIntent's three branches ---------------------------------------

    static boolean isSystemPickerAvailable() {
        if (Build.VERSION.SDK_INT >= 33) return true;
        return android.os.ext.SdkExtensions.getExtensionVersion(Build.VERSION_CODES.R) >= 2;
    }

    @SuppressWarnings("deprecation")
    private ResolveInfo systemFallbackPicker() {
        return activity.getPackageManager().resolveActivity(new Intent(ACTION_SYSTEM_FALLBACK_PICK_IMAGES),
                PackageManager.MATCH_DEFAULT_ONLY | PackageManager.MATCH_SYSTEM_ONLY);
    }

    private Intent pickVisualMediaIntent(Intent request) {
        String type = request.getStringExtra("type");
        boolean multiple = request.hasExtra("max");
        int max = request.getIntExtra("max", Integer.MAX_VALUE);
        int tab = request.getIntExtra("tab", MediaStore.PICK_IMAGES_TAB_IMAGES);
        boolean ordered = request.getBooleanExtra("ordered", false);
        boolean accent = request.hasExtra("accent");
        long accentColor = request.getLongExtra("accent", 0);
        if (isSystemPickerAvailable()) {
            Intent i = new Intent(MediaStore.ACTION_PICK_IMAGES);
            i.setType(type);
            if (multiple) {
                int current = Math.min(max, MediaStore.getPickImagesMaxLimit());
                if (current <= 1) throw new IllegalArgumentException("Max items must be greater than 1 and lesser than or equal to MediaStore.getPickImagesMaxLimit()");
                i.putExtra(MediaStore.EXTRA_PICK_IMAGES_MAX, current);
                i.putExtra(MediaStore.EXTRA_PICK_IMAGES_IN_ORDER, ordered);
            }
            i.putExtra(MediaStore.EXTRA_PICK_IMAGES_LAUNCH_TAB, tab);
            if (accent) i.putExtra(MediaStore.EXTRA_PICK_IMAGES_ACCENT_COLOR, accentColor);
            int[] hdr = request.getIntArrayExtra("hdr");
            if (Build.VERSION.SDK_INT >= 33 && hdr != null) {
                android.media.ApplicationMediaCapabilities.Builder b = new android.media.ApplicationMediaCapabilities.Builder();
                b.addSupportedVideoMimeType(android.media.MediaFormat.MIMETYPE_VIDEO_HEVC);
                for (int h : hdr) {
                    switch (h) {
                        case 0: b.addSupportedHdrType(android.media.MediaFeature.HdrType.HLG); break;
                        case 1: b.addSupportedHdrType(android.media.MediaFeature.HdrType.HDR10); break;
                        case 2: b.addSupportedHdrType(android.media.MediaFeature.HdrType.HDR10_PLUS); break;
                        case 3: b.addSupportedHdrType(android.media.MediaFeature.HdrType.DOLBY_VISION); break;
                        default: break;
                    }
                }
                i.putExtra(MediaStore.EXTRA_MEDIA_CAPABILITIES, b.build());
            }
            return i;
        }
        ResolveInfo fallback = systemFallbackPicker();
        if (fallback != null) {
            Intent i = new Intent(ACTION_SYSTEM_FALLBACK_PICK_IMAGES);
            i.setClassName(fallback.activityInfo.applicationInfo.packageName, fallback.activityInfo.name);
            i.setType(type);
            if (multiple) {
                if (max <= 1) throw new IllegalArgumentException("Max items must be greater than 1");
                i.putExtra(EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_MAX, max);
                i.putExtra(EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_IN_ORDER, ordered);
            }
            i.putExtra(EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_LAUNCH_TAB, tab);
            if (accent) i.putExtra(EXTRA_SYSTEM_FALLBACK_PICK_IMAGES_ACCENT_COLOR, accentColor);
            return i;
        }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.setType(type);
        if (multiple) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        if (type == null) {
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "image/*", "video/*" });
        }
        return i;
    }

    // --- intents as native passes them -------------------------------------------------------
    // {action, data, type, String[] categories, packageName, className, flags (text), extras
    // bundle arrays, String[] clip URIs}

    static Intent toIntent(Object[] s) {
        Intent i = new Intent();
        if (s[0] != null) i.setAction((String) s[0]);
        Uri data = s[1] != null ? Uri.parse((String) s[1]) : null;
        String type = (String) s[2];
        if (data != null && type != null) i.setDataAndType(data, type);
        else if (data != null) i.setData(data);
        else if (type != null) i.setType(type);
        for (String c : (String[]) s[3]) i.addCategory(c);
        if (s[4] != null && s[5] != null && !((String) s[4]).isEmpty()) i.setClassName((String) s[4], (String) s[5]);
        i.addFlags(Integer.parseInt((String) s[6]));
        KilnBundles.putExtras(i, (Object[]) s[7]);
        return i;
    }

    static Object[] fromIntent(Intent i) {
        if (i == null) return null;
        java.util.ArrayList<String> clip = new java.util.ArrayList<>();
        if (i.getClipData() != null)
            for (int k = 0; k < i.getClipData().getItemCount(); ++k) {
                Uri u = i.getClipData().getItemAt(k).getUri();
                if (u != null) clip.add(u.toString());
            }
        java.util.ArrayList<String> cats = new java.util.ArrayList<>();
        if (i.getCategories() != null) cats.addAll(i.getCategories());
        return new Object[] {
            i.getAction(), i.getData() != null ? i.getData().toString() : null, i.getType(), cats.toArray(new String[0]),
            i.getComponent() != null ? i.getComponent().getPackageName() : "", i.getComponent() != null ? i.getComponent().getClassName() : "",
            Integer.toString(i.getFlags()), KilnBundles.fromBundle(i.getExtras()), clip.toArray(new String[0]),
        };
    }
}
