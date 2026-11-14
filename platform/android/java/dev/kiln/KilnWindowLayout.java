package dev.kiln;

import android.app.Activity;
import android.content.Context;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.view.WindowMetrics;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;

/**
 * The window's folds and hinges, from the OEM window extensions as androidx.window reads them
 * (SafeWindowLayoutComponentProvider, ExtensionWindowBackendApi1/2,
 * ExtensionsWindowLayoutInfoAdapter), by reflection: the extensions are a system library the
 * app's manifest asks for (uses-library androidx.window.extensions, not required).
 */
final class KilnWindowLayout {
    private static final String EXTENSIONS = "androidx.window.extensions";
    // androidx.window.extensions.layout.FoldingFeature.
    private static final int TYPE_FOLD = 1, TYPE_HINGE = 2, STATE_FLAT = 1, STATE_HALF_OPENED = 2;

    private final Activity activity;
    private final int windowId;
    private Object component;
    private Object consumer;
    private Method remove;

    private KilnWindowLayout(Activity activity, int windowId) {
        this.activity = activity;
        this.windowId = windowId;
    }

    /** Starts listening, or returns null where the device has no window extensions. */
    static KilnWindowLayout start(Context context, int windowId) {
        Activity activity = activityOf(context);
        if (activity == null) return null;
        KilnWindowLayout layout = new KilnWindowLayout(activity, windowId);
        return layout.register() ? layout : null;
    }

    void stop() {
        if (component == null || remove == null) return;
        try {
            remove.invoke(component, consumer);
        } catch (ReflectiveOperationException | RuntimeException ignored) {
        }
        component = null;
    }

    private static Activity activityOf(Context c) {
        while (c instanceof android.content.ContextWrapper) {
            if (c instanceof Activity) return (Activity) c;
            c = ((android.content.ContextWrapper) c).getBaseContext();
        }
        return null;
    }

    private boolean register() {
        try {
            ClassLoader loader = KilnWindowLayout.class.getClassLoader();
            Class<?> providerClass = loader.loadClass(EXTENSIONS + ".WindowExtensionsProvider");
            Object extensions = providerClass.getMethod("getWindowExtensions").invoke(null);
            if (extensions == null) return false;
            int vendorApiLevel = (int) extensions.getClass().getMethod("getVendorApiLevel").invoke(extensions);
            Object layoutComponent = extensions.getClass().getMethod("getWindowLayoutComponent").invoke(extensions);
            if (layoutComponent == null || vendorApiLevel < 1) return false;
            Class<?> componentClass = loader.loadClass(EXTENSIONS + ".layout.WindowLayoutComponent");
            Class<?> consumerClass;
            Method add;
            if (vendorApiLevel >= 2) {
                // The extensions' own Consumer, with a Context (ExtensionWindowBackendApi2).
                consumerClass = loader.loadClass(EXTENSIONS + ".core.util.function.Consumer");
                add = componentClass.getMethod("addWindowLayoutInfoListener", Context.class, consumerClass);
            } else {
                consumerClass = java.util.function.Consumer.class;
                add = componentClass.getMethod("addWindowLayoutInfoListener", Activity.class, consumerClass);
            }
            remove = componentClass.getMethod("removeWindowLayoutInfoListener", consumerClass);
            Handler main = new Handler(Looper.getMainLooper());
            InvocationHandler handler = (proxy, method, args) -> {
                switch (method.getName()) {
                    case "accept":
                        Object info = args[0];
                        // Reported on the main thread on the devices seen, but not promised.
                        if (Looper.myLooper() == Looper.getMainLooper()) report(info);
                        else main.post(() -> report(info));
                        return null;
                    case "equals": return proxy == args[0];
                    case "hashCode": return System.identityHashCode(proxy);
                    case "toString": return "KilnWindowLayout.Consumer";
                    default: return null;
                }
            };
            consumer = Proxy.newProxyInstance(loader, new Class<?>[] { consumerClass }, handler);
            component = layoutComponent;
            add.invoke(component, activity, consumer);
            return true;
        } catch (ClassNotFoundException | NoClassDefFoundError e) {
            return false;   // no window extensions on this device
        } catch (ReflectiveOperationException | RuntimeException | LinkageError e) {
            android.util.Log.w("kiln", "window extensions unavailable: " + e);
            return false;
        }
    }

    /** ExtensionsWindowLayoutInfoAdapter.translate: folding features with valid bounds. */
    private void report(Object info) {
        if (component == null) return;
        List<Integer> out = new ArrayList<>();
        try {
            ClassLoader loader = KilnWindowLayout.class.getClassLoader();
            Class<?> foldingClass = loader.loadClass(EXTENSIONS + ".layout.FoldingFeature");
            List<?> features = (List<?>) info.getClass().getMethod("getDisplayFeatures").invoke(info);
            WindowMetrics metrics = activity.getWindowManager().getCurrentWindowMetrics();
            Rect window = metrics.getBounds();
            for (Object f : features) {
                if (!foldingClass.isInstance(f)) continue;
                Rect bounds = (Rect) foldingClass.getMethod("getBounds").invoke(f);
                int type = (int) foldingClass.getMethod("getType").invoke(f);
                int state = (int) foldingClass.getMethod("getState").invoke(f);
                if (type != TYPE_FOLD && type != TYPE_HINGE) continue;
                if (state != STATE_FLAT && state != STATE_HALF_OPENED) continue;
                if (!validBounds(window, bounds)) continue;
                out.add(bounds.left); out.add(bounds.top); out.add(bounds.right); out.add(bounds.bottom);
                out.add(type == TYPE_HINGE ? 1 : 0);
                out.add(state == STATE_HALF_OPENED ? 1 : 0);
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            android.util.Log.w("kiln", "window layout info unreadable: " + e);
        }
        int[] data = new int[out.size()];
        for (int i = 0; i < data.length; i++) data[i] = out.get(i);
        KilnNative.setDisplayFolds(windowId, data);
    }

    /** A fold spans the window on one axis, is thinner on the other, and is not empty. */
    private static boolean validBounds(Rect window, Rect bounds) {
        int w = bounds.width(), h = bounds.height(), ww = window.width(), wh = window.height();
        if (w == 0 && h == 0) return false;
        if (w != ww && h != wh) return false;
        if (w < ww && h < wh) return false;
        return !(w == ww && h == wh);
    }
}
