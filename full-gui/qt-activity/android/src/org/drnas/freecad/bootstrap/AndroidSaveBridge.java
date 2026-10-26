package org.drnas.freecad.bootstrap;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Provider writes run off both the Android and Qt GUI threads. */
public final class AndroidSaveBridge {
    private static final String TAG = "DRNAS_SAVE";
    private static String result = "IDLE";
    private static final ExecutorService WRITER = Executors.newSingleThreadExecutor();

    private AndroidSaveBridge() {}

    public static synchronized boolean launchSave(Activity activity, String source,
                                                   String filename, String destination) {
        if (activity == null || "PENDING".equals(result)) return false;
        result = "PENDING";
        try {
            validateSource(activity, source);
            if (destination != null && !destination.isEmpty()) {
                write(activity.getApplicationContext(), source, Uri.parse(destination), null);
            } else {
                activity.runOnUiThread(() -> {
                    try {
                        Intent picker = new Intent(activity, DocumentSaveActivity.class);
                        picker.putExtra("source", source);
                        picker.putExtra("filename", filename);
                        activity.startActivity(picker);
                    } catch (Exception error) {
                        finish("ERROR", "", error.toString());
                    }
                });
            }
            return true;
        } catch (Exception error) {
            finish("ERROR", "", error.toString());
            return true; // Qt consumes the detailed final result through poll.
        }
    }

    private static File validateSource(Context context, String source) throws IOException {
        File root = new File(context.getFilesDir(), "drnas-documents").getCanonicalFile();
        File file = new File(source).getCanonicalFile();
        if (!file.getPath().startsWith(root.getPath() + File.separator)
                || !file.getName().endsWith(".FCStd")) {
            throw new IOException("Save snapshot is outside the private document directory");
        }
        return file;
    }

    public static void write(Context context, String source, Uri destination, Runnable completed) {
        WRITER.execute(() -> {
            try {
                if (!"content".equals(destination.getScheme())) {
                    throw new IOException("Save destination is not an Android document URI");
                }
                File local = validateSource(context, source);
                long bytes = FcstdTransfer.write(local,
                    () -> context.getContentResolver().openOutputStream(destination, "wt"));
                Log.i(TAG, "PROVIDER_WRITE_OK bytes=" + bytes);
                finish("SUCCESS", destination.toString(), "");
            } catch (Exception error) {
                Log.e(TAG, "PROVIDER_WRITE_FAILED; private snapshot retained", error);
                finish("ERROR", "", error.getClass().getSimpleName() + ": " + error.getMessage());
            } finally {
                if (completed != null) completed.run();
            }
        });
    }

    public static synchronized void finish(String status, String uri, String message) {
        if (!"PENDING".equals(result)) return;
        try {
            JSONObject object = new JSONObject();
            object.put("status", status);
            object.put("uri", uri);
            object.put("message", message);
            result = object.toString();
        } catch (Exception error) {
            result = "{\"status\":\"ERROR\",\"message\":\"Save result encoding failed\"}";
        }
    }

    public static synchronized String pollSaveResult() {
        String current = result;
        if (!"PENDING".equals(current) && !"IDLE".equals(current)) result = "IDLE";
        return current;
    }
}
