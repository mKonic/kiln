// kiln billing: Google Play Billing used from C++ through the JVM bridge, built by Gradle
// (tools/kiln build|apk|bundle|run billing hands over to it). The native side is CMakeLists.txt.
pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
dependencyResolutionManagement { repositories { google(); mavenCentral() } }
rootProject.name = "billing"
