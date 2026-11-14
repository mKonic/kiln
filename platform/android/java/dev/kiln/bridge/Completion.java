package dev.kiln.bridge;

/** How C++ hands a suspend call's result back to the Kotlin coroutine waiting for it. */
public interface Completion {
    /** The result's bytes in the bridge's format (null for Unit). */
    void resume(byte[] value);

    void fail(String type, String message);
}
