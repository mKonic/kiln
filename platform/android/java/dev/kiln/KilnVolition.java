package dev.kiln;

import android.app.Activity;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.pm.ApplicationInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inspector.WindowInspector;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Volition's endpoint (github.com/mKonic/volition) in a debug build: the volition CLI and MCP
 * tools drive a kiln app by name.
 *
 * <pre>adb shell content call --uri content://&lt;package&gt;.volition --method ui</pre>
 *
 * kiln-pack --debug declares it; it refuses to answer when the app is not debuggable. The
 * answers come from each window's semantics (KilnNative.volition), read on the main thread.
 */
public final class KilnVolition extends ContentProvider {
    private static final String RESULT = "result";
    private static final long TIMEOUT_MS = 10_000;

    @Override public boolean onCreate() { return true; }

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        Bundle out = new Bundle();
        Context context = getContext();
        if (context == null) { out.putString(RESULT, "error: no context"); return out; }
        if ((context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            out.putString(RESULT, "error: this build does not answer - it is not debuggable");
            return out;
        }
        out.putString(RESULT, onMain(() -> answer(method, arg)));
        return out;
    }

    private interface Answer { String get(); }

    private static String onMain(Answer a) {
        if (Looper.myLooper() == Looper.getMainLooper()) return a.get();
        final String[] result = { null };
        final CountDownLatch done = new CountDownLatch(1);
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                result[0] = a.get();
            } catch (RuntimeException e) {
                result[0] = "error: " + e.getClass().getSimpleName() + ": " + e.getMessage();
            } finally {
                done.countDown();
            }
        });
        try {
            if (!done.await(TIMEOUT_MS, TimeUnit.MILLISECONDS)) return "error: the app did not answer in time";
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return "error: interrupted";
        }
        return result[0];
    }

    private static String answer(String method, String arg) {
        List<KilnView> views = new ArrayList<>();
        List<String> names = new ArrayList<>();
        // Bottom to top, as the window manager stacks them.
        for (View root : WindowInspector.getGlobalWindowViews()) {
            if (!root.isAttachedToWindow() || root.getWindowVisibility() != View.VISIBLE || !root.isShown()) continue;
            KilnView v = find(root);
            if (v == null) continue;
            views.add(v);
            names.add(nameOf(root));
        }
        if (views.isEmpty()) {
            // A provider call can start the process before the app has a window (or its library).
            if ("where".equals(method) || "state".equals(method)) return "{\"activity\":\"none\",\"ready\":false}";
            return "error: nothing on screen";
        }
        int[] ids = new int[views.size()];
        int[] origins = new int[views.size() * 2];
        int[] at = new int[2];
        String activity = "";
        for (int i = 0; i < views.size(); i++) {
            KilnView v = views.get(i);
            ids[i] = v.windowId;
            v.getLocationOnScreen(at);
            origins[2 * i] = at[0];
            origins[2 * i + 1] = at[1];
            Activity a = activityOf(v.getContext());
            if (a != null && v.windowId == 0) activity = a.getClass().getSimpleName();
        }
        return KilnNative.volition(ids, names.toArray(new String[0]), origins, activity, method, arg == null ? "" : arg);
    }

    private static KilnView find(View v) {
        if (v instanceof KilnView) return v.isShown() ? (KilnView) v : null;
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            for (int i = 0; i < g.getChildCount(); i++) {
                KilnView k = find(g.getChildAt(i));
                if (k != null) return k;
            }
        }
        return null;
    }

    private static Activity activityOf(Context c) {
        while (c instanceof ContextWrapper) {
            if (c instanceof Activity) return (Activity) c;
            c = ((ContextWrapper) c).getBaseContext();
        }
        return null;
    }

    /** An activity by its name; a dialog or popup by what it is (Volition's Elements.nameOf). */
    private static String nameOf(View root) {
        if (!(root.getLayoutParams() instanceof WindowManager.LayoutParams)) return "window";
        WindowManager.LayoutParams p = (WindowManager.LayoutParams) root.getLayoutParams();
        if (p.type == WindowManager.LayoutParams.TYPE_APPLICATION) return "dialog";
        if (p.type >= WindowManager.LayoutParams.FIRST_SUB_WINDOW && p.type <= WindowManager.LayoutParams.LAST_SUB_WINDOW) return "popup";
        String title = p.getTitle() == null ? "" : p.getTitle().toString();
        title = title.substring(title.lastIndexOf('/') + 1);
        title = title.substring(title.lastIndexOf('.') + 1).trim();
        return title.isEmpty() ? "window" : title;
    }

    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) { return null; }
    @Override public String getType(Uri uri) { return null; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] selectionArgs) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) { return 0; }
}
