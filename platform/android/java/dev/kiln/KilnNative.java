package dev.kiln;

import android.view.Surface;

/** JNI entry points into libkiln (statically linked into the app's library). */
final class KilnNative {
    private KilnNative() {}

    static native void create(KilnView view, KilnText text, android.content.res.AssetManager assets, float density, float fontScale, byte[] savedState);
    static native byte[] saveState();
    static native void setVisible(boolean visible);
    static native void destroy();
    static native void setDensity(float density, float fontScale);
    /** Configuration.fontWeightAdjustment (the bold text setting). */
    static native void setFontWeightAdjustment(int adjustment);
    /** Configuration fields (see KilnView.pushConfiguration) and the locale list as BCP 47 tags. */
    static native void setConfiguration(int[] values, float fontScale, String[] locales);
    /** DateFormat.is24HourFormat. */
    static native void set24HourFormat(boolean is24Hour);
    /** The window's FLAG_SECURE. */
    static native void flagSecureChanged(boolean secure);
    /** Downloadable fonts arrived: lay text out again. */
    static native void fontsChanged();
    static native void textToolbarItem(int win, int item);
    static native void textToolbarDestroyed(int win);
    /** A suggestSelection / classifyText request's result (handle 0: no classification). */
    static native void classifierResult(int request, boolean hasRange, int start, int end, int handle, String[] actions, boolean legacyAssist);
    static native void setAnimationScale(float scale);
    /** The show-passwords setting: bit 1 touch input, bit 2 physical keyboards. */
    static native void setPasswordVisibility(int bits);
    static native void setAccessibilityServices(boolean enabled, boolean touchExploration, boolean switchAccess, boolean voiceAccess);
    static native void setLayoutDirection(boolean rtl);
    static native void touchEvent(int win, int action, int actionIndex, long eventTime, int[] ids, float[] xy,
                                  int[] toolTypes, int buttons, int metaState, int classification, float hscroll, float vscroll,
                                  long[] historyTimes, float[] historyXy, float[] pan);
    static native boolean keyEvent(int win, int action, int keyCode, int metaState, int unicode, int repeatCount, long eventTime, int source);
    static native boolean keyEventPreIme(int win, int action, int keyCode, int metaState, int unicode, int repeatCount, long eventTime, int source);
    static native boolean rotary(int win, float verticalPixels, float horizontalPixels, long eventTime, int deviceId);
    static native boolean indirectPointer(int win, int action, int actionIndex, long eventTime, int[] ids, float[] xy,
                                          float[] pressures, long[] historyTimes, float[] historyXy, int axis);
    static native void indirectFling(int win, boolean next);
    static native float[] focusedRect(int win);
    static native boolean hasFocusableContent(int win);
    static native float[] focusSearch(int win, int direction, float left, float top, float right, float bottom);
    static native boolean isBetterFocusCandidate(int direction, float[] proposed, float[] current, float[] focused);
    static native boolean hostRequestFocus(int win, int direction, boolean hasRect, float left, float top, float right,
                                           float bottom, boolean hostHasFocus);
    static native void hostFocusChanged(int win, boolean focused);
    static native void windowFocusChanged(int win, boolean focused);
    static native void setWindowInsets(int win, int[] data);
    static native void insetsControlReady(int win, int[] hiddenShownCurrent);
    static native void insetsControlEnded(int win);
    static native void setInsetsAnimation(int win, int type, boolean running, int[] sourceTarget, float fraction, float alpha, long durationMillis);

    // InputConnection -> the active native text input session.
    static native boolean imeBeginBatchEdit(int win);
    static native boolean imeEndBatchEdit(int win);
    static native void imeCommitText(int win, String text, int newCursorPosition, boolean suggestionSelected);
    static native void imeSetComposingText(int win, String text, int newCursorPosition, int[] spans, String[] families, boolean suggestionSelected);
    static native void imeRequestCursorUpdates(int win, int mode);
    static native int imePerformHandwritingGesture(int win, float[] gesture, String textToInsert, String fallbackText);
    static native boolean imePreviewHandwritingGesture(int win, float[] gesture);
    static native void imeCancelHandwritingPreview(int win);
    static native boolean imeCommitContent(int win, String uri, String[] mimeTypes, String label, String linkUri, int permission);
    static native void imeSetComposingRegion(int win, int start, int end);
    static native void imeFinishComposingText(int win);
    static native void imeDeleteSurroundingText(int win, int before, int after, boolean codePoints);
    static native void imeSetSelection(int win, int start, int end);
    static native void imePerformEditorAction(int win, int action);
    static native String imeGetText(int win);
    static native int[] imeGetSelection(int win);

    // Back: started/progressed gesture events, cancel, completion.
    static native void backEvent(boolean started, float x, float y, float progress, int edge, long frameTimeMillis);
    static native void backCancelled();
    static native void backInvoked();

    // Semantics for KilnAccessibility.
    static native void a11ySetEnabled(int win, boolean enabled, String localeTag);
    static native int[] a11yNodeInfo(int win, int id);
    static native String[] a11yStrings(int win);
    static native int[] a11yTakeEvents(int win);
    static native boolean a11yPerformAction(int win, int id, int action, int granularity, boolean extend, int selStart, int selEnd,
                                            String text, boolean hasProgress, float progress);
    static native int[] a11yExtraData(int win, int id, String key, int start, int length);
    static native void a11ySpanClick(int win, int id, int index);
    static native boolean a11yHover(int win, int action, float x, float y);
    static native int a11yFindFocus(int win, boolean accessibility);
    static native void a11yCheckSemantics(int win);
    /** Autofill's virtual structure: {int[] 11 per node, String[] 5 per node, long[] dates}. */
    static native Object[] autofillStructure(int win);
    /** One autofilled value; kind 0 text, 1 toggle, 2 list, 3 date. */
    static native void autofillPerform(int win, int id, int kind, String text, int intValue, long date);
    /** A pending credential request's credential (its data as KilnAutofill's bundle arrays). */
    static native void credentialResult(int win, int id, String type, Object[] data);
    static native void credentialError(int win, int id, String type, String message);
    /** OnDragListener.onDrag; items (3 per clip item) on a drop only; local: one of our drags. */
    /** Scroll capture: the candidate {id, l, t, r, b} or null; a session's start, strips and end. */
    static native int[] scrollCaptureSearch();
    static native void scrollCaptureStart(int id, int left, int top, int right, int bottom);
    static native void scrollCaptureImageRequest(int request, int left, int top, int right, int bottom);
    static native void scrollCaptureCancel(int request);
    static native void scrollCaptureEnd(int request);
    /** An activity result for a native launcher key (the intent as KilnActivityResults passes it). */
    static native void activityResult(String key, int resultCode, Object[] data);
    static native boolean dragEvent(int win, int action, float x, float y, String[] mimeTypes, String label, String[] items, boolean local, boolean result);
    static native boolean a11yFlushBounds(int win);
    static native boolean a11yCanScroll(int win, boolean vertical, int direction, float x, float y);
    // Content capture and translation.
    static native void ccStart(int win);
    static native void ccStop(int win);
    static native void ccCheck(int win);
    static native void ccFlush(int win);
    static native int[] ccTakeEvents(int win);
    static native String[] ccTranslationRequests(int win, int[] ids);
    static native void ccTranslationResponses(int win, int[] ids, String[] texts);
    static native void ccTranslation(int win, int mode);
    static native void setViewConfiguration(long longPressTimeout, long doubleTapTimeout, float touchSlop,
                                            float maxFling, float minFling, float handwritingSlop, float handwritingLineMargin);
    static native void surfaceCreated(int win, Surface surface);
    /** A dialog window asks to be dismissed (Back, Escape, a press outside its content). */
    static native void dialogDismissRequest(int win);
    /** A dialog's view measures its content (MeasureSpecs): {width, height, content height}. */
    static native int[] windowMeasure(int win, int widthSpec, int heightSpec);
    /** Where a dialog's content sits in its view: {left, top, right, bottom}. */
    static native int[] windowContentBounds(int win);
    static native void surfaceChanged(int win, int width, int height);
    static native void surfaceDestroyed(int win);
}
