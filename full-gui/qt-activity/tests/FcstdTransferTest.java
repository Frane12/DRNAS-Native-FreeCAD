package org.drnas.freecad.bootstrap;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Run with javac/java on the host; never packaged in the Android APK. */
public final class FcstdTransferTest {
    private static void require(boolean condition) {
        if (!condition) throw new AssertionError();
    }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("fcstd-transfer").toFile();
        File source = new File(root, "prozor.FCStd");
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(source))) {
            zip.putNextEntry(new ZipEntry("Document.xml"));
            zip.write("<Document/>".getBytes("UTF-8"));
            zip.closeEntry();
        }
        byte[] original = Files.readAllBytes(source.toPath());
        File destination = new File(root, "existing.FCStd");
        Files.write(destination.toPath(), new byte[original.length + 8192]);
        long bytes = FcstdTransfer.write(source, () -> new FileOutputStream(destination, false));
        require(bytes == original.length);
        require(Arrays.equals(original, Files.readAllBytes(destination.toPath())));
        for (String failure : new String[]{"open", "write", "flush", "close"}) {
            boolean failed = false;
            try {
                FcstdTransfer.write(source, () -> {
                    if (failure.equals("open")) throw new IOException("permission revoked");
                    return new OutputStream() {
                        @Override public void write(int b) throws IOException {
                            if (failure.equals("write")) throw new IOException("disk full");
                        }
                        @Override public void flush() throws IOException {
                            if (failure.equals("flush")) throw new IOException("flush failed");
                        }
                        @Override public void close() throws IOException {
                            if (failure.equals("close")) throw new IOException("provider close failed");
                        }
                    };
                });
            } catch (IOException expected) { failed = true; }
            require(failed);
            require(Arrays.equals(original, Files.readAllBytes(source.toPath())));
        }
        File invalid = new File(root, "bad.FCStd");
        Files.write(invalid.toPath(), "invalid".getBytes("UTF-8"));
        boolean[] opened = {false};
        try {
            FcstdTransfer.write(invalid, () -> { opened[0] = true; return new ByteArrayOutputStream(); });
            throw new AssertionError();
        } catch (IOException expected) { require(!opened[0]); }
        System.out.println("PASS: FCStd transfer, truncation, revoked permission, write/flush/close failures, snapshot retention");
    }
}
