// kiln inside an app's own layout: a KilnView under a CoordinatorLayout's app bar, built with
// Gradle against kiln's AAR (tools/build-aar).
pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
dependencyResolutionManagement { repositories { google(); mavenCentral() } }
rootProject.name = "gradle-coordinator"
