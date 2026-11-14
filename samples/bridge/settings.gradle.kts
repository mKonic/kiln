// kiln bridge: an app with JVM code of its own, built by Gradle (tools/kiln build|apk|bundle|run
// bridge hands over to it). The native side is CMakeLists.txt, built from the kiln checkout.
pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
dependencyResolutionManagement { repositories { google(); mavenCentral() } }
rootProject.name = "bridge"
