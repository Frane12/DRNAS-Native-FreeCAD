package org.drnas.freecad.bootstrap;

import android.content.ContentResolver;
import android.content.Context;
import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.UUID;

/**
 * DRNAS SAF bridge. Convert a document picked through Android's native file
 * picker into an app-private regular file, preserving the provider's real name
 * and extension for upstream FreeCAD's normal import handler.
 *
 * Called synchronously through Qt JNI before the picker URI grant expires.
 * Uses buffered streaming (not readAll, not QFile::copy(content://)).
 */
public final class AndroidDocumentBridge {
    private static final String TAG = "DRNAS_SAF";

    private AndroidDocumentBridge() {}

    // An explicit Android SAF picker; no QFileDialog/content:// assumptions.
    // Poll from Qt after Android restores the original QtActivity.
    private static String pickerResult = "IDLE";

    public static synchronized boolean launchPicker(Activity activity) {
        if (activity == null || "PENDING".equals(pickerResult)) {
            return false;
        }
        pickerResult = "PENDING";
        try {
            Intent picker = new Intent(activity, DocumentPickerActivity.class);
            activity.startActivity(picker);
            Log.i(TAG, "SAF_PICKER LAUNCH");
            return true;
        } catch (Exception error) {
            pickerResult = "ERROR: Cannot launch Android picker: " + error;
            Log.e(TAG, "SAF_PICKER launch failed", error);
            return false;
        }
    }

    public static synchronized void finishPicker(String result) {
        if ("PENDING".equals(pickerResult)) {
            pickerResult = result != null ? result : "ERROR: No picker result";
        }
    }

    public static synchronized String pollPickerResult() {
        String result = pickerResult;
        if (!"PENDING".equals(result) && !"IDLE".equals(result)) {
            pickerResult = "IDLE";
        }
        return result;
    }

    public static String cacheForOpen(Context context, String uriText) {
        if (context == null || uriText == null || uriText.isEmpty()) {
            return "ERROR: Android context or selected document is missing";
        }
        File target = null;
        File uniqueDirectory = null;
        try {
            Uri uri = Uri.parse(uriText);
            if (!ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())) {
                return "ERROR: Unsupported URI scheme";
            }

            ContentResolver resolver = context.getContentResolver();
            String displayName = null;
            try (Cursor cursor = resolver.query(
                    uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (column >= 0) {
                        displayName = cursor.getString(column);
                    }
                }
            }

            if (displayName == null || displayName.trim().isEmpty()) {
                return "ERROR: The document provider did not supply a filename";
            }

            // A provider controls DISPLAY_NAME: never let it supply a path.
            StringBuilder clean = new StringBuilder();
            for (int index = 0; index < displayName.length(); index++) {
                char ch = displayName.charAt(index);
                clean.append(ch == '/' || ch == '\\' || Character.isISOControl(ch) ? '_' : ch);
            }
            String fileName = clean.toString().trim();
            if (fileName.equals(".") || fileName.equals("..") || fileName.isEmpty()) {
                return "ERROR: Invalid document filename from Android provider";
            }
            int lastDot = fileName.lastIndexOf('.');
            if (lastDot <= 0 || lastDot == fileName.length() - 1) {
                return "ERROR: File has no extension; FreeCAD needs an original filename";
            }
            // Android filesystems typically limit filename bytes. Keep extension
            // intact even when a provider supplies a very long display name.
            if (fileName.length() > 150) {
                String extension = fileName.substring(lastDot);
                if (extension.length() >= 70) {
                    return "ERROR: Invalid filename extension from Android provider";
                }
                fileName = fileName.substring(0, 150 - extension.length()) + extension;
            }

            File stagingRoot = new File(context.getCacheDir(), "drnas-saf-open");
            if (!stagingRoot.isDirectory() && !stagingRoot.mkdirs()) {
                return "ERROR: Cannot create private document staging directory";
            }
            uniqueDirectory = new File(stagingRoot, UUID.randomUUID().toString());
            if (!uniqueDirectory.mkdir()) {
                return "ERROR: Cannot create unique document directory";
            }
            target = new File(uniqueDirectory, fileName);
            try (InputStream in = resolver.openInputStream(uri)) {
                if (in == null) {
                    throw new java.io.IOException("Android provider returned no input stream");
                }
                try (OutputStream out = new FileOutputStream(target)) {
                    byte[] buffer = new byte[65536];
                    int bytesRead;
                    while ((bytesRead = in.read(buffer)) != -1) {
                        out.write(buffer, 0, bytesRead);
                    }
                    out.flush();
                }
            }
            if (!target.isFile()) {
                throw new java.io.IOException("Local document was not written");
            }
            Log.i(TAG, "SAF document copied for FreeCAD open: " + fileName
                    + ", bytes=" + target.length());
            return target.getAbsolutePath();
        } catch (Exception error) {
            Log.e(TAG, "Cannot stage SAF document", error);
            if (target != null) {
                target.delete();
            }
            if (uniqueDirectory != null) {
                uniqueDirectory.delete();
            }
            return "ERROR: " + error.getClass().getSimpleName() + ": "
                    + (error.getMessage() != null ? error.getMessage() : "unknown error");
        }
    }
}
