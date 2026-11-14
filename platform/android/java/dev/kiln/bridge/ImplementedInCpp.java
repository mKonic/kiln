package dev.kiln.bridge;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * A Kotlin interface whose implementation is C++. tools/gen-jvm writes {@code object <Name>Cpp}
 * implementing it in Kotlin, and a C++ base class with a method per function for the app to
 * implement; the app defines {@code std::unique_ptr<Name> MakeName()} to supply it.
 */
@Retention(RetentionPolicy.SOURCE)
@Target(ElementType.TYPE)
public @interface ImplementedInCpp {}
