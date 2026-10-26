package org.drnas.freecad.bootstrap;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

/**
 * Short-lived Android-only SAF broker. Using ACTION_OPEN_DOCUMENT gives
 * DocumentsUI access to Downloads, device files, Drive and other providers.
 * The URI is copied while this Activity still owns its read permission.
 * It does not launch or change any FreeCAD GUI/native OpenGL surface.
 */
public final class DocumentPickerActivity extends Activity {
    private static final int DOCUMENT_REQUEST = 7201;
    private static final String TAG = "DRNAS_SAF";

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState != null) {
            // Picker selection in flight after an Activity recreation.
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(intent, DOCUMENT_REQUEST);
        } catch (ActivityNotFoundException | SecurityException error) {
            Log.e(TAG, "SAF_PICKER cannot open DocumentsUI", error);
            AndroidDocumentBridge.finishPicker("ERROR: Android DocumentsUI unavailable: " + error);
            finish();
        }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != DOCUMENT_REQUEST) {
            AndroidDocumentBridge.finishPicker("ERROR: Unexpected SAF request");
            finish();
            return;
        }
        if (result != RESULT_OK || data == null || data.getData() == null) {
            AndroidDocumentBridge.finishPicker("CANCELLED");
            finish();
            return;
        }
        try {
            Uri uri = data.getData();
            Log.i(TAG, "SAF_PICKER SELECT uri_scheme=" + uri.getScheme());
            final String localFile = AndroidDocumentBridge.cacheForOpen(this, uri.toString());
            if (localFile.startsWith("ERROR:")) {
                AndroidDocumentBridge.finishPicker(localFile);
            } else {
                Log.i(TAG, "SAF_PICKER STAGED app_private_file=true");
                AndroidDocumentBridge.finishPicker("OK:" + localFile);
            }
        } catch (Exception error) {
            Log.e(TAG, "SAF_PICKER result failure", error);
            AndroidDocumentBridge.finishPicker("ERROR: " + error);
        } finally {
            finish();
        }
    }
}
