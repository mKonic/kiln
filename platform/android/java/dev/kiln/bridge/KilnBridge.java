package dev.kiln.bridge;

/** The natives the generated Kotlin calls; kiln registers them when its library loads. */
public final class KilnBridge {
    private KilnBridge() {}

    /** A Kotlin call that C++ awaits (handle) finished with this result. */
    public static native void complete(long handle, byte[] value);

    /** ... failed with this exception, or was cancelled on the Kotlin side. */
    public static native void fail(long handle, Throwable error);

    /** A C++ call Kotlin awaits (handle, from the native start function) was cancelled. */
    public static native void cancel(long handle);
}
