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
        if (verb.equals("dump")) { dump(root, 0); return; }
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
        if (verb.equals("settext")) { find = arg.substring(0, arg.indexOf('=')); value = arg.substring(arg.indexOf('=') + 1); }
        AccessibilityNodeInfo n = findByText(root, find);
        if (n == null) { out.append("not found: ").append(find).append('\n'); return; }
        boolean ok;
        switch (verb) {
            case "click": ok = n.performAction(AccessibilityNodeInfo.ACTION_CLICK); break;
            case "longclick": ok = n.performAction(AccessibilityNodeInfo.ACTION_LONG_CLICK); break;
            case "focus": ok = n.performAction(AccessibilityNodeInfo.ACTION_FOCUS); break;
            case "a11yfocus": ok = n.performAction(AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS); break;
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
        if (n.getCollectionInfo() != null) out.append(" collection=").append(n.getCollectionInfo().getRowCount()).append('x').append(n.getCollectionInfo().getColumnCount());
        out.append('\n');
        for (int i = 0; i < n.getChildCount(); i++) dump(n.getChild(i), depth + 1);
    }
}
