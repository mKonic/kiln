package dev.kiln;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Window;

/**
 * Hosts one kiln root. Platform-only (no androidx): the app's native library is named by
 * {@code <meta-data android:name="dev.kiln.lib_name">} on this activity, loaded before the view
 * is created. Configuration changes listed in the manifest are handled in-process.
 */
public class KilnActivity extends Activity {
    private KilnView view;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        System.loadLibrary(libraryName());

        Window window = getWindow();
        // Edge to edge: kiln draws behind the system bars and reads insets itself.
        window.setDecorFitsSystemWindows(false);
        window.setStatusBarColor(Color.TRANSPARENT);
        window.setNavigationBarColor(Color.TRANSPARENT);
        window.setNavigationBarContrastEnforced(false);

        view = new KilnView(this);
        setContentView(view);
    }

    private String libraryName() {
        try {
            ActivityInfo info = getPackageManager().getActivityInfo(getComponentName(),
                    PackageManager.GET_META_DATA);
            if (info.metaData != null) {
                String name = info.metaData.getString("dev.kiln.lib_name");
                if (name != null) return name;
            }
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        return "main";
    }

    @Override
    protected void onStart() {
        super.onStart();
        view.onHostStart();
    }

    @Override
    protected void onStop() {
        view.onHostStop();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        KilnNative.destroy();
        super.onDestroy();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        view.onHostConfigurationChanged(newConfig);
    }
}
