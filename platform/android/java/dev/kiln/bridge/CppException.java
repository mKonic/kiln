package dev.kiln.bridge;

/** A C++ exception that left a bridged call: {@link #type} names its C++ type. */
public class CppException extends RuntimeException {
    public final String type;

    public CppException(String type, String message) {
        super(message);
        this.type = type;
    }
}
