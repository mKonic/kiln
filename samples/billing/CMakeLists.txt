# kiln billing: Google Play Billing from C++, through the JVM bridge (tools/gen-jvm). Gradle builds
# it (tools/kiln run billing hands over); the Kotlin that wraps the billing library is src/main/kotlin.
kiln_add_app(billing
    SOURCES main.cpp
    LIBS kiln_material3
    PACKAGE dev.kiln.billingsample
    LABEL "kiln billing"
    JVM_BRIDGE src/main/kotlin
)
