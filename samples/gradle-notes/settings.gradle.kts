// An Android app built with Gradle against kiln's AAR (tools/build-aar), through Prefab.
pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
dependencyResolutionManagement { repositories { google(); mavenCentral() } }
rootProject.name = "gradle-notes"
