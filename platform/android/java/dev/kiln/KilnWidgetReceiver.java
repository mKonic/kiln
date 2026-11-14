package dev.kiln;

import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.util.SizeF;
import android.widget.RemoteViews;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A kiln::glance widget's receiver (GlanceAppWidgetReceiver). Each widget has its own subclass
 * (kiln-pack generates them for WIDGETS; a Gradle app writes {@code class X : KilnWidgetReceiver()})
 * declared with the appwidget-provider info and {@code <meta-data android:name="dev.kiln.widget"
 * android:value="name">}, the name the C++ side registered.
 */
public class KilnWidgetReceiver extends AppWidgetProvider {
    private static Context appContext;

    static native Object[] render(String widget, int appWidgetId, float[] launcherSizes, float minWidth, float minHeight);
    static native boolean runCallback(String widget, int appWidgetId, String key, String params, int checked);

    static void attach(Context context) {
        if (appContext == null) appContext = context.getApplicationContext();
    }

    private static void ensureLoaded(Context context) {
        KilnProcess.ensureLoaded(context);
    }

    /** This receiver's widget name (its dev.kiln.widget meta-data). */
    private String widgetName(Context context) {
        try {
            ActivityInfo info = context.getPackageManager().getReceiverInfo(new ComponentName(context, getClass()), PackageManager.GET_META_DATA);
            if (info.metaData != null) return info.metaData.getString("dev.kiln.widget");
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        return null;
    }

    @Override
    public void onUpdate(Context context, AppWidgetManager manager, int[] ids) {
        ensureLoaded(context);
        String name = widgetName(context);
        for (int id : ids) draw(context, manager, new ComponentName(context, getClass()), name, id);
    }

    @Override
    public void onAppWidgetOptionsChanged(Context context, AppWidgetManager manager, int id, Bundle options) {
        ensureLoaded(context);
        draw(context, manager, new ComponentName(context, getClass()), widgetName(context), id);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (KilnGlance.ACTION_RUN.equals(intent.getAction())) {
            ensureLoaded(context);
            String name = widgetName(context);
            int id = intent.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, 0);
            String start = intent.getStringExtra(KilnGlance.EXTRA_START);
            if (start != null) {
                // A deep link from a list item (the list's template is a broadcast).
                context.startActivity(KilnGlance.openFromList(context, start));
                return;
            }
            int checked = intent.hasExtra(RemoteViews.EXTRA_CHECKED) ? (intent.getBooleanExtra(RemoteViews.EXTRA_CHECKED, false) ? 1 : 0) : -1;
            runCallback(name, id, intent.getStringExtra(KilnGlance.EXTRA_KEY), intent.getStringExtra(KilnGlance.EXTRA_PARAMS), checked);
            // Redrawn after its callback, as Glance does.
            updateAll(name);
            return;
        }
        super.onReceive(context, intent);
    }

    static void draw(Context context, AppWidgetManager manager, ComponentName receiver, String name, int id) {
        if (name == null) {
            Log.e("kiln", "widget: " + receiver.getClassName() + " has no dev.kiln.widget meta-data");
            return;
        }
        Bundle options = manager.getAppWidgetOptions(id);
        ArrayList<SizeF> sizes = sizes(options);
        float[] launcher = new float[sizes == null ? 0 : sizes.size() * 2];
        for (int i = 0; sizes != null && i < sizes.size(); i++) {
            launcher[2 * i] = sizes.get(i).getWidth();
            launcher[2 * i + 1] = sizes.get(i).getHeight();
        }
        float minWidth = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH, 0);
        float minHeight = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT, 0);
        Object[] drawn = render(name, id, launcher, minWidth, minHeight);
        if (drawn == null) {
            Log.e("kiln", "widget: no widget named " + name + " (register it with a static glance::WidgetRegistration)");
            return;
        }
        float[] drawnSizes = (float[]) drawn[0];
        byte[][] contents = (byte[][]) drawn[1];
        RemoteViews views;
        try {
            if (contents.length == 1) {
                views = KilnGlance.build(context, contents[0], id, receiver);
            } else {
                // One layout per size, the launcher choosing (RemoteViews(Map<SizeF, RemoteViews>)).
                Map<SizeF, RemoteViews> map = new HashMap<>();
                for (int i = 0; i < contents.length; i++)
                    map.put(new SizeF(drawnSizes[2 * i], drawnSizes[2 * i + 1]), KilnGlance.build(context, contents[i], id, receiver));
                views = new RemoteViews(map);
            }
        } catch (RuntimeException e) {
            Log.e("kiln", "widget " + name + ": cannot draw", e);
            return;
        }
        manager.updateAppWidget(id, views);
    }

    /** OPTION_APPWIDGET_SIZES (the typed getParcelableArrayList is API 33). */
    @SuppressWarnings("deprecation")
    private static ArrayList<SizeF> sizes(Bundle options) {
        if (Build.VERSION.SDK_INT >= 33) return options.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES, SizeF.class);
        return options.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES);
    }

    /** The receivers of a widget: this package's with that dev.kiln.widget name. */
    private static List<ComponentName> receivers(Context context, String name) {
        List<ComponentName> out = new ArrayList<>();
        try {
            PackageInfo p = context.getPackageManager().getPackageInfo(context.getPackageName(),
                    PackageManager.GET_RECEIVERS | PackageManager.GET_META_DATA);
            if (p.receivers != null)
                for (ActivityInfo r : p.receivers)
                    if (r.metaData != null && name.equals(r.metaData.getString("dev.kiln.widget"))) out.add(new ComponentName(r.packageName, r.name));
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        return out;
    }

    // --- glance::GlanceHost -----------------------------------------------------------------

    static void updateAll(String name) {
        Context c = appContext;
        if (c == null || name == null) return;
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        for (ComponentName r : receivers(c, name))
            for (int id : manager.getAppWidgetIds(r)) draw(c, manager, r, name, id);
    }

    static int[] appWidgetIds(String name) {
        Context c = appContext;
        if (c == null) return new int[0];
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        List<Integer> all = new ArrayList<>();
        for (ComponentName r : receivers(c, name)) for (int id : manager.getAppWidgetIds(r)) all.add(id);
        int[] out = new int[all.size()];
        for (int i = 0; i < out.length; i++) out[i] = all.get(i);
        return out;
    }

    static boolean requestPin(String name) {
        Context c = appContext;
        if (c == null) return false;
        AppWidgetManager manager = AppWidgetManager.getInstance(c);
        List<ComponentName> r = receivers(c, name);
        return !r.isEmpty() && manager.isRequestPinAppWidgetSupported() && manager.requestPinAppWidget(r.get(0), null, null);
    }
}
