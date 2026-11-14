# kiln bridge: C++ and Kotlin calling each other through the JVM bridge (tools/gen-jvm). Gradle
# builds it (tools/kiln run bridge hands over); its Kotlin is in src/main/kotlin.
kiln_add_app(bridge
    SOURCES main.cpp
    LIBS kiln_material3
    PACKAGE dev.kiln.bridgesample
    LABEL "kiln bridge"
    JVM_BRIDGE src/main/kotlin
)
