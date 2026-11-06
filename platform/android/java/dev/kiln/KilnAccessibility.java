package dev.kiln;

import android.graphics.Rect;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;

/**
 * Accessibility for the kiln tree (spec 01 req. 5): every semantics node becomes a virtual view.
 * Node data comes from native in two arrays (structure/flags/bounds, and strings).
 */
final class KilnAccessibility extends AccessibilityNodeProvider {
    // int[] layout from native: exists, parent, selStart, selEnd, flags, role, l, t, r, b, childCount, children...
    static final int F_CLICKABLE = 1, F_FOCUSABLE = 2, F_FOCUSED = 4, F_EDITABLE = 8, F_HEADING = 16,
            F_DISABLED = 32, F_PASSWORD = 64, F_HAS_SELECTED = 128, F_SELECTED = 256, F_CHECKABLE = 512,
            F_CHECKED = 1024, F_LONG_CLICK = 2048;
    private static final String[] ROLE_CLASS = { null, "android.widget.Button", "android.widget.CheckBox",
            "android.widget.Switch", "android.widget.RadioButton", "android.widget.TabWidget",
            "android.widget.ImageView", "android.widget.Spinner", "android.widget.NumberPicker", "android.widget.HorizontalScrollView" };

    private final KilnView view;
    private int accessibilityFocused = Integer.MIN_VALUE;
    private int hovered = Integer.MIN_VALUE;

    KilnAccessibility(KilnView view) { this.view = view; }

    @Override
    public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
        int[] d = KilnNative.semNode(id == View.NO_ID ? -1 : id);
        if (id == View.NO_ID) {
            AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(view);
            view.onInitializeAccessibilityNodeInfo(info);
            for (int i = 0; i < d[10]; i++) info.addChild(view, d[11 + i]);
            return info;
        }
        if (d[0] == 0) return null;
        String[] s = KilnNative.semStrings(id);   // text, contentDescription, editableText, stateDescription, paneTitle, onClickLabel
        AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(view, id);
        info.setPackageName(view.getContext().getPackageName());
        info.setSource(view, id);
        if (d[1] == -1) info.setParent(view); else info.setParent(view, d[1]);
        int[] loc = new int[2];
        view.getLocationOnScreen(loc);
        info.setBoundsInScreen(new Rect(loc[0] + d[6], loc[1] + d[7], loc[0] + d[8], loc[1] + d[9]));
        for (int i = 0; i < d[10]; i++) info.addChild(view, d[11 + i]);
        final int f = d[4];
        final boolean editable = (f & F_EDITABLE) != 0;
        String cls = editable ? "android.widget.EditText" : (d[5] > 0 && d[5] < ROLE_CLASS.length ? ROLE_CLASS[d[5]] : null);
        if (cls == null) cls = !TextUtils.isEmpty(s[0]) ? "android.widget.TextView" : "android.view.View";
        info.setClassName(cls);
        info.setText(editable ? s[2] : (TextUtils.isEmpty(s[0]) ? null : s[0]));
        if (!TextUtils.isEmpty(s[1])) info.setContentDescription(s[1]);
        if (!TextUtils.isEmpty(s[3])) info.setStateDescription(s[3]);
        if (!TextUtils.isEmpty(s[4])) info.setPaneTitle(s[4]);
        info.setEnabled((f & F_DISABLED) == 0);
        info.setVisibleToUser(true);
        info.setImportantForAccessibility(true);
        info.setHeading((f & F_HEADING) != 0);
        info.setPassword((f & F_PASSWORD) != 0);
        if ((f & F_CLICKABLE) != 0) {
            info.setClickable(true);
            // onClick(label): the label is what TalkBack says for "double-tap to ...".
            info.addAction(TextUtils.isEmpty(s[5]) ? AccessibilityNodeInfo.AccessibilityAction.ACTION_CLICK
                    : new AccessibilityNodeInfo.AccessibilityAction(AccessibilityNodeInfo.ACTION_CLICK, s[5]));
        }
        if ((f & F_LONG_CLICK) != 0) { info.setLongClickable(true); info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_LONG_CLICK); }
        if ((f & F_FOCUSABLE) != 0) {
            info.setFocusable(true);
            info.setFocused((f & F_FOCUSED) != 0);
            info.addAction((f & F_FOCUSED) != 0 ? AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_FOCUS
                                                : AccessibilityNodeInfo.AccessibilityAction.ACTION_FOCUS);
        }
        if (editable) {
            info.setEditable(true);
            info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_TEXT);
            info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_SELECTION);
            info.setTextSelection(d[2], d[3]);
        }
        if ((f & F_CHECKABLE) != 0) { info.setCheckable(true); info.setChecked((f & F_CHECKED) != 0); }
        if ((f & F_HAS_SELECTED) != 0) info.setSelected((f & F_SELECTED) != 0);
        // Scrolling (after the children): flags, collection rows and columns.
        final int sf = d[11 + d[10]];
        final boolean hasCollection = (sf & 512) != 0;
        if ((sf & 3) != 0) {
            if (!hasCollection) info.setClassName((sf & 2) != 0 ? "android.widget.ScrollView" : "android.widget.HorizontalScrollView");
            if ((sf & 256) != 0) info.setScrollable(true);
            if ((f & F_DISABLED) == 0) {
                if ((sf & 4) != 0) { info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD); info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_RIGHT); }
                if ((sf & 8) != 0) { info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD); info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_LEFT); }
                if ((sf & 16) != 0) { info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD); info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_DOWN); }
                if ((sf & 32) != 0) { info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD); info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_UP); }
            }
        }
        if ((sf & 1024) != 0) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_PAGE_UP);
        if ((sf & 2048) != 0) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_PAGE_DOWN);
        if ((sf & 4096) != 0) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_PAGE_LEFT);
        if ((sf & 8192) != 0) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_PAGE_RIGHT);
        if (hasCollection) info.setCollectionInfo(AccessibilityNodeInfo.CollectionInfo.obtain(d[12 + d[10]], d[13 + d[10]], false));
        info.setAccessibilityFocused(id == accessibilityFocused);
        info.addAction(id == accessibilityFocused ? AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_ACCESSIBILITY_FOCUS
                                                  : AccessibilityNodeInfo.AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS);
        return info;
    }

    @Override
    public boolean performAction(int id, int action, Bundle args) {
        if (id == View.NO_ID) return view.performAccessibilityAction(action, args);
        switch (action) {
            case AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS:
                if (accessibilityFocused == id) return false;
                if (accessibilityFocused != Integer.MIN_VALUE) send(accessibilityFocused, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                accessibilityFocused = id;
                view.invalidate();
                send(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED);
                return true;
            case AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS:
                if (accessibilityFocused != id) return false;
                accessibilityFocused = Integer.MIN_VALUE;
                send(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                return true;
            case AccessibilityNodeInfo.ACTION_CLICK: {
                boolean ok = KilnNative.semAction(id, 0, null);
                if (ok) send(id, AccessibilityEvent.TYPE_VIEW_CLICKED);
                return ok;
            }
            case AccessibilityNodeInfo.ACTION_LONG_CLICK: return KilnNative.semAction(id, 1, null);
            case AccessibilityNodeInfo.ACTION_FOCUS: return KilnNative.semAction(id, 2, null);
            case AccessibilityNodeInfo.ACTION_SET_TEXT: {
                CharSequence t = args != null ? args.getCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE) : null;
                return KilnNative.semAction(id, 3, t == null ? "" : t.toString());
            }
            case AccessibilityNodeInfo.ACTION_SCROLL_FORWARD: return scroll(id, "f");
            case AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD: return scroll(id, "b");
            default:
                if (action == android.R.id.accessibilityActionScrollUp) return scroll(id, "u");
                if (action == android.R.id.accessibilityActionScrollDown) return scroll(id, "d");
                if (action == android.R.id.accessibilityActionScrollLeft) return scroll(id, "l");
                if (action == android.R.id.accessibilityActionScrollRight) return scroll(id, "r");
                if (action == android.R.id.accessibilityActionPageUp) return page(id, "u");
                if (action == android.R.id.accessibilityActionPageDown) return page(id, "d");
                if (action == android.R.id.accessibilityActionPageLeft) return page(id, "l");
                if (action == android.R.id.accessibilityActionPageRight) return page(id, "r");
                return false;
        }
    }

    private boolean page(int id, String direction) {
        boolean ok = KilnNative.semAction(id, 5, direction);
        if (ok) send(id, AccessibilityEvent.TYPE_VIEW_SCROLLED);
        return ok;
    }

    private boolean scroll(int id, String direction) {
        boolean ok = KilnNative.semAction(id, 4, direction);
        if (ok) send(id, AccessibilityEvent.TYPE_VIEW_SCROLLED);
        return ok;
    }

    void send(int id, int type) {
        android.view.accessibility.AccessibilityManager am =
                view.getContext().getSystemService(android.view.accessibility.AccessibilityManager.class);
        if (am == null || !am.isEnabled() || view.getParent() == null) return;
        AccessibilityEvent e = AccessibilityEvent.obtain(type);
        e.setPackageName(view.getContext().getPackageName());
        e.setSource(view, id);
        view.getParent().requestSendAccessibilityEvent(view, e);
    }

    /** Explore by touch: announce the node under the finger (Compose's hover dispatch). */
    boolean onHover(int action, float x, float y) {
        int id = KilnNative.semHitTest(x, y);
        if (action == android.view.MotionEvent.ACTION_HOVER_EXIT) id = Integer.MIN_VALUE;
        if (id == hovered) return id != Integer.MIN_VALUE;
        if (hovered != Integer.MIN_VALUE) send(hovered, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
        hovered = id;
        if (id != Integer.MIN_VALUE) send(id, AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
        return id != Integer.MIN_VALUE;
    }
}
