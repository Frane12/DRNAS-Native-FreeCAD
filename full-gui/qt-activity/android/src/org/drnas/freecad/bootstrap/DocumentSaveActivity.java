package org.drnas.freecad.bootstrap;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

/** Asynchronous Android Save As broker; never opens a FreeCAD Qt dialog. */
public final class DocumentSaveActivity extends Activity {
    private static final int SAVE_REQUEST = 7202;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) return;
        Intent picker = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        picker.addCategory(Intent.CATEGORY_OPENABLE);
        picker.setType("application/octet-stream");
        picker.putExtra(Intent.EXTRA_TITLE, getIntent().getStringExtra("filename"));
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(picker, SAVE_REQUEST);
        } catch (Exception error) {
            AndroidSaveBridge.finish("ERROR", "", error.toString());
            finish();
        }
    }

    @Override protected void onActivityResult(int request, int code, Intent data) {
        super.onActivityResult(request, code, data);
        if (request != SAVE_REQUEST || code != RESULT_OK || data == null || data.getData() == null) {
            AndroidSaveBridge.finish("CANCELLED", "", "");
            finish();
            return;
        }
        Uri uri = data.getData();
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if ((data.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
            try {
                getContentResolver().takePersistableUriPermission(uri, flags);
            } catch (SecurityException error) {
                // A provider may offer only a session grant; writing still works.
                Log.i("DRNAS_SAVE", "Destination grant limited to current session");
            }
        }
        AndroidSaveBridge.write(getApplicationContext(), getIntent().getStringExtra("source"),
                uri, () -> runOnUiThread(this::finish));
    }
}
