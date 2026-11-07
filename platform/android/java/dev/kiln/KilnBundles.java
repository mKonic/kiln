package dev.kiln;

import android.net.Uri;
import android.os.Bundle;

/**
 * Bundles as native passes them: {String[] keys, int[] kinds, Object[] values} with kind 0
 * String, 1 boolean and 2 int (as text), 3 String[] (a string list or array), 4 byte[] (may be
 * null), 5 Uri (as text), 6 int[], 7 long (as text).
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
                default: break;
            }
        }
        return b;
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
                default: break;
            }
        }
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
                else if (v instanceof java.util.ArrayList) {
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
