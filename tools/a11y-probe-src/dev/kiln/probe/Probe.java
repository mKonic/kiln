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
                long started = android.os.SystemClock.uptimeMillis();
                try { run(ua, c); }
                finally { out.append("  (").append(android.os.SystemClock.uptimeMillis() - started).append(" ms)\n"); }
                // Short: animating content (progress indicators) never idles; expect: and reject: poll.
                try { ua.waitForIdle(100, 300); } catch (Exception ignored) {}
            }
        } catch (AssertionError t) {
            out.append("FAILED ").append(t.getMessage()).append('\n');
            dumpTo(ua);
        } catch (Throwable t) {
            out.append("error: ").append(t).append('\n');
        }
        out.append("events:\n").append(events);
        Bundle b = new Bundle();
        b.putString(Instrumentation.REPORT_KEY_STREAMRESULT, out.toString());
        finish(0, b);
    }

    private void dumpTo(UiAutomation ua) {
        AccessibilityNodeInfo root = ua.getRootInActiveWindow();
        out.append("> dump (at failure)\n");
        verbose = false;
        dump(root, 0);
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
        if (verb.equals("back")) { out.append("  back -> ").append(ua.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)).append('\n'); return; }
        if (verb.equals("shot")) {
            // shot:<name>: a screenshot into this package's external files (tools/a11y-probe pulls them).
            android.graphics.Bitmap bitmap = ua.takeScreenshot();
            if (bitmap == null) throw new IllegalStateException("screenshot failed");
            java.io.File file = new java.io.File(getContext().getExternalFilesDir(null), arg + ".png");
            try (java.io.FileOutputStream f = new java.io.FileOutputStream(file)) { bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, f); }
            out.append("  shot -> ").append(file).append('\n');
            return;
        }
        if (verb.equals("expect") || verb.equals("reject")) {
            // Waits (up to 3 s) for a visible node with that text or description to appear / go away.
            boolean want = verb.equals("expect"), seen = !want;
            for (long end = android.os.SystemClock.uptimeMillis() + 3000; ; Thread.sleep(50)) {
                AccessibilityNodeInfo r = ua.getRootInActiveWindow();
                AccessibilityNodeInfo found = r == null ? null : findByText(r, arg);
                seen = found != null && found.isVisibleToUser();
                if (seen == want || android.os.SystemClock.uptimeMillis() > end) break;
            }
            if (seen != want) throw new AssertionError((want ? "missing: " : "unexpected: ") + arg);
            out.append("  ").append(verb).append(" -> true\n");
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
        if ((verb.equals("press") && arg.contains("=")) || verb.equals("swipeon") || verb.equals("settext") || verb.equals("setprogress") || verb.equals("action")) { find = arg.substring(0, arg.indexOf('=')); value = arg.substring(arg.indexOf('=') + 1); }
        AccessibilityNodeInfo n = findByText(root, find);
        // The tree changes under us (a popup closing, a recomposition): wait for the node as
        // expect: does before giving up (reach: then scrolls for it).
        for (long end = android.os.SystemClock.uptimeMillis() + (verb.equals("reach") ? 500 : 3000);
             n == null && android.os.SystemClock.uptimeMillis() < end; ) {
            Thread.sleep(50);
            AccessibilityNodeInfo r = ua.getRootInActiveWindow();
            if (r != null) { root = r; n = findByText(root, find); }
        }
        if (n == null && verb.equals("reach")) {
            // Scrolls the first scrollable forward until the node exists.
            java.util.List<AccessibilityNodeInfo> list = new java.util.ArrayList<>();
            scrollables(root, list);
            for (int i = 0; i < 40 && n == null && !list.isEmpty() && list.get(0).performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD); i++) {
                try { ua.waitForIdle(100, 1000); } catch (Exception ignored) {}
                root = ua.getRootInActiveWindow();
                n = findByText(root, find);
            }
        }
        if (n == null) throw new AssertionError("not found: " + find);
        boolean ok;
        switch (verb) {
            case "reach": {
                // Into the upper half of the screen, so popups below it have room.
                ok = n.performAction(android.R.id.accessibilityActionShowOnScreen);
                try { ua.waitForIdle(100, 1000); } catch (Exception ignored) {}
                Rect r = new Rect(), screen = new Rect();
                root.getBoundsInScreen(screen);
                int lastY = Integer.MIN_VALUE;
                for (int attempt = 0; attempt < 3; attempt++) {
                    n.refresh();
                    n.getBoundsInScreen(r);
                    // Done when in the upper half, or when the content no longer moves (its end).
                    if (r.centerY() <= screen.centerY() || r.centerY() == lastY) break;
                    lastY = r.centerY();
                    long down = android.os.SystemClock.uptimeMillis();
                    int x = screen.centerX(), from = r.centerY(), to = screen.top + screen.height() / 4;
                    // A slow drag (no fling), long enough to pass touch slop.
                    touch(ua, down, down, android.view.MotionEvent.ACTION_DOWN, x, from);
                    for (int i = 1; i <= 30; i++) {
                        android.os.SystemClock.sleep(16);
                        touch(ua, down, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_MOVE, x, from + (to - from) * i / 30f);
                    }
                    android.os.SystemClock.sleep(200);
                    touch(ua, down, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_UP, x, to);
                    try { ua.waitForIdle(100, 300); } catch (Exception ignored) {}
                }
                break;
            }
            case "swipeon": {
                // swipeon:<label>=dx,dy,millis: a drag from the node's centre by (dx, dy) dp,
                // released where it ends (scrolls what is under it, through nested scroll).
                String[] p = value.split(",");
                if (p.length != 3) throw new IllegalArgumentException("swipeon:label=dx,dy,millis");
                float density = getContext().getResources().getDisplayMetrics().density;
                Rect r = new Rect();
                n.getBoundsInScreen(r);
                float x1 = r.exactCenterX(), y1 = r.exactCenterY();
                float x2 = x1 + Float.parseFloat(p[0]) * density, y2 = y1 + Float.parseFloat(p[1]) * density;
                long duration = Long.parseLong(p[2]);
                long down = android.os.SystemClock.uptimeMillis();
                ok = touch(ua, down, down, android.view.MotionEvent.ACTION_DOWN, x1, y1);
                for (long elapsed = 16; elapsed < duration; elapsed += 16) {
                    android.os.SystemClock.sleep(Math.max(0, down + elapsed - android.os.SystemClock.uptimeMillis()));
                    float f = (float) elapsed / duration;
                    ok &= touch(ua, down, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_MOVE, x1 + (x2 - x1) * f, y1 + (y2 - y1) * f);
                }
                android.os.SystemClock.sleep(Math.max(0, down + duration - android.os.SystemClock.uptimeMillis()));
                ok &= touch(ua, down, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_UP, x2, y2);
                break;
            }
            case "tap": case "press": {
                // A real touch at the node's centre (press holds it for `=ms`, default 800).
                Rect r = new Rect();
                n.getBoundsInScreen(r);
                long hold = verb.equals("tap") ? 50 : value == null ? 800 : Long.parseLong(value);
                long down = android.os.SystemClock.uptimeMillis();
                ok = touch(ua, down, down, android.view.MotionEvent.ACTION_DOWN, r.exactCenterX(), r.exactCenterY());
                android.os.SystemClock.sleep(hold);
                ok &= touch(ua, down, android.os.SystemClock.uptimeMillis(), android.view.MotionEvent.ACTION_UP, r.exactCenterX(), r.exactCenterY());
                break;
            }
            // Like TalkBack on the unmerged tree: the nearest clickable node at or above the match.
            case "click": ok = clickableAt(n, false).performAction(AccessibilityNodeInfo.ACTION_CLICK); break;
            case "longclick": ok = clickableAt(n, true).performAction(AccessibilityNodeInfo.ACTION_LONG_CLICK); break;
            case "focus": ok = focusableAt(n).performAction(AccessibilityNodeInfo.ACTION_FOCUS); break;
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
                // A field found by its label or placeholder: the editable node that holds it.
                AccessibilityNodeInfo field = n;
                while (field != null && !field.isEditable()) field = field.getParent();
                ok = (field != null ? field : n).performAction(AccessibilityNodeInfo.ACTION_SET_TEXT, a);
                break;
            }
            default: out.append("unknown verb ").append(verb).append('\n'); return;
        }
        out.append("  ").append(verb).append(" -> ").append(ok).append('\n');
    }

    private static boolean touch(UiAutomation ua, long down, long time, int action, float x, float y) {
        // A finger: the short obtain() leaves the tool type unknown, which touch-only gestures
        // (a tooltip's long press, for one) ignore, as Compose's do.
        android.view.MotionEvent.PointerProperties props = new android.view.MotionEvent.PointerProperties();
        props.id = 0;
        props.toolType = android.view.MotionEvent.TOOL_TYPE_FINGER;
        android.view.MotionEvent.PointerCoords coords = new android.view.MotionEvent.PointerCoords();
        coords.x = x;
        coords.y = y;
        coords.pressure = 1;
        coords.size = 1;
        android.view.MotionEvent event = android.view.MotionEvent.obtain(down, time, action, 1,
            new android.view.MotionEvent.PointerProperties[] { props }, new android.view.MotionEvent.PointerCoords[] { coords },
            0, 0, 1, 1, 0, 0, android.view.InputDevice.SOURCE_TOUCHSCREEN, 0);
        // Waiting for each move to be dispatched paces a drag at hundreds of ms per event.
        try { return ua.injectInputEvent(event, action != android.view.MotionEvent.ACTION_MOVE); }
        finally { event.recycle(); }
    }

    private static AccessibilityNodeInfo clickableAt(AccessibilityNodeInfo n, boolean longClick) {
        for (AccessibilityNodeInfo at = n; at != null; at = at.getParent())
            if (longClick ? at.isLongClickable() : at.isClickable()) return at;
        return n;
    }

    private static AccessibilityNodeInfo focusableAt(AccessibilityNodeInfo n) {
        for (AccessibilityNodeInfo at = n; at != null; at = at.getParent()) if (at.isFocusable()) return at;
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
