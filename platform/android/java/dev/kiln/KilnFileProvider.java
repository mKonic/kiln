package dev.kiln;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * ui::GetUriForFile's provider (androidx.core FileProvider with SimplePathStrategy): serves the
 * app's own folders by content:// URI to apps it grants them to. Not exported; manifests declare
 * it with authority {@code <package>.kiln.fileprovider} and grantUriPermissions.
 */
public class KilnFileProvider extends ContentProvider {
    private static final String[] COLUMNS = { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE };
    private static final String DISPLAYNAME_FIELD = "displayName";

    private final Map<String, File> roots = new HashMap<>();

    /** The authority and the roots, as name, folder pairs: what native code mirrors. */
    static String[] config(Context context) {
        List<String> out = new ArrayList<>();
        out.add(context.getPackageName() + ".kiln.fileprovider");
        add(out, "files", context.getFilesDir());
        add(out, "cache", context.getCacheDir());
        add(out, "no_backup", context.getNoBackupFilesDir());
        add(out, "external_files", context.getExternalFilesDir(null));
        add(out, "external_cache", context.getExternalCacheDir());
        return out.toArray(new String[0]);
    }
    private static void add(List<String> out, String name, File dir) {
        if (dir == null) return;
        out.add(name);
        out.add(dir.getPath());
    }

    @Override
    public boolean onCreate() { return true; }

    @Override
    public void attachInfo(Context context, ProviderInfo info) {
        super.attachInfo(context, info);
        // FileProvider's checks: a private provider that grants access.
        if (info.exported) throw new SecurityException("Provider must not be exported");
        if (!info.grantUriPermissions) throw new SecurityException("Provider must grant uri permissions");
        String[] c = config(context);
        for (int i = 1; i + 1 < c.length; i += 2) {
            try {
                roots.put(c[i], new File(c[i + 1]).getCanonicalFile());
            } catch (IOException e) {
                throw new IllegalArgumentException("Failed to resolve canonical path for " + c[i + 1], e);
            }
        }
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        final File file = getFileForUri(uri);
        String displayName = uri.getQueryParameter(DISPLAYNAME_FIELD);
        if (projection == null) projection = COLUMNS;
        String[] cols = new String[projection.length];
        Object[] values = new Object[projection.length];
        int i = 0;
        for (String col : projection) {
            if (OpenableColumns.DISPLAY_NAME.equals(col)) {
                cols[i] = OpenableColumns.DISPLAY_NAME;
                values[i++] = displayName == null ? file.getName() : displayName;
            } else if (OpenableColumns.SIZE.equals(col)) {
                cols[i] = OpenableColumns.SIZE;
                values[i++] = file.length();
            }
        }
        cols = Arrays.copyOf(cols, i);
        values = Arrays.copyOf(values, i);
        final MatrixCursor cursor = new MatrixCursor(cols, 1);
        cursor.addRow(values);
        return cursor;
    }

    @Override
    public String getType(Uri uri) {
        final File file = getFileForUri(uri);
        final int lastDot = file.getName().lastIndexOf('.');
        if (lastDot >= 0) {
            final String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(file.getName().substring(lastDot + 1));
            if (mime != null) return mime;
        }
        return "application/octet-stream";
    }

    @Override
    public String getTypeAnonymous(Uri uri) { return "application/octet-stream"; }

    @Override
    public Uri insert(Uri uri, ContentValues values) { throw new UnsupportedOperationException("No external inserts"); }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("No external updates");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) { return getFileForUri(uri).delete() ? 1 : 0; }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(getFileForUri(uri), modeToMode(mode));
    }

    private static int modeToMode(String mode) {
        if ("r".equals(mode)) return ParcelFileDescriptor.MODE_READ_ONLY;
        if ("w".equals(mode) || "wt".equals(mode))
            return ParcelFileDescriptor.MODE_WRITE_ONLY | ParcelFileDescriptor.MODE_CREATE | ParcelFileDescriptor.MODE_TRUNCATE;
        if ("wa".equals(mode))
            return ParcelFileDescriptor.MODE_WRITE_ONLY | ParcelFileDescriptor.MODE_CREATE | ParcelFileDescriptor.MODE_APPEND;
        if ("rw".equals(mode)) return ParcelFileDescriptor.MODE_READ_WRITE | ParcelFileDescriptor.MODE_CREATE;
        if ("rwt".equals(mode))
            return ParcelFileDescriptor.MODE_READ_WRITE | ParcelFileDescriptor.MODE_CREATE | ParcelFileDescriptor.MODE_TRUNCATE;
        throw new IllegalArgumentException("Invalid mode: " + mode);
    }

    /** SimplePathStrategy.getFileForUri. */
    private File getFileForUri(Uri uri) {
        String path = uri.getEncodedPath();
        final int splitIndex = path.indexOf('/', 1);
        if (splitIndex == -1) throw new IllegalArgumentException("Unable to find path from root: " + uri);
        final String tag = Uri.decode(path.substring(1, splitIndex));
        path = Uri.decode(path.substring(splitIndex + 1));
        final File root = roots.get(tag);
        if (root == null) throw new IllegalArgumentException("Unable to find configured root for " + uri);
        File file = new File(root, path);
        try {
            file = file.getCanonicalFile();
        } catch (IOException e) {
            throw new IllegalArgumentException("Failed to resolve canonical path for " + file);
        }
        if (!file.getPath().startsWith(removeTrailingSlash(root.getPath()) + '/'))
            throw new SecurityException("Resolved path jumped beyond configured root");
        return file;
    }

    private static String removeTrailingSlash(String p) {
        return p.length() > 0 && p.charAt(p.length() - 1) == '/' ? p.substring(0, p.length() - 1) : p;
    }
}
