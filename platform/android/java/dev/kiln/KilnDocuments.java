package dev.kiln;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;

import java.util.ArrayList;

/**
 * ui::DocumentFile's host: TreeDocumentFile / SingleDocumentFile and DocumentsContractApi19
 * (androidx documentfile), by URI string. Called on any thread (content queries block).
 */
final class KilnDocuments {
    private static final String TAG = "DocumentFile";

    private KilnDocuments() {}

    private static Context context() { return KilnContent.context(); }

    static boolean isDocumentUri(String uri) {
        Context c = context();
        return c != null && DocumentsContract.isDocumentUri(c, Uri.parse(uri));
    }

    /** DocumentFile.fromTreeUri: the document URI for the tree, or null for a URI that is not one. */
    static String treeDocumentUri(String treeUri) {
        Context c = context();
        if (c == null) return null;
        try {
            Uri tree = Uri.parse(treeUri);
            String id = DocumentsContract.isDocumentUri(c, tree) ? DocumentsContract.getDocumentId(tree)
                                                                  : DocumentsContract.getTreeDocumentId(tree);
            return DocumentsContract.buildDocumentUriUsingTree(tree, id).toString();
        } catch (RuntimeException e) {
            return null;
        }
    }

    /** { displayName, mimeType, flags, lastModified, size }, or null when there is no such document. */
    static String[] query(String uri) {
        Context c = context();
        if (c == null) return null;
        String[] columns = { DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
                             DocumentsContract.Document.COLUMN_FLAGS, DocumentsContract.Document.COLUMN_LAST_MODIFIED,
                             DocumentsContract.Document.COLUMN_SIZE };
        try (Cursor cursor = c.getContentResolver().query(Uri.parse(uri), columns, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) return null;
            String[] out = new String[5];
            for (int i = 0; i < 5; i++) out[i] = cursor.isNull(i) ? null : cursor.getString(i);
            return out;
        } catch (Exception e) {
            Log.w(TAG, "Failed query: " + e);
            return null;
        }
    }

    static boolean hasPermission(String uri, boolean write) {
        Context c = context();
        if (c == null) return false;
        int flag = write ? Intent.FLAG_GRANT_WRITE_URI_PERMISSION : Intent.FLAG_GRANT_READ_URI_PERMISSION;
        return c.checkCallingOrSelfUriPermission(Uri.parse(uri), flag) == PackageManager.PERMISSION_GRANTED;
    }

    static String create(String parent, String mimeType, String displayName) {
        Context c = context();
        if (c == null) return null;
        try {
            Uri u = DocumentsContract.createDocument(c.getContentResolver(), Uri.parse(parent), mimeType, displayName);
            return u == null ? null : u.toString();
        } catch (Exception e) {
            return null;
        }
    }

    static boolean delete(String uri) {
        Context c = context();
        if (c == null) return false;
        try {
            return DocumentsContract.deleteDocument(c.getContentResolver(), Uri.parse(uri));
        } catch (Exception e) {
            return false;
        }
    }

    static String rename(String uri, String displayName) {
        Context c = context();
        if (c == null) return null;
        try {
            Uri u = DocumentsContract.renameDocument(c.getContentResolver(), Uri.parse(uri), displayName);
            return u == null ? null : u.toString();
        } catch (Exception e) {
            return null;
        }
    }

    /** TreeDocumentFile.listFiles: the children's document URIs. */
    static String[] children(String uri) {
        Context c = context();
        ArrayList<String> out = new ArrayList<>();
        if (c == null) return new String[0];
        Uri self = Uri.parse(uri);
        ContentResolver resolver = c.getContentResolver();
        try {
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(self, DocumentsContract.getDocumentId(self));
            try (Cursor cursor = resolver.query(childrenUri, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID }, null, null, null)) {
                while (cursor != null && cursor.moveToNext())
                    out.add(DocumentsContract.buildDocumentUriUsingTree(self, cursor.getString(0)).toString());
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed query: " + e);
        }
        return out.toArray(new String[0]);
    }
}
