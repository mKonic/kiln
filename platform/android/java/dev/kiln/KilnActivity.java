package dev.kiln;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.os.Bundle;

/**
 * Hosts one kiln root. Platform-only (no androidx): the app's native library is named by
 * {@code <meta-data android:name="dev.kiln.lib_name">} on this activity, loaded before the view
 * is created. Configuration changes listed in the manifest are handled in-process.
 */
public class KilnActivity extends Activity {
    // One registry per root ("SaveableStateRegistry:<id>", DisposableSaveableStateRegistry.android.kt).
    private static final String SAVED_STATE_KEY = "SaveableStateRegistry:kiln";
    private KilnView view;
    // The activity result registry: restored before the content registers its launchers.
    final KilnActivityResults activityResults = new KilnActivityResults(this);
    // enableEdgeToEdge's styles: the defaults until the app asks for others; re-applied when the
    // configuration changes in-process (where Android would recreate the activity).
    private KilnEdgeToEdge.Style statusBarStyle = KilnEdgeToEdge.defaultStatus();
    private KilnEdgeToEdge.Style navigationBarStyle = KilnEdgeToEdge.defaultNavigation();

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        outState.putByteArray(SAVED_STATE_KEY, view.saveState());
        activityResults.onSaveInstanceState(outState);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        System.loadLibrary(libraryName());

        // Edge to edge: kiln draws behind the system bars and reads insets itself.
        KilnEdgeToEdge.enable(this, statusBarStyle, navigationBarStyle);

        activityResults.onRestoreInstanceState(savedInstanceState);
        byte[] saved = savedInstanceState != null ? savedInstanceState.getByteArray(SAVED_STATE_KEY) : null;
        view = new KilnView(this, saved);
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
        view.release();
        KilnNative.destroy();
        super.onDestroy();
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        if (!activityResults.dispatchResult(requestCode, resultCode, data)) super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (!activityResults.dispatchPermissionsResult(requestCode, permissions, grantResults))
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        KilnEdgeToEdge.enable(this, statusBarStyle, navigationBarStyle);
        view.onHostConfigurationChanged(newConfig);
    }

    /** enableEdgeToEdge(statusBarStyle, navigationBarStyle) from native code. */
    void enableEdgeToEdge(KilnEdgeToEdge.Style status, KilnEdgeToEdge.Style navigation) {
        statusBarStyle = status;
        navigationBarStyle = navigation;
        KilnEdgeToEdge.enable(this, status, navigation);
    }
}
