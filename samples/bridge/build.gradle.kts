import javax.inject.Inject

plugins { id("com.android.application") version "9.4.1" }

// The kiln checkout this sample builds against (samples/bridge sits inside it).
val kilnRoot = file(System.getenv("KILN_ROOT") ?: "../..")

android {
    namespace = "dev.kiln.bridgesample"
    compileSdk = 37
    // kiln's NDK; KILN_NDK overrides it.
    ndkPath = System.getenv("KILN_NDK") ?: "/opt/android-ndk"
    ndkVersion = "30.0.16248370"
    defaultConfig {
        applicationId = "dev.kiln.bridgesample"
        minSdk = 31
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        // kiln's CMake builds this app's library (bridge, from CMakeLists.txt) and nothing else.
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_static"); targets += listOf("bridge") } }
    }
    // kiln needs CMake 3.24+, newer than the SDK's: the one on PATH (KILN_CMAKE_VERSION
    // overrides), which AGP finds there when the SDK has no such version.
    externalNativeBuild {
        cmake {
            path = kilnRoot.resolve("CMakeLists.txt")
            version = System.getenv("KILN_CMAKE_VERSION") ?: providers.exec { commandLine("cmake", "--version") }
                .standardOutput.asText.get().lineSequence().first().removePrefix("cmake version ").trim()
        }
    }
    sourceSets {
        getByName("main") {
            // kiln's Java host (KilnActivity and the platform glue), the app's resources and assets.
            java.directories.add(kilnRoot.resolve("platform/android/java").path)
            res.directories.add("res")
            assets.directories.add("assets")
        }
    }
    // Release builds are signed with KILN_KEYSTORE (KILN_KEY_ALIAS, KILN_KEYSTORE_PASSWORD,
    // KILN_KEY_PASSWORD), else with kiln's debug key.
    signingConfigs {
        create("kiln") {
            storeFile = file(System.getenv("KILN_KEYSTORE") ?: kilnRoot.resolve("build/kiln-debug.keystore").path)
            storePassword = System.getenv("KILN_KEYSTORE_PASSWORD") ?: "android"
            keyAlias = System.getenv("KILN_KEY_ALIAS") ?: "kiln"
            keyPassword = System.getenv("KILN_KEY_PASSWORD") ?: storePassword
        }
    }
    buildTypes { getByName("release") { signingConfig = signingConfigs.getByName("kiln") } }
    packaging { jniLibs { useLegacyPackaging = false } }
}

// The JVM bridge's Kotlin half (tools/gen-jvm), from the @ImplementedInCpp, @CalledFromCpp and
// @CppStruct declarations in src/main/kotlin; JVM_BRIDGE in CMakeLists.txt writes the C++ half.
abstract class KilnBridge : DefaultTask() {
    @get:InputFiles abstract val sources: ConfigurableFileCollection
    @get:InputFile abstract val generator: RegularFileProperty
    @get:Input abstract val library: Property<String>
    @get:OutputDirectory abstract val output: DirectoryProperty
    @get:Inject abstract val exec: ExecOperations

    @TaskAction fun generate() {
        val out = output.get().asFile
        out.deleteRecursively()
        exec.exec { commandLine(listOf("python3", generator.get().asFile.path, "--lib", library.get(), "--kotlin", out.path) + sources.files.map { it.path }) }
    }
}
val kilnBridge = tasks.register<KilnBridge>("kilnBridge") {
    sources.from(fileTree("src/main/kotlin") { include("**/*.kt") })
    generator.set(kilnRoot.resolve("tools/gen-jvm"))
    library.set("bridge")
}
androidComponents { onVariants { v -> v.sources.kotlin?.addGeneratedSourceDirectory(kilnBridge, KilnBridge::output) } }

dependencies {
    // The androidx interfaces kiln's embedded-View holder implements.
    implementation("androidx.core:core:1.9.0")
    // The JVM bridge's suspend calls.
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.11.0")
}
