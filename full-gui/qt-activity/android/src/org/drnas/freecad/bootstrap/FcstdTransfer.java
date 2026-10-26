package org.drnas.freecad.bootstrap;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.zip.ZipFile;

/** Small Java-only transfer core, also exercised by host tests. */
public final class FcstdTransfer {
    private FcstdTransfer() {}

    public interface Destination {
        OutputStream open() throws IOException;
    }

    public static long write(File source, Destination destination) throws IOException {
        if (!source.isFile() || source.length() == 0) {
            throw new IOException("Private FCStd snapshot is missing");
        }
        // Validate BEFORE opening a provider stream, which can truncate a file.
        try (ZipFile zip = new ZipFile(source)) {
            if (zip.getEntry("Document.xml") == null) {
                throw new IOException("Private FCStd snapshot has no Document.xml");
            }
        }
        long expected = source.length();
        long count = 0;
        try (FileInputStream in = new FileInputStream(source);
             OutputStream out = destination.open()) {
            if (out == null) throw new IOException("Android destination returned no output stream");
            byte[] buffer = new byte[65536];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
                count += read;
            }
            if (count != expected) throw new IOException("FCStd snapshot changed during transfer");
            out.flush();
        }
        // Both provider flush AND close must succeed before returning success.
        return count;
    }
}
