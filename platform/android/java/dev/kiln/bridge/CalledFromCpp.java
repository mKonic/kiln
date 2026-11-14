package dev.kiln.bridge;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * A Kotlin interface implemented in Kotlin and called from C++. The app registers its
 * implementation with {@code <Name>Cpp.register(impl)}; tools/gen-jvm writes a C++ class with a
 * method per function (a suspend function is an rt::Task).
 */
@Retention(RetentionPolicy.SOURCE)
@Target(ElementType.TYPE)
public @interface CalledFromCpp {}
