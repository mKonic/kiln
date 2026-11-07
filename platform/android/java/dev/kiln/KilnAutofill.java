package dev.kiln;

import android.graphics.Rect;
import android.os.Build;
import android.text.InputType;
import android.util.SparseArray;
import android.view.View;
import android.view.ViewStructure;
import android.view.autofill.AutofillId;
import android.view.autofill.AutofillManager;
import android.view.autofill.AutofillValue;
import android.view.inputmethod.EditorInfo;

/**
 * Autofill for kiln's semantics nodes (Compose's PlatformAutofillManagerImpl and
 * ViewStructure.populate): the platform AutofillManager calls native makes, the virtual structure
 * built from native's nodes, and filled values handed back.
 */
final class KilnAutofill {
    private final View view;
    private AutofillManager manager;
    private final Rect rect = new Rect();

    KilnAutofill(View view) { this.view = view; }

    private AutofillManager manager() {
        if (manager == null) manager = view.getContext().getSystemService(AutofillManager.class);
        return manager;
    }

    void notifyEntered(int id, int l, int t, int r, int b) {
        AutofillManager m = manager();
        if (m == null) return;
        rect.set(l, t, r, b);
        m.notifyViewEntered(view, id, rect);
    }

    void notifyExited(int id) {
        AutofillManager m = manager();
        if (m != null) m.notifyViewExited(view, id);
    }

    void notifyValue(int id, int kind, String text, int intValue, long date) {
        AutofillManager m = manager();
        if (m != null) m.notifyValueChanged(view, id, valueOf(kind, text, intValue, date));
    }

    void notifyVisibility(int id, boolean visible) {
        AutofillManager m = manager();
        if (m != null) m.notifyViewVisibilityChanged(view, id, visible);
    }

    void commit() {
        AutofillManager m = manager();
        if (m != null) m.commit();
    }

    void cancel() {
        AutofillManager m = manager();
        if (m != null) m.cancel();
    }

    void request(int id, int l, int t, int r, int b) {
        AutofillManager m = manager();
        if (m == null) return;
        rect.set(l, t, r, b);
        m.requestAutofill(view, id, rect);
    }

    private static AutofillValue valueOf(int kind, String text, int intValue, long date) {
        switch (kind) {
            case 1: return AutofillValue.forToggle(intValue != 0);
            case 2: return AutofillValue.forList(intValue);
            case 3: return AutofillValue.forDate(date);
            default: return AutofillValue.forText(text != null ? text : "");
        }
    }

    private static final int INTS = 11, STRINGS = 5;

    /** populateViewStructure: the root (this view's structure), then each node under its parent. */
    void populate(ViewStructure root) {
        Object[] data = KilnNative.autofillStructure();
        if (data == null) return;
        int[] ints = (int[]) data[0];
        String[] strs = (String[]) data[1];
        long[] dates = (long[]) data[2];
        Object[] requests = data.length > 3 ? (Object[]) data[3] : null;
        int n = ints.length / INTS;
        if (n == 0) return;
        AutofillId rootId = view.getAutofillId();
        String packageName = view.getContext().getPackageName();
        ViewStructure[] structures = new ViewStructure[n];
        for (int i = 0; i < n; ++i) {
            int parent = ints[i * INTS + 1];
            ViewStructure s;
            if (parent < 0) {
                s = root;
            } else {
                ViewStructure p = structures[parent];
                int index = p.addChildCount(1);
                s = p.newChild(index);
            }
            structures[i] = s;
            populateNode(s, ints, i * INTS, strs, i * STRINGS, dates[i], rootId, packageName);
            if (Build.VERSION.SDK_INT >= 35 && requests != null && requests[i] != null)
                setPendingCredentialRequest(s, ints[i * INTS], (Object[]) requests[i]);
        }
    }

    private static void populateNode(ViewStructure s, int[] in, int o, String[] str, int so, long date, AutofillId rootId,
                                     String packageName) {
        int id = in[o];
        int type = in[o + 2], valueKind = in[o + 3], valueInt = in[o + 4];
        int left = in[o + 5], top = in[o + 6], right = in[o + 7], bottom = in[o + 8];
        int flags = in[o + 9], maxLength = in[o + 10];
        String valueText = str[so], hints = str[so + 1], description = str[so + 2], text = str[so + 3], className = str[so + 4];
        if (description != null) s.setContentDescription(description);
        if ((flags & 1) != 0) s.setFocused((flags & 2) != 0);
        if ((flags & 8) != 0) s.setClickable(true);
        if ((flags & 16) != 0) s.setLongClickable(true);
        if ((flags & 32) != 0) s.setFocusable(true);
        if ((flags & 4) == 0) s.setEnabled(false);
        s.setAutofillId(rootId, id);
        s.setId(id, packageName, null, null);
        if (type >= 0) s.setAutofillType(type);
        if (valueKind >= 0) s.setAutofillValue(valueOf(valueKind, valueText, valueInt, date));
        if (hints != null) s.setAutofillHints(hints.split("\n"));
        s.setDimens(left, top, 0, 0, right - left, bottom - top);
        if ((flags & 64) != 0) s.setSelected((flags & 128) != 0);
        if ((flags & 256) != 0) {
            s.setCheckable(true);
            s.setChecked((flags & 512) != 0);
        }
        s.setDataIsSensitive((flags & 1024) != 0);
        s.setVisibility(View.VISIBLE);
        if (text != null) s.setText(text);
        if (className != null) s.setClassName(className);
        if ("android.widget.EditText".equals(className)) {
            if (Build.VERSION.SDK_INT >= 28 && maxLength >= 0) s.setMaxTextLength(maxLength);
            if ((flags & 2048) != 0) s.setInputType(InputType.TYPE_CLASS_TEXT | EditorInfo.TYPE_TEXT_VARIATION_PASSWORD);
        }
    }

    // --- Credential Manager requests (AutofillApi35Helper.setPendingCredentialRequest) ----------

    private final android.os.Handler main = new android.os.Handler(android.os.Looper.getMainLooper());

    private void setPendingCredentialRequest(ViewStructure s, int id, Object[] request) {
        android.credentials.GetCredentialRequest.Builder b = new android.credentials.GetCredentialRequest.Builder(KilnBundles.toBundle((Object[]) request[0]));
        for (Object o : (Object[]) request[1]) {
            Object[] opt = (Object[]) o;
            b.addCredentialOption(new android.credentials.CredentialOption.Builder(
                    (String) opt[0], KilnBundles.toBundle((Object[]) opt[1]), KilnBundles.toBundle((Object[]) opt[2]))
                    .setIsSystemProviderRequired("true".equals(opt[3])).build());
        }
        s.setPendingCredentialRequest(b.build(),
                new android.os.OutcomeReceiver<android.credentials.GetCredentialResponse, android.credentials.GetCredentialException>() {
                    @Override public void onResult(android.credentials.GetCredentialResponse response) {
                        android.credentials.Credential c = response.getCredential();
                        Object[] data = KilnBundles.fromBundle(c.getData());
                        String type = c.getType();
                        main.post(() -> KilnNative.credentialResult(id, type, data));
                    }
                    @Override public void onError(android.credentials.GetCredentialException e) {
                        String type = e.getType(), message = e.getMessage();
                        main.post(() -> KilnNative.credentialError(id, type, message));
                    }
                });
    }

    /** performAutofill. */
    void autofill(SparseArray<AutofillValue> values) {
        for (int i = 0; i < values.size(); ++i) {
            int id = values.keyAt(i);
            AutofillValue v = values.valueAt(i);
            if (v.isText()) KilnNative.autofillPerform(id, 0, v.getTextValue().toString(), 0, 0);
            else if (v.isToggle()) KilnNative.autofillPerform(id, 1, null, v.getToggleValue() ? 1 : 0, 0);
            else if (v.isList()) KilnNative.autofillPerform(id, 2, null, v.getListValue(), 0);
            else if (v.isDate()) KilnNative.autofillPerform(id, 3, null, 0, v.getDateValue());
        }
    }
}
