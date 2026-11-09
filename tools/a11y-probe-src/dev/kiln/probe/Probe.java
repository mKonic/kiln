package dev.kiln.probe;

import android.app.Instrumentation;
import android.app.UiAutomation;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;

/**
 * Reads and drives the active window's accessibility tree the way a service would.
 * am instrument -w -e cmd "dump;click:Tap me;settext:Edit me=hi;focus:Row 1;wait:500" dev.kiln.probe/.Probe
 */
public final class Probe extends Instrumentation {
    private String cmd = "dump";
    private final StringBuilder out = new StringBuilder();
    private final StringBuilder events = new StringBuilder();

    @Override public void onCreate(Bundle args) {
        if (args != null && args.getString("cmd") != null) cmd = args.getString("cmd");
        start();
    }

    @Override public void onStart() {
        UiAutomation ua = getUiAutomation();
        ua.setOnAccessibilityEventListener(e -> {
            if (e.getEventType() == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED) return;
            events.append("  event ").append(AccessibilityEvent.eventTypeToString(e.getEventType()))
                  .append(" text=").append(e.getText());
            if (e.getEventType() == AccessibilityEvent.TYPE_VIEW_SCROLLED)
                events.append(" scrollY=").append(e.getScrollY()).append('/').append(e.getMaxScrollY())
                      .append(" scrollX=").append(e.getScrollX()).append('/').append(e.getMaxScrollX())
                      .append(" delta=").append(e.getScrollDeltaX()).append(',').append(e.getScrollDeltaY());
            events.append('\n');
        });
        try {
            for (String c : cmd.split(";")) {
                c = c.trim();
                if (c.isEmpty()) continue;
                out.append("> ").append(c).append('\n');
                run(ua, c);
                try { ua.waitForIdle(150, 2000); } catch (Exception ignored) {}
            }
        } catch (Throwable t) {
            out.append("error: ").append(t).append('\n');
        }
        out.append("events:\n").append(events);
        Bundle b = new Bundle();
        b.putString(Instrumentation.REPORT_KEY_STREAMRESULT, out.toString());
        finish(0, b);
    }

    private void run(UiAutomation ua, String c) throws Exception {
        String verb = c.contains(":") ? c.substring(0, c.indexOf(':')) : c;
        String arg = c.contains(":") ? c.substring(c.indexOf(':') + 1) : "";
        if (verb.equals("wait")) { Thread.sleep(Long.parseLong(arg)); return; }
        if (verb.equals("swipe")) {
            // Keep UiAutomation connected throughout the gesture and its resulting events.
            String[] p = arg.split(",");
            if (p.length != 5) throw new IllegalArgumentException("swipe:x1,y1,x2,y2,millis");
            float x1 = Float.parseFloat(p[0]), y1 = Float.parseFloat(p[1]);
            float x2 = Float.parseFloat(p[2]), y2 = Float.parseFloat(p[3]);
            long duration = Long.parseLong(p[4]);
            if (duration <= 0) throw new IllegalArgumentException("swipe duration must be positive");
            long down = android.os.SystemClock.uptimeMillis();
            boolean ok = touch(ua, down, down, android.view.MotionEvent.ACTION_DOWN, x1, y1);
            for (long elapsed = 16; elapsed < duration; elapsed += 16) {
                android.os.SystemClock.sleep(Math.max(0, down + elapsed - android.os.SystemClock.uptimeMillis()));
                float f = (float) elapsed / duration;
                ok &= touch(ua, down, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_MOVE,
                        x1 + (x2 - x1) * f, y1 + (y2 - y1) * f);
            }
            android.os.SystemClock.sleep(Math.max(0, down + duration - android.os.SystemClock.uptimeMillis()));
            ok &= touch(ua, down, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_UP, x2, y2);
            out.append("  swipe -> ").append(ok).append('\n');
            return;
        }
        if (verb.equals("clip")) {
            // The primary clip's text, read with the shell's background-clipboard permission.
            ua.adoptShellPermissionIdentity("android.permission.READ_CLIPBOARD_IN_BACKGROUND");
            try {
                android.content.ClipboardManager cm = (android.content.ClipboardManager)
                    getContext().getSystemService(android.content.Context.CLIPBOARD_SERVICE);
                android.content.ClipData clip = cm.getPrimaryClip();
                out.append("clip: ").append(clip == null || clip.getItemCount() == 0 ? "(none)" : String.valueOf(clip.getItemAt(0).getText())).append('\n');
            } finally {
                ua.dropShellPermissionIdentity();
            }
            return;
        }
        AccessibilityNodeInfo root = null;
        for (int i = 0; i < 40 && root == null; i++) {   // the first window list arrives after connecting
            root = ua.getRootInActiveWindow();
            if (root == null) Thread.sleep(50);
        }
        if (root == null) { out.append("no active window\n"); return; }
        if (verb.equals("dump") || verb.equals("vdump")) { verbose = verb.equals("vdump"); dump(root, 0); return; }
        if (verb.equals("scroll") || verb.equals("scrollback")) {
            // The n-th scrollable node in pre-order (0 when no argument).
            java.util.List<AccessibilityNodeInfo> list = new java.util.ArrayList<>();
            scrollables(root, list);
            int i = arg.isEmpty() ? 0 : Integer.parseInt(arg);
            if (i >= list.size()) { out.append("no scrollable ").append(i).append('\n'); return; }
            boolean ok = list.get(i).performAction(verb.equals("scroll") ? AccessibilityNodeInfo.ACTION_SCROLL_FORWARD
                                                                         : AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD);
            out.append("  ").append(verb).append(' ').append(i).append(" -> ").append(ok).append('\n');
            return;
        }
        String find = arg, value = null;
        if (verb.equals("settext") || verb.equals("setprogress") || verb.equals("action")) { find = arg.substring(0, arg.indexOf('=')); value = arg.substring(arg.indexOf('=') + 1); }
        AccessibilityNodeInfo n = findByText(root, find);
        if (n == null) { out.append("not found: ").append(find).append('\n'); return; }
        boolean ok;
        switch (verb) {
            // Like TalkBack on the unmerged tree: the nearest clickable node at or above the match.
            case "click": ok = clickableAt(n, false).performAction(AccessibilityNodeInfo.ACTION_CLICK); break;
            case "longclick": ok = clickableAt(n, true).performAction(AccessibilityNodeInfo.ACTION_LONG_CLICK); break;
            case "focus": ok = n.performAction(AccessibilityNodeInfo.ACTION_FOCUS); break;
            case "a11yfocus": ok = n.performAction(AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS); break;
            case "show": ok = n.performAction(android.R.id.accessibilityActionShowOnScreen); break;
            case "word": case "char": case "line": case "para": {
                Bundle a = new Bundle();
                int g = verb.equals("word") ? AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD : verb.equals("char") ? AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER
                        : verb.equals("line") ? AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE : AccessibilityNodeInfo.MOVEMENT_GRANULARITY_PARAGRAPH;
                a.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT, g);
                ok = n.performAction(AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, a);
                break;
            }
            case "chars": {
                // The first five character locations (extra data).
                Bundle a = new Bundle();
                a.putInt(AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_START_INDEX, 0);
                a.putInt(AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_LENGTH, 5);
                ok = n.refreshWithExtraData(AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY, a);
                android.os.Parcelable[] rects = n.getExtras().getParcelableArray(AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY, android.graphics.RectF.class);
                out.append("  rects=").append(rects == null ? "null" : java.util.Arrays.toString(rects)).append('\n');
                break;
            }
            case "action": {
                ok = false;
                for (AccessibilityNodeInfo.AccessibilityAction a : n.getActionList())
                    if (a.getLabel() != null && a.getLabel().toString().equals(value)) ok = n.performAction(a.getId());
                break;
            }
            case "setprogress": {
                Bundle a = new Bundle();
                a.putFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE, Float.parseFloat(value));
                ok = n.performAction(android.R.id.accessibilityActionSetProgress, a);
                break;
            }
            case "settext": {
                Bundle a = new Bundle();
                a.putCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, value);
                ok = n.performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, a);
                break;
            }
            default: out.append("unknown verb ").append(verb).append('\n'); return;
        }
        out.append("  ").append(verb).append(" -> ").append(ok).append('\n');
    }

    private static boolean touch(UiAutomation ua, long down, long time, int action, float x, float y) {
        android.view.MotionEvent event = android.view.MotionEvent.obtain(down, time, action, x, y, 0);
        event.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);
        try { return ua.injectInputEvent(event, true); }
        finally { event.recycle(); }
    }

    private static AccessibilityNodeInfo clickableAt(AccessibilityNodeInfo n, boolean longClick) {
        for (AccessibilityNodeInfo at = n; at != null; at = at.getParent())
            if (longClick ? at.isLongClickable() : at.isClickable()) return at;
        return n;
    }

    private void scrollables(AccessibilityNodeInfo n, java.util.List<AccessibilityNodeInfo> out) {
        if (n == null) return;
        if (n.isScrollable()) out.add(n);
        for (int i = 0; i < n.getChildCount(); i++) scrollables(n.getChild(i), out);
    }

    private AccessibilityNodeInfo findByText(AccessibilityNodeInfo n, String text) {
        if (n == null) return null;
        CharSequence t = n.getText(), d = n.getContentDescription();
        if ((t != null && t.toString().equals(text)) || (d != null && d.toString().equals(text))) return n;
        for (int i = 0; i < n.getChildCount(); i++) {
            AccessibilityNodeInfo r = findByText(n.getChild(i), text);
            if (r != null) return r;
        }
        return null;
    }

    private boolean verbose;

    private static String actionName(int id) {
        switch (id) {
            case AccessibilityNodeInfo.ACTION_CLICK: return "click";
            case AccessibilityNodeInfo.ACTION_LONG_CLICK: return "longclick";
            case AccessibilityNodeInfo.ACTION_FOCUS: return "focus";
            case AccessibilityNodeInfo.ACTION_CLEAR_FOCUS: return "clearfocus";
            case AccessibilityNodeInfo.ACTION_SCROLL_FORWARD: return "fwd";
            case AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD: return "back";
            case AccessibilityNodeInfo.ACTION_SET_SELECTION: return "setsel";
            case AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY: return "next";
            case AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY: return "prev";
            case AccessibilityNodeInfo.ACTION_SET_TEXT: return "settext";
            case AccessibilityNodeInfo.ACTION_COPY: return "copy";
            case AccessibilityNodeInfo.ACTION_PASTE: return "paste";
            case AccessibilityNodeInfo.ACTION_EXPAND: return "expand";
            case AccessibilityNodeInfo.ACTION_COLLAPSE: return "collapse";
            case AccessibilityNodeInfo.ACTION_DISMISS: return "dismiss";
            default:
                if (id == android.R.id.accessibilityActionScrollDown) return "down";
                if (id == android.R.id.accessibilityActionScrollUp) return "up";
                if (id == android.R.id.accessibilityActionScrollLeft) return "left";
                if (id == android.R.id.accessibilityActionScrollRight) return "right";
                if (id == android.R.id.accessibilityActionSetProgress) return "setprogress";
                if (id == android.R.id.accessibilityActionShowOnScreen) return "show";
                if (id == android.R.id.accessibilityActionPageLeft) return "pageleft";
                if (id == android.R.id.accessibilityActionPageRight) return "pageright";
                return "0x" + Integer.toHexString(id);
        }
    }

    private void dump(AccessibilityNodeInfo n, int depth) {
        if (n == null) return;
        Rect r = new Rect();
        n.getBoundsInScreen(r);
        out.append("  ".repeat(depth + 1)).append(n.getClassName());
        if (n.getText() != null) out.append(" text=\"").append(n.getText()).append('"');
        if (n.getContentDescription() != null) out.append(" desc=\"").append(n.getContentDescription()).append('"');
        out.append(' ').append(r.toShortString());
        if (n.isClickable()) out.append(" clickable");
        if (n.isFocusable()) out.append(" focusable");
        if (n.isFocused()) out.append(" focused");
        if (n.isAccessibilityFocused()) out.append(" a11yfocused");
        if (n.isEditable()) out.append(" editable sel=").append(n.getTextSelectionStart()).append('-').append(n.getTextSelectionEnd());
        if (n.isPassword()) out.append(" password");
        if (n.isHeading()) out.append(" heading");
        if (!n.isVisibleToUser()) out.append(" invisible");
        if (n.isScrollable()) out.append(" scrollable");
        if (!n.isEnabled()) out.append(" disabled");
        if (n.getCollectionInfo() != null) out.append(" collection=").append(n.getCollectionInfo().getRowCount()).append('x').append(n.getCollectionInfo().getColumnCount());
        if (n.isScreenReaderFocusable()) out.append(" srf");
        if (n.isCheckable()) out.append(n.isChecked() ? " checked" : " unchecked");
        if (n.getStateDescription() != null) out.append(" state=\"").append(n.getStateDescription()).append('"');
        if (n.getRangeInfo() != null) out.append(" range=").append(n.getRangeInfo().getCurrent());
        if (verbose) {
            StringBuilder acts = new StringBuilder();
            for (AccessibilityNodeInfo.AccessibilityAction a : n.getActionList()) {
                if (a.getId() == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS || a.getId() == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) continue;
                if (acts.length() > 0) acts.append(',');
                acts.append(a.getLabel() != null ? a.getLabel() : actionName(a.getId()));
            }
            if (acts.length() > 0) out.append(" actions=[").append(acts).append(']');
            AccessibilityNodeInfo before = n.getTraversalBefore();
            if (before != null) out.append(" before=\"").append(before.getText() != null ? before.getText() : before.getContentDescription()).append('"');
        }
        out.append('\n');
        for (int i = 0; i < n.getChildCount(); i++) dump(n.getChild(i), depth + 1);
    }
}
