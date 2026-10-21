package org.drnas.freecad.smoke;
import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
public final class StlWriterTest {
    public static void main(String[] args) throws Exception {
        float[] mesh={
            0f,0f,0f,0f,0f,1f,
            2f,0f,0f,0f,0f,1f,
            0f,3f,0f,0f,0f,1f
        };
        ByteArrayOutputStream out=new ByteArrayOutputStream();
        if(StlWriter.write(out,mesh)!=1) throw new AssertionError("triangle count");
        byte[] bytes=out.toByteArray();
        if(bytes.length!=134) throw new AssertionError("binary STL size "+bytes.length);
        ByteBuffer b=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if(b.getInt(80)!=1 || b.getFloat(84+8)!=1f ||
           b.getFloat(84+12+12)!=2f ||
           b.getFloat(84+12+24+4)!=3f)
            throw new AssertionError("STL positions/normal mismatch");
        try {StlWriter.write(new ByteArrayOutputStream(),new float[]{1f});throw new AssertionError("invalid geometry accepted");}
        catch(IllegalArgumentException expected){}
        System.out.println("PASS: STL header, little-endian triangle record, vertices, and validation");
    }
}
