package dev.kiln;

import android.net.Uri;
import android.os.Bundle;

/**
 * Bundles as native passes them: {String[] keys, int[] kinds, Object[] values} with kind 0
 * String, 1 boolean and 2 int (as text), 3 String[] (a string list or array), 4 byte[] (may be
 * null), 5 Uri (as text), 6 int[], 7 long (as text), 8 ArrayList<Uri> (String[]), 9 Intent (as
 * KilnActivityResults passes intents), 10 a null binder (Bundle.putBinder(key, null)), 11 a Bundle
 * (nested arrays), 12 a Bitmap ({byte[] encoded or null, String drawable}), 13 a PendingIntent
 * opening the app at a deep link (String), 14 ArrayList<Bundle> (Object[] of bundles), 15
 * SparseArray<Bundle> ({int[] keys, Object[] bundles}).
 */
final class KilnBundles {
    private KilnBundles() {}

    static Bundle toBundle(Object[] arrays) {
        Bundle b = new Bundle();
        if (arrays == null) return b;
        String[] keys = (String[]) arrays[0];
        int[] kinds = (int[]) arrays[1];
        Object[] values = (Object[]) arrays[2];
        for (int i = 0; i < keys.length; ++i) {
            Object v = values[i];
            switch (kinds[i]) {
                case 0: b.putString(keys[i], (String) v); break;
                case 1: b.putBoolean(keys[i], "true".equals(v)); break;
                case 2: b.putInt(keys[i], Integer.parseInt((String) v)); break;
                case 3: b.putStringArrayList(keys[i], new java.util.ArrayList<>(java.util.Arrays.asList((String[]) v))); break;
                case 4: b.putByteArray(keys[i], (byte[]) v); break;
                case 5: b.putParcelable(keys[i], Uri.parse((String) v)); break;
                case 6: b.putIntArray(keys[i], (int[]) v); break;
                case 7: b.putLong(keys[i], Long.parseLong((String) v)); break;
                case 8: b.putParcelableArrayList(keys[i], uris((String[]) v)); break;
                case 9: b.putParcelable(keys[i], KilnActivityResults.toIntent((Object[]) v)); break;
                case 10: b.putBinder(keys[i], null); break;
                case 11: b.putBundle(keys[i], toBundle((Object[]) v)); break;
                case 12: b.putParcelable(keys[i], bitmap((Object[]) v)); break;
                case 13: b.putParcelable(keys[i], openApp((String) v)); break;
                case 14: b.putParcelableArrayList(keys[i], bundles((Object[]) v)); break;
                case 15: b.putSparseParcelableArray(keys[i], sparse((Object[]) v)); break;
                case 16: b.putBundle(keys[i], animations((String[]) v)); break;
                default: break;
            }
        }
        return b;
    }

    static android.graphics.Bitmap bitmap(Object[] v) {
        byte[] bytes = (byte[]) v[0];
        if (bytes != null) return android.graphics.BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
        android.content.Context c = KilnContent.context();
        String name = (String) v[1];
        if (c == null || name == null || name.isEmpty()) return null;
        int id = c.getResources().getIdentifier(name, "drawable", c.getPackageName());
        if (id == 0) id = c.getResources().getIdentifier(name, "mipmap", c.getPackageName());
        if (id == 0) return null;
        android.graphics.drawable.Drawable d = c.getDrawable(id);
        if (d == null) return null;
        if (d instanceof android.graphics.drawable.BitmapDrawable) return ((android.graphics.drawable.BitmapDrawable) d).getBitmap();
        int w = Math.max(1, d.getIntrinsicWidth()), h = Math.max(1, d.getIntrinsicHeight());
        android.graphics.Bitmap bm = android.graphics.Bitmap.createBitmap(w, h, android.graphics.Bitmap.Config.ARGB_8888);
        android.graphics.Canvas canvas = new android.graphics.Canvas(bm);
        d.setBounds(0, 0, w, h);
        d.draw(canvas);
        return bm;
    }

    private static int sRequestCode = 0x4b000000;

    /** The app's launcher activity at `data` (as notification actions open it); mutable so a
     * browser can fill in the page's URL. */
    static android.app.PendingIntent openApp(String data) {
        android.content.Context c = KilnContent.context();
        if (c == null) return null;
        android.content.Intent launch = c.getPackageManager().getLaunchIntentForPackage(c.getPackageName());
        android.content.Intent i = new android.content.Intent(data.isEmpty() ? android.content.Intent.ACTION_MAIN : android.content.Intent.ACTION_VIEW);
        if (launch != null && launch.getComponent() != null) i.setComponent(launch.getComponent());
        if (!data.isEmpty()) i.setData(Uri.parse(data));
        i.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK | android.content.Intent.FLAG_ACTIVITY_SINGLE_TOP | android.content.Intent.FLAG_ACTIVITY_CLEAR_TOP);
        return android.app.PendingIntent.getActivity(c, ++sRequestCode, i, android.app.PendingIntent.FLAG_MUTABLE | android.app.PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private static int animId(android.content.Context c, String name) {
        if (name == null || name.isEmpty()) return 0;
        int id = c.getResources().getIdentifier(name, "anim", c.getPackageName());
        if (id == 0) id = c.getResources().getIdentifier(name, "animator", c.getPackageName());
        if (id == 0) id = c.getResources().getIdentifier(name, "anim", "android");
        return id;
    }

    /** ActivityOptions.makeCustomAnimation(enter, exit).toBundle(). */
    static Bundle animations(String[] v) {
        android.content.Context c = KilnContent.context();
        if (c == null) return null;
        return android.app.ActivityOptions.makeCustomAnimation(c, animId(c, v[0]), animId(c, v[1])).toBundle();
    }

    /** startActivity's options: {enter, exit, share identity, pass through}; null when none. */
    static Bundle activityOptions(android.content.Context c, String[] o) {
        if (o == null || (o[0].isEmpty() && o[1].isEmpty() && o[2].isEmpty() && o[3].isEmpty())) return null;
        android.app.ActivityOptions options = !o[0].isEmpty() || !o[1].isEmpty()
                ? android.app.ActivityOptions.makeCustomAnimation(c, animId(c, o[0]), animId(c, o[1]))
                : android.app.ActivityOptions.makeBasic();
        if (!o[2].isEmpty() && android.os.Build.VERSION.SDK_INT >= 34) options.setShareIdentityEnabled("true".equals(o[2]));
        if (!o[3].isEmpty() && android.os.Build.VERSION.SDK_INT >= 36) options.setAllowPassThroughOnTouchOutside("true".equals(o[3]));
        return options.toBundle();
    }

    private static java.util.ArrayList<Bundle> bundles(Object[] v) {
        java.util.ArrayList<Bundle> list = new java.util.ArrayList<>();
        for (Object o : v) list.add(toBundle((Object[]) o));
        return list;
    }

    private static android.util.SparseArray<Bundle> sparse(Object[] v) {
        int[] ids = (int[]) v[0];
        Object[] items = (Object[]) v[1];
        android.util.SparseArray<Bundle> out = new android.util.SparseArray<>();
        for (int i = 0; i < ids.length; ++i) out.put(ids[i], toBundle((Object[]) items[i]));
        return out;
    }

    /** An Intent's extras need String[] as an array (EXTRA_MIME_TYPES, EXTRA_PERMISSIONS). */
    static void putExtras(android.content.Intent intent, Object[] arrays) {
        if (arrays == null) return;
        String[] keys = (String[]) arrays[0];
        int[] kinds = (int[]) arrays[1];
        Object[] values = (Object[]) arrays[2];
        for (int i = 0; i < keys.length; ++i) {
            Object v = values[i];
            switch (kinds[i]) {
                case 0: intent.putExtra(keys[i], (String) v); break;
                case 1: intent.putExtra(keys[i], "true".equals(v)); break;
                case 2: intent.putExtra(keys[i], Integer.parseInt((String) v)); break;
                case 3: intent.putExtra(keys[i], (String[]) v); break;
                case 4: intent.putExtra(keys[i], (byte[]) v); break;
                case 5: intent.putExtra(keys[i], Uri.parse((String) v)); break;
                case 6: intent.putExtra(keys[i], (int[]) v); break;
                case 7: intent.putExtra(keys[i], Long.parseLong((String) v)); break;
                case 8: intent.putParcelableArrayListExtra(keys[i], uris((String[]) v)); break;
                case 9: intent.putExtra(keys[i], KilnActivityResults.toIntent((Object[]) v)); break;
                case 10: { Bundle nb = new Bundle(); nb.putBinder(keys[i], null); intent.putExtras(nb); break; }
                case 11: intent.putExtra(keys[i], toBundle((Object[]) v)); break;
                case 12: intent.putExtra(keys[i], bitmap((Object[]) v)); break;
                case 13: intent.putExtra(keys[i], openApp((String) v)); break;
                case 14: intent.putParcelableArrayListExtra(keys[i], bundles((Object[]) v)); break;
                case 15: { Bundle nb = new Bundle(); nb.putSparseParcelableArray(keys[i], sparse((Object[]) v)); intent.putExtras(nb); break; }
                case 16: intent.putExtra(keys[i], animations((String[]) v)); break;
                default: break;
            }
        }
    }

    private static java.util.ArrayList<Uri> uris(String[] strs) {
        java.util.ArrayList<Uri> list = new java.util.ArrayList<>();
        for (String s : strs) list.add(Uri.parse(s));
        return list;
    }

    @SuppressWarnings("deprecation")
    static Object[] fromBundle(Bundle b) {
        java.util.ArrayList<String> keys = new java.util.ArrayList<>();
        java.util.ArrayList<Integer> kinds = new java.util.ArrayList<>();
        java.util.ArrayList<Object> values = new java.util.ArrayList<>();
        if (b != null) {
            for (String k : b.keySet()) {
                Object v = b.get(k);
                int kind;
                Object out;
                if (v instanceof String) { kind = 0; out = v; }
                else if (v instanceof Boolean) { kind = 1; out = v.toString(); }
                else if (v instanceof Integer) { kind = 2; out = v.toString(); }
                else if (v instanceof java.util.ArrayList && !((java.util.ArrayList<?>) v).isEmpty() && ((java.util.ArrayList<?>) v).get(0) instanceof Uri) {
                    java.util.ArrayList<?> list = (java.util.ArrayList<?>) v;
                    String[] strs = new String[list.size()];
                    for (int i = 0; i < strs.length; ++i) strs[i] = String.valueOf(list.get(i));
                    kind = 8;
                    out = strs;
                } else if (v instanceof android.content.Intent) {
                    kind = 9;
                    out = KilnActivityResults.fromIntent((android.content.Intent) v);
                } else if (v instanceof java.util.ArrayList) {
                    java.util.ArrayList<?> list = (java.util.ArrayList<?>) v;
                    String[] strs = new String[list.size()];
                    for (int i = 0; i < strs.length; ++i) strs[i] = String.valueOf(list.get(i));
                    kind = 3;
                    out = strs;
                } else if (v instanceof String[]) { kind = 3; out = v; }
                else if (v instanceof byte[]) { kind = 4; out = v; }
                else if (v instanceof Uri) { kind = 5; out = v.toString(); }
                else if (v instanceof int[]) { kind = 6; out = v; }
                else if (v instanceof Long) { kind = 7; out = v.toString(); }
                else if (v instanceof Bundle) { kind = 11; out = fromBundle((Bundle) v); }
                else continue;
                keys.add(k);
                kinds.add(kind);
                values.add(out);
            }
        }
        int[] k = new int[kinds.size()];
        for (int i = 0; i < k.length; ++i) k[i] = kinds.get(i);
        return new Object[] { keys.toArray(new String[0]), k, values.toArray() };
    }
}
