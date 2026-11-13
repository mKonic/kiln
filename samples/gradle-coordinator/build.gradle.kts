plugins { id("com.android.application") version "9.4.1" }

android {
    namespace = "dev.kiln.coordinator"
    compileSdk = 36
    // kiln's NDK (r30); KILN_NDK overrides it.
    ndkPath = System.getenv("KILN_NDK") ?: "/opt/android-ndk"
    ndkVersion = "30.0.16248370"
    defaultConfig {
        applicationId = "dev.kiln.coordinator"
        minSdk = 31
        targetSdk = 36
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_static") } }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt") } }
    buildFeatures { prefab = true }
    packaging { jniLibs { useLegacyPackaging = false } }
}

dependencies {
    implementation(files("../../build/kiln.aar"))
    implementation("com.google.android.material:material:1.12.0")
}
