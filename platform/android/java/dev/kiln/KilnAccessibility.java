package dev.kiln;

import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.SpannableString;
import android.text.Spanned;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Accessibility for the kiln tree (spec 01 req. 5-10): the native delegate (a port of Compose's
 * AndroidComposeViewAccessibilityDelegateCompat) decides every virtual view's node info, performs
 * actions and makes events; this turns them into AccessibilityNodeInfo / AccessibilityEvent, maps
 * root pixels to the screen, and paces the change checks like Compose (semantics changes on the
 * next loop, layout and scroll changes at most every 100 ms).
 */
final class KilnAccessibility extends AccessibilityNodeProvider {
    private static final int INVALID = Integer.MIN_VALUE;
    private static final long SEND_RECURRING_EVENTS_INTERVAL_MILLIS = 100;

    private final KilnView view;
    private final AccessibilityManager manager;
    private boolean enabled;
    // Content capture (API 29+): the view's session between the host's start and stop.
    private android.view.contentcapture.ContentCaptureSession capture;
    private boolean checkPosted, boundsPosted;
    private long lastBoundsFlush;

    KilnAccessibility(KilnView view) {
        this.view = view;
        manager = view.getContext().getSystemService(AccessibilityManager.class);
    }

    /** A service started or stopped listening (or touch exploration toggled). */
    void updateEnabled() {
        boolean on = manager != null && manager.isEnabled();
        if (on == enabled) return;
        enabled = on;
        KilnNative.a11ySetEnabled(view.windowId, on, Locale.getDefault().toLanguageTag());
        if (on) onChange(false);
    }

    boolean isEnabled() { return enabled; }

    boolean isTouchExplorationEnabled() { return enabled && manager.isTouchExplorationEnabled(); }

    private boolean listening() { return enabled || capture != null; }

    /** Native: semantics (false) or layout (true) changed. */
    void onChange(boolean layout) {
        if (!listening()) return;
        postCheck();
        if (layout) scheduleBoundsFlush();
    }

    /** semanticsChangeChecker / contentCaptureChangeChecker on the next loop. */
    private void postCheck() {
        if (checkPosted) return;
        checkPosted = true;
        view.post(() -> {
            checkPosted = false;
            if (!listening() || !view.isAttachedToWindow()) return;
            if (enabled) {
                KilnNative.a11yCheckSemantics(view.windowId);
                sendPendingEvents();
            }
            if (capture != null) {
                KilnNative.ccCheck(view.windowId);
                sendCaptureEvents();
            }
            scheduleBoundsFlush();
        });
    }

    /** The host started (onStart): a content capture session sees the whole tree appear. */
    void onHostStart() {
        if (android.os.Build.VERSION.SDK_INT < 29) return;
        capture = view.getContentCaptureSession();
        if (capture == null) return;
        KilnNative.ccStart(view.windowId);
        sendCaptureEvents();
    }

    /** The host stopped (onStop): everything disappears and the session ends. */
    void onHostStop() {
        if (capture == null) return;
        KilnNative.ccStop(view.windowId);
        sendCaptureEvents();
        capture = null;
    }

    /** The buffered content capture events, applied to the session. */
    private void sendCaptureEvents() {
        int[] d = KilnNative.ccTakeEvents(view.windowId);
        if (d.length == 0 || capture == null) return;
        String[] s = KilnNative.a11yStrings(view.windowId);
        android.view.autofill.AutofillId root = view.getAutofillId();
        if (root == null) return;
        for (int at = 0; at + 16 <= d.length; at += 16) {
            final int kind = d[at], id = d[at + 1], parent = d[at + 2];
            switch (kind) {
                case 0: {
                    android.view.autofill.AutofillId parentId = parent == INVALID ? root : capture.newAutofillId(root, parent);
                    android.view.ViewStructure st = capture.newVirtualViewStructure(parentId, id);
                    Bundle extras = st.getExtras();
                    if (extras != null) {
                        extras.putLong("android.view.contentcapture.EventTimestamp", ((long) d[at + 10] << 32) | (d[at + 11] & 0xffffffffL));
                        extras.putInt("android.view.ViewStructure.extra.EXTRA_VIEW_NODE_INDEX", d[at + 3]);
                    }
                    String cls = str(s, d[at + 12]), text = str(s, d[at + 13]), cd = str(s, d[at + 14]), tag = str(s, d[at + 15]);
                    if (tag != null) st.setId(id, null, null, tag);
                    if (cls != null) st.setClassName(cls);
                    if (text != null) st.setText(text);
                    if (cd != null) st.setContentDescription(cd);
                    if (d[at + 4] != 0) st.setTextStyle(Float.intBitsToFloat(d[at + 5]), 0, 0, 0);
                    st.setDimens(d[at + 6], d[at + 7], 0, 0, d[at + 8], d[at + 9]);
                    capture.notifyViewAppeared(st);
                    break;
                }
                case 1: capture.notifyViewDisappeared(capture.newAutofillId(root, id)); break;
                case 2: capture.notifyViewTextChanged(capture.newAutofillId(root, id), str(s, d[at + 13])); break;
                default:
                    // flush (ContentCaptureSessionCompat.flush before API 36).
                    if (android.os.Build.VERSION.SDK_INT >= 36) capture.flush();
                    else capture.notifyViewsDisappeared(root, new long[] { Long.MIN_VALUE });
                    break;
            }
        }
    }

    // --- translation (API 31+) ----------------------------------------------------------------

    void onCreateTranslationRequests(long[] virtualIds, java.util.function.Consumer<android.view.translation.ViewTranslationRequest> collector) {
        int[] ids = new int[virtualIds.length];
        for (int i = 0; i < ids.length; i++) ids[i] = (int) virtualIds[i];
        String[] texts = KilnNative.ccTranslationRequests(view.windowId, ids);
        for (int i = 0; i < ids.length && i < texts.length; i++) {
            if (texts[i] == null) continue;
            android.view.translation.ViewTranslationRequest.Builder b =
                    new android.view.translation.ViewTranslationRequest.Builder(view.getAutofillId(), virtualIds[i]);
            b.setValue(android.view.translation.ViewTranslationRequest.ID_TEXT,
                    android.view.translation.TranslationRequestValue.forText(texts[i]));
            collector.accept(b.build());
        }
    }

    void onTranslationResponses(android.util.LongSparseArray<android.view.translation.ViewTranslationResponse> response) {
        Runnable apply = () -> {
            int n = response.size();
            int[] ids = new int[n];
            String[] texts = new String[n];
            for (int i = 0; i < n; i++) {
                ids[i] = (int) response.keyAt(i);
                android.view.translation.ViewTranslationResponse r = response.valueAt(i);
                android.view.translation.TranslationResponseValue v = r == null ? null : r.getValue(android.view.translation.ViewTranslationRequest.ID_TEXT);
                CharSequence t = v == null ? null : v.getText();
                texts[i] = t == null ? null : t.toString();
            }
            KilnNative.ccTranslationResponses(view.windowId, ids, texts);
        };
        if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) apply.run(); else view.post(apply);
    }

    /** ViewTranslationCallback: show (0), hide (1) or clear (2) the translated text. */
    final android.view.translation.ViewTranslationCallback translationCallback = new android.view.translation.ViewTranslationCallback() {
        @Override public boolean onShowTranslation(View v) { KilnNative.ccTranslation(view.windowId, 0); return true; }
        @Override public boolean onHideTranslation(View v) { KilnNative.ccTranslation(view.windowId, 1); return true; }
        @Override public boolean onClearTranslation(View v) { KilnNative.ccTranslation(view.windowId, 2); return true; }
    };

    /** boundsUpdatesEventLoop: subtree changes and scroll events, then 100 ms of quiet. */
    private void scheduleBoundsFlush() {
        if (boundsPosted) return;
        boundsPosted = true;
        long wait = Math.max(0, lastBoundsFlush + SEND_RECURRING_EVENTS_INTERVAL_MILLIS - SystemClock.uptimeMillis());
        view.postDelayed(() -> {
            boundsPosted = false;
            lastBoundsFlush = SystemClock.uptimeMillis();
            if (!listening() || !view.isAttachedToWindow()) return;
            boolean more = false;
            if (enabled) {
                more = KilnNative.a11yFlushBounds(view.windowId);
                sendPendingEvents();
            }
            if (capture != null) {
                // notifyContentCaptureChanges, then the change checker.
                KilnNative.ccFlush(view.windowId);
                sendCaptureEvents();
                postCheck();
            }
            if (more) scheduleBoundsFlush();
        }, wait);
    }

    private Rect toScreen(int l, int t, int r, int b) {
        int[] loc = new int[2];
        view.getLocationOnScreen(loc);
        return new Rect(loc[0] + l, loc[1] + t, loc[0] + r, loc[1] + b);
    }

    private static String str(String[] s, int i) { return i < 0 || i >= s.length ? null : s[i]; }

    /** A standard action by its id, a android.R.id one, or a custom one (with its label). */
    private static AccessibilityNodeInfo.AccessibilityAction action(int id, String label) {
        return new AccessibilityNodeInfo.AccessibilityAction(id, label);
    }

    /** toAccessibilitySpannableString: URL, clickable and TTS spans on the text. */
    private CharSequence withSpans(final int id, String text, int[] d, int at, int count, String[] s) {
        if (count == 0) return text;
        SpannableString out = new SpannableString(text);
        int clickable = 0;
        for (int i = 0; i < count; i++, at += 4) {
            final int kind = d[at], start = Math.max(0, d[at + 1]), end = Math.min(text.length(), d[at + 2]);
            final String value = str(s, d[at + 3]);
            final int spanIndex = i;
            if (start >= end) continue;
            Object span;
            if (kind == 0) span = new android.text.style.URLSpan(value);
            else if (kind == 1) span = new android.text.style.ClickableSpan() {
                @Override public void onClick(View widget) { KilnNative.a11ySpanClick(view.windowId, id, spanIndex); }
            };
            else span = new android.text.style.TtsSpan.VerbatimBuilder(value == null ? "" : value).build();
            out.setSpan(span, start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return out;
    }

    @Override
    public AccessibilityNodeInfo createAccessibilityNodeInfo(int virtualViewId) {
        final int id = virtualViewId == View.NO_ID ? -1 : virtualViewId;
        int[] d = KilnNative.a11yNodeInfo(view.windowId, id);
        if (d.length == 0 || d[0] == 0) {
            // emptyNodeInfoOrNull.
            return enabled ? null : AccessibilityNodeInfo.obtain();
        }
        String[] s = KilnNative.a11yStrings(view.windowId);
        AccessibilityNodeInfo info;
        if (id == -1) {
            info = AccessibilityNodeInfo.obtain(view);
            view.onInitializeAccessibilityNodeInfo(info);
        } else {
            info = AccessibilityNodeInfo.obtain(view, id);
            info.setPackageName(view.getContext().getPackageName());
            info.setSource(view, id);
            if (d[1] == -1 || d[1] == INVALID) info.setParent(view); else info.setParent(view, d[1]);
            info.setBoundsInScreen(toScreen(d[2], d[3], d[4], d[5]));
        }
        final int f = d[6];
        if (id != -1) {
            info.setClassName(str(s, 0));
            info.setImportantForAccessibility((f & 1) != 0);
            info.setCheckable((f & 4) != 0);
            if ((f & 262144) != 0) info.setChecked((f & 524288) != 0);
            info.setSelected((f & 8) != 0);
            info.setHeading((f & 16) != 0);
            info.setTextEntryKey((f & 32) != 0);
            info.setPassword((f & 64) != 0);
            info.setEditable((f & 128) != 0);
            info.setEnabled((f & 256) != 0);
            info.setFocusable((f & 512) != 0);
            info.setFocused((f & 1024) != 0);
            info.setVisibleToUser((f & 2048) != 0);
            info.setClickable((f & 4096) != 0);
            info.setLongClickable((f & 8192) != 0);
            info.setScrollable((f & 16384) != 0);
            info.setScreenReaderFocusable((f & 32768) != 0);
            info.setContentInvalid((f & 65536) != 0);
            if (android.os.Build.VERSION.SDK_INT >= 34) info.setAccessibilityDataSensitive((f & 131072) != 0);
            if (d[7] >= 0) info.setDrawingOrder(d[7]);
            info.setMaxTextLength(d[8]);
            info.setLiveRegion(d[9]);
            info.setMovementGranularities(d[10]);
            if (d[11] != INVALID) info.setTextSelection(d[11], d[12]);
            if (d[13] != INVALID) info.setTraversalBefore(view, d[13]);
            if (d[14] != INVALID) info.setTraversalAfter(view, d[14]);
            if (d[15] != INVALID)
                info.setCollectionInfo(AccessibilityNodeInfo.CollectionInfo.obtain(d[15], d[16], false,
                        AccessibilityNodeInfo.CollectionInfo.SELECTION_MODE_NONE));
            if (d[17] != INVALID)
                info.setCollectionItemInfo(AccessibilityNodeInfo.CollectionItemInfo.obtain(d[17], d[18], d[19], d[20], false, d[21] != 0));
            if (d[22] != 0)
                info.setRangeInfo(AccessibilityNodeInfo.RangeInfo.obtain(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT,
                        Float.intBitsToFloat(d[23]), Float.intBitsToFloat(d[24]), Float.intBitsToFloat(d[25])));
            String role = str(s, 1);
            if (role != null) info.getExtras().putCharSequence("AccessibilityNodeInfo.roleDescription", role);
            String contentDescription = str(s, 3), state = str(s, 4), hint = str(s, 5), error = str(s, 6), pane = str(s, 7), resId = str(s, 8);
            if (contentDescription != null) info.setContentDescription(contentDescription);
            info.setStateDescription(state);
            if (hint != null) info.setHintText(hint);
            if (error != null) info.setError(error);
            info.setPaneTitle(pane);
            if (resId != null) info.setViewIdResourceName(resId);
        }
        int at = 26;
        int children = d[at++];
        for (int i = 0; i < children; i++) info.addChild(view, d[at++]);
        int actions = d[at++];
        for (int i = 0; i < actions; i++, at += 2) {
            if (id == -1) continue;   // the host view keeps its own actions
            info.addAction(action(d[at], str(s, d[at + 1])));
        }
        int spans = d[at++];
        if (id != -1) {
            String text = str(s, 2);
            if (text != null) info.setText(withSpans(id, text, d, at, spans, s));
        }
        at += spans * 4;
        int keys = d[at++];
        if (id != -1 && keys > 0) {
            List<String> extra = new ArrayList<>();
            for (int i = 0; i < keys; i++) extra.add(str(s, d[at + i]));
            info.setAvailableExtraData(extra);
        }
        return info;
    }

    @Override
    public void addExtraDataToAccessibilityNodeInfo(int virtualViewId, AccessibilityNodeInfo info, String key, Bundle args) {
        int start = Integer.MIN_VALUE, length = Integer.MIN_VALUE;
        if (args != null) {
            start = args.getInt(AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_START_INDEX, -1);
            length = args.getInt(AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_LENGTH, -1);
        }
        int[] d = KilnNative.a11yExtraData(view.windowId, virtualViewId == View.NO_ID ? -1 : virtualViewId, key, start, length);
        if (d.length == 0) return;
        Bundle extras = info.getExtras();
        int[] loc = new int[2];
        view.getLocationOnScreen(loc);
        switch (d[0]) {
            case 1: extras.putInt(key, d[1]); break;
            case 2: { String[] s = KilnNative.a11yStrings(view.windowId); if (s.length > 0) extras.putCharSequence(key, s[0]); break; }
            case 3: {
                int n = d[1];
                RectF[] rects = new RectF[n];
                for (int i = 0; i < n; i++) {
                    int b = 2 + i * 5;
                    if (d[b] == 0) continue;
                    rects[i] = new RectF(loc[0] + Float.intBitsToFloat(d[b + 1]), loc[1] + Float.intBitsToFloat(d[b + 2]),
                            loc[0] + Float.intBitsToFloat(d[b + 3]), loc[1] + Float.intBitsToFloat(d[b + 4]));
                }
                extras.putParcelableArray(key, rects);
                break;
            }
            case 4: {
                // Shape keys: the type, the rect relative to the node's bounds, corners, region.
                Rect bounds = new Rect();
                info.getBoundsInScreen(bounds);
                if (d[1] != INVALID) extras.putInt("androidx.compose.ui.semantics.shapeType", d[1]);
                if (d[2] != 0)
                    extras.putParcelable("androidx.compose.ui.semantics.shapeRect",
                            new Rect(loc[0] + d[3] - bounds.left, loc[1] + d[4] - bounds.top, loc[0] + d[5] - bounds.left, loc[1] + d[6] - bounds.top));
                int at = 7, corners = d[at++];
                if (corners > 0) {
                    float[] c = new float[corners];
                    for (int i = 0; i < corners; i++) c[i] = Float.intBitsToFloat(d[at + i]);
                    extras.putFloatArray("androidx.compose.ui.semantics.shapeCorners", c);
                }
                at += corners;
                int regions = d[at++];
                if (regions > 0) {
                    android.graphics.Region region = new android.graphics.Region();
                    for (int i = 0; i < regions; i++, at += 4)
                        region.union(new Rect(loc[0] + d[at] - bounds.left, loc[1] + d[at + 1] - bounds.top, loc[0] + d[at + 2] - bounds.left, loc[1] + d[at + 3] - bounds.top));
                    extras.putParcelable("androidx.compose.ui.semantics.shapeRegion", region);
                }
                break;
            }
            default: break;
        }
    }

    @Override
    public AccessibilityNodeInfo findFocus(int focus) {
        int id = KilnNative.a11yFindFocus(view.windowId, focus == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY);
        if (id == INVALID) return null;
        return createAccessibilityNodeInfo(id);
    }

    @Override
    public boolean performAction(int virtualViewId, int action, Bundle args) {
        if (virtualViewId == View.NO_ID) return view.performAccessibilityAction(action, args);
        int granularity = Integer.MIN_VALUE, selStart = Integer.MIN_VALUE, selEnd = Integer.MIN_VALUE;
        boolean extend = false, hasProgress = false;
        float progress = 0;
        String text = null;
        if (args != null) {
            if (args.containsKey(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT))
                granularity = args.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT);
            extend = args.getBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN);
            selStart = args.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, -1);
            selEnd = args.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, -1);
            CharSequence t = args.getCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE);
            if (t != null) text = t.toString();
            if (args.containsKey(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE)) {
                hasProgress = true;
                progress = args.getFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE);
            }
        } else if (action == AccessibilityNodeInfo.ACTION_SET_SELECTION) {
            selStart = -1;
            selEnd = -1;
        }
        if (action == AccessibilityNodeInfo.ACTION_SET_TEXT && text == null) text = "";
        boolean ok = KilnNative.a11yPerformAction(view.windowId, virtualViewId, action, granularity, extend, selStart, selEnd, text, hasProgress, progress);
        sendPendingEvents();
        return ok;
    }

    /** Explore by touch: the node under the finger (dispatchHoverEvent). */
    boolean onHover(int action, float x, float y) {
        boolean handled = KilnNative.a11yHover(view.windowId, action, x, y);
        sendPendingEvents();
        return handled;
    }

    boolean canScroll(boolean vertical, int direction, float x, float y) {
        return KilnNative.a11yCanScroll(view.windowId, vertical, direction, x, y);
    }

    /** The events the delegate made (focus, clicks, property changes, scrolls, panes). */
    void sendPendingEvents() {
        int[] d = KilnNative.a11yTakeEvents(view.windowId);
        if (d.length == 0) return;
        String[] s = KilnNative.a11yStrings(view.windowId);
        int at = 0;
        while (at < d.length) {
            AccessibilityEvent e = AccessibilityEvent.obtain(d[at]);
            final int id = d[at + 1];
            e.setEnabled(true);
            e.setPackageName(view.getContext().getPackageName());
            if (id == -1) e.setSource(view); else e.setSource(view, id);
            if (d[at + 2] != INVALID) e.setContentChangeTypes(d[at + 2]);
            e.setPassword((d[at + 3] & 1) != 0);
            if (android.os.Build.VERSION.SDK_INT >= 34) e.setAccessibilityDataSensitive((d[at + 3] & 2) != 0);
            if (d[at + 4] != INVALID) e.setFromIndex(d[at + 4]);
            if (d[at + 5] != INVALID) e.setToIndex(d[at + 5]);
            if (d[at + 6] != INVALID) e.setItemCount(d[at + 6]);
            if (d[at + 7] != INVALID) e.setAddedCount(d[at + 7]);
            if (d[at + 8] != INVALID) e.setRemovedCount(d[at + 8]);
            if (d[at + 9] != INVALID) e.setScrollX(d[at + 9]);
            if (d[at + 10] != INVALID) e.setMaxScrollX(d[at + 10]);
            if (d[at + 11] != INVALID) e.setScrollY(d[at + 11]);
            if (d[at + 12] != INVALID) e.setMaxScrollY(d[at + 12]);
            if (e.getEventType() == AccessibilityEvent.TYPE_VIEW_SCROLLED) {
                e.setScrollDeltaX(d[at + 13]);
                e.setScrollDeltaY(d[at + 14]);
            }
            if (d[at + 15] != INVALID) e.setAction(d[at + 15]);
            if (d[at + 16] != INVALID) e.setMovementGranularity(d[at + 16]);
            e.setClassName(str(s, d[at + 17]));
            String cd = str(s, d[at + 18]);
            if (cd != null) e.setContentDescription(cd);
            String before = str(s, d[at + 19]);
            if (before != null) e.setBeforeText(before);
            int texts = d[at + 20];
            at += 21;
            for (int i = 0; i < texts; i++) e.getText().add(str(s, d[at++]));
            // The manager updates its state before our state-change listener runs. A bounds
            // flush queued while a service was connected can therefore see stale `enabled`.
            android.view.ViewParent parent = view.getParent();
            if (!enabled || manager == null || !manager.isEnabled() || parent == null) {
                e.recycle();
                continue;
            }
            try {
                parent.requestSendAccessibilityEvent(view, e);
            } catch (IllegalStateException ex) {
                // The service can also disconnect between the check and the platform send.
                // Do not hide unrelated dispatch failures while accessibility is still on.
                if (manager.isEnabled()) throw ex;
                e.recycle();
            }
        }
    }
}
