package dev.kiln;

import android.content.ComponentName;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;

/**
 * A process started for something other than the activity (a job, a widget, an alarm): loads the
 * app's library (KilnActivity's dev.kiln.lib_name meta-data) and gives kiln's platform services
 * the application context, so native code can post notifications, fetch and play there too.
 */
final class KilnProcess {
    private static boolean loaded;

    private KilnProcess() {}

    static synchronized void ensureLoaded(Context context) {
        Context app = context.getApplicationContext();
        KilnWork.attach(app);
        KilnHttp.attach(app);
        KilnMedia.attach(app);
        KilnNotifications.attach(app);
        KilnWidgetReceiver.attach(app);
        KilnAlarms.attach(app);
        KilnDevice.attach(app);
        KilnContent.attach(app);
        if (!KilnResources.attached()) KilnResources.attach(app);
        if (loaded) return;
        System.loadLibrary(libraryName(app));
        loaded = true;
        KilnNative.setProcessAssets(app.getAssets());
        KilnNative.setDirectories(app.getFilesDir().getPath(), app.getCacheDir().getPath(), app.getNoBackupFilesDir().getPath());
        KilnNative.setFileProvider(KilnFileProvider.config(app));
    }

    static String libraryName(Context context) {
        try {
            ActivityInfo info = context.getPackageManager().getActivityInfo(new ComponentName(context, KilnActivity.class),
                    PackageManager.GET_META_DATA);
            if (info.metaData != null) {
                String name = info.metaData.getString("dev.kiln.lib_name");
                if (name != null) return name;
            }
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        return "main";
    }
}
