package org.drnas.freecad.smoke;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

// Pure Java STL serializer: inputs are original CAD coordinates in millimetres.
// Binary STL triangle = 12-byte normal + 36-byte vertices + 2-byte attributes.
// This exports a TRIANGULATED MESH, not a FreeCAD parametric document.
public final class StlWriter {
    private StlWriter() {}

    public static int write(OutputStream stream,float[] mesh) throws IOException {
        if(stream==null || mesh==null || mesh.length==0 || mesh.length%18!=0 ||
                mesh.length/18>200000) throw new IllegalArgumentException("Invalid triangle mesh");
        int triangles=mesh.length/18;
        byte[] header=new byte[84];
        byte[] title="DRNAS FreeCAD Render LAB - Binary STL (mm)".getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(title,0,header,0,Math.min(80,title.length));
        ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN).putInt(80,triangles);
        stream.write(header);
        ByteBuffer record=ByteBuffer.allocate(50).order(ByteOrder.LITTLE_ENDIAN);
        for(int t=0;t<triangles;t++) {
            record.clear();
            int at=t*18;
            for(int k=3;k<=5;k++) {
                float v=mesh[at+k];
                if(!Float.isFinite(v)) throw new IllegalArgumentException("Nonfinite normal");
                record.putFloat(v);
            }
            for(int p=0;p<3;p++) {
                for(int k=0;k<3;k++) {
                    float v=mesh[at+p*6+k];
                    if(!Float.isFinite(v)) throw new IllegalArgumentException("Nonfinite position");
                    record.putFloat(v);
                }
            }
            record.putShort((short)0);
            stream.write(record.array());
        }
        stream.flush();
        return triangles;
    }
}
