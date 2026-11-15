package dev.kiln;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.util.List;

/**
 * ContentResolver for kiln's ui::ContentResolver: descriptors for content URIs (detached, so
 * native code owns and closes them), OpenableColumns metadata, getType and persistable grants.
 * Uses the application context, so it is safe from any thread.
 */
final class KilnContent {
    private static Context sContext;

    private KilnContent() {}

    static void attach(Context context) { sContext = context.getApplicationContext(); }
    static Context context() { return sContext; }

    /** The app's memory budget for image caches: { bytes, lowRam } (Coil's totalAvailableMemoryBytes). */
    static long[] memoryClass() {
        android.app.ActivityManager am = sContext.getSystemService(android.app.ActivityManager.class);
        if (am == null) return new long[] { 0, 0 };
        boolean large = (sContext.getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_LARGE_HEAP) != 0;
        long mb = large ? am.getLargeMemoryClass() : am.getMemoryClass();
        return new long[] { mb * 1024L * 1024L, am.isLowRamDevice() ? 1 : 0 };
    }

    private static ContentResolver resolver() { return sContext.getContentResolver(); }

    static int openFd(String uri, String mode) {
        if (sContext == null) return -1;
        try {
            Uri u = Uri.parse(uri);
            ParcelFileDescriptor pfd = "file".equals(u.getScheme())
                    ? ParcelFileDescriptor.open(new java.io.File(u.getPath()), ParcelFileDescriptor.parseMode(mode))
                    : resolver().openFileDescriptor(u, mode);
            return pfd == null ? -1 : pfd.detachFd();
        } catch (Exception e) {
            return -1;
        }
    }

    /** [displayName, size, mimeType]; null when the URI cannot be queried. */
    static String[] query(String uri) {
        if (sContext == null) return null;
        Uri u = Uri.parse(uri);
        String name = null, size = null, type = null;
        try {
            type = resolver().getType(u);
        } catch (Exception ignored) {
        }
        try (Cursor c = resolver().query(u, new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int n = c.getColumnIndex(OpenableColumns.DISPLAY_NAME), s = c.getColumnIndex(OpenableColumns.SIZE);
                if (n >= 0 && !c.isNull(n)) name = c.getString(n);
                if (s >= 0 && !c.isNull(s)) size = Long.toString(c.getLong(s));
            } else if (type == null) {
                return null;
            }
        } catch (Exception e) {
            if (type == null) return null;
        }
        return new String[] { name, size, type };
    }

    private static int flags(boolean read, boolean write) {
        return (read ? Intent.FLAG_GRANT_READ_URI_PERMISSION : 0) | (write ? Intent.FLAG_GRANT_WRITE_URI_PERMISSION : 0);
    }

    static boolean takePersistable(String uri, boolean read, boolean write) {
        try {
            resolver().takePersistableUriPermission(Uri.parse(uri), flags(read, write));
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    static boolean releasePersistable(String uri, boolean read, boolean write) {
        try {
            resolver().releasePersistableUriPermission(Uri.parse(uri), flags(read, write));
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    static String[] persisted() {
        List<UriPermission> list = resolver().getPersistedUriPermissions();
        String[] out = new String[list.size()];
        for (int i = 0; i < out.length; ++i) out[i] = list.get(i).getUri().toString();
        return out;
    }
}
