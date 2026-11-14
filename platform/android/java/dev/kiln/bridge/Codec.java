package dev.kiln.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** The bridge's byte format; kiln/jvm/Codec.h describes it and reads and writes the same. */
public final class Codec {
    private Codec() {}

    public static final class Writer {
        private byte[] bytes = new byte[64];
        private int size;

        private void room(int n) {
            if (size + n > bytes.length) bytes = java.util.Arrays.copyOf(bytes, Math.max(bytes.length * 2, size + n));
        }

        public void writeBoolean(boolean v) { room(1); bytes[size++] = (byte) (v ? 1 : 0); }

        public void writeInt(int v) {
            room(4);
            for (int i = 0; i < 4; i++) bytes[size++] = (byte) (v >>> (8 * i));
        }

        public void writeLong(long v) {
            room(8);
            for (int i = 0; i < 8; i++) bytes[size++] = (byte) (v >>> (8 * i));
        }

        public void writeFloat(float v) { writeInt(Float.floatToRawIntBits(v)); }

        public void writeDouble(double v) { writeLong(Double.doubleToRawLongBits(v)); }

        public void writeBytes(byte[] v) {
            writeInt(v.length);
            room(v.length);
            System.arraycopy(v, 0, bytes, size, v.length);
            size += v.length;
        }

        public void writeString(String v) { writeBytes(v.getBytes(StandardCharsets.UTF_8)); }

        public byte[] toByteArray() { return java.util.Arrays.copyOf(bytes, size); }
    }

    public static final class Reader {
        private final ByteBuffer buffer;

        public Reader(byte[] bytes) { buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN); }

        private int length() {
            int n = buffer.getInt();
            if (n < 0 || n > buffer.remaining()) throw new IllegalArgumentException("kiln bridge: a length past the end of the bytes");
            return n;
        }

        public boolean readBoolean() { return buffer.get() != 0; }

        public int readInt() { return buffer.getInt(); }

        public long readLong() { return buffer.getLong(); }

        public float readFloat() { return buffer.getFloat(); }

        public double readDouble() { return buffer.getDouble(); }

        public byte[] readBytes() {
            byte[] v = new byte[length()];
            buffer.get(v);
            return v;
        }

        public String readString() { return new String(readBytes(), StandardCharsets.UTF_8); }

        /** A list's element count. */
        public int readCount() { return length(); }

        /** Throws when bytes are left: the value did not use them all. */
        public void end() {
            if (buffer.hasRemaining()) throw new IllegalArgumentException("kiln bridge: bytes left after the value");
        }
    }
}
