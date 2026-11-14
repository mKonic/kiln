package dev.kiln.bridge;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * A Kotlin data class or enum class that crosses the bridge by value: tools/gen-jvm writes the
 * matching C++ struct or enum and the code that copies it across (dev.kiln.bridge.Codec).
 */
@Retention(RetentionPolicy.SOURCE)
@Target(ElementType.TYPE)
public @interface CppStruct {}
