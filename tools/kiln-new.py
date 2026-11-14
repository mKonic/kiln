#!/usr/bin/env python3
"""Writes a new app's folder for `tools/kiln new`: kiln-new.py DIR NAME PACKAGE LABEL [--jvm KILN_ROOT]."""
import os
import shutil
import struct
import sys
import zlib

CMAKE = '''# {label}: built by kiln (tools/kiln build|apk|run {name}), on Android or in a desktop window.
kiln_add_app({name}
    SOURCES src/main.cpp
    LIBS kiln_material3 kiln_navigation
    PACKAGE {package}
    LABEL "{label}"
    ASSETS assets
    RES res
    ICON icon.png
    VERSION_CODE 1
    VERSION_NAME 1.0
    # PERMISSIONS android.permission.CAMERA
    # APPLICATION_XML android/application.xml   (extra elements inside <application>)
    # MANIFEST_XML android/manifest.xml         (extra elements inside <manifest>)
    # ACTIVITY_XML android/activity.xml         (inside the app's <activity>: deep-link and share intent filters)
    # ACTIVITY_ATTRIBUTES "android:launchMode=\\"singleTop\\""
    # APPLICATION_ATTRIBUTES "android:networkSecurityConfig=\\"@xml/network\\""
    # ICON_FOREGROUND res/icon_foreground.png ICON_BACKGROUND "#FFFFFF"   (an adaptive icon)
    # SPLASH_BACKGROUND "#FFFFFF"                (the splash screen; its icon is the app's)
)
'''

MAIN = '''#include <kiln/kiln.hpp>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Layouts.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Switch.h>
#include <kiln/material3/Text.h>

#include <string>

#include "kiln_res.h"   // R::string, R::plurals...: the names in res/, checked when compiling

using namespace kiln::foundation;
using namespace kiln::material3;
using namespace kiln::ui;

namespace {{
    void Counter() {{
        auto& count = RememberState(0);
        auto& fast = RememberState(false);
        ScaffoldOptions options;
        options.topBar = [] {{ TopAppBar([] {{ Text(kiln::ui::StringResource(R::string::app_name)); }}, {{}}); }};
        Scaffold({{}}, options, [&](const PaddingValues& padding) {{
            Column(Padding(Padding(FillMax(Modifier{{}}, Axis::Both), padding), 16_dp),
                   Arrangement::SpacedBy(16_dp), AlignmentH::Start(), [&] {{
                Text(kiln::ui::PluralStringResource(R::plurals::pressed, count.Get(), count.Get()),
                     {{}}, {{ .style = MaterialTheme::typography().headlineMedium }});
                Row(Modifier{{}}, Arrangement::SpacedBy(8_dp), AlignmentV::CenterVertically(), [&] {{
                    Switch(fast.Get(), [&](bool on) {{ fast.Set(on); }});
                    Text(kiln::ui::StringResource(R::string::count_by_ten));
                }});
                Button([&] {{ count.Set(count.Get() + (fast.Get() ? 10 : 1)); }}, {{}}, {{}}, [] {{ Text(kiln::ui::StringResource(R::string::add)); }});
                auto& list = RememberLazyListState();
                LazyColumn(Weight(Modifier{{}}, 1), list, [&](LazyListScope& s) {{
                    s.Items(count.Get(), [](int i) {{ Text(kiln::ui::StringResource(R::string::row, i)); }});
                }});
            }});
        }});
    }}
}}

extern "C" void kiln_main(kiln_app* app) {{
    kiln::SetContent(app, [] {{ MaterialTheme([] {{ Counter(); }}); }});
}}
'''


STRINGS = '''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">{label}</string>
    <plurals name="pressed">
        <item quantity="one">Pressed %d time</item>
        <item quantity="other">Pressed %d times</item>
    </plurals>
    <string name="count_by_ten">Count by ten</string>
    <string name="add">Add</string>
    <string name="row">Row %d</string>
</resources>
'''


def icon_png(size=192):
    """A launcher icon: a purple circle on transparent, anti-aliased (stdlib only)."""
    rows = []
    c, r = (size - 1) / 2, size * 0.46
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            a = max(0.0, min(1.0, r - d + 0.5))
            row += bytes((0x67, 0x50, 0xA4, int(a * 255)))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


GRADLE_SETTINGS = '''// {label}: an app with JVM code of its own, built by Gradle (tools/kiln build|apk|bundle|run
// {name} hands over to it). The native side is CMakeLists.txt, built from the kiln checkout.
pluginManagement {{ repositories {{ google(); mavenCentral(); gradlePluginPortal() }} }}
dependencyResolutionManagement {{ repositories {{ google(); mavenCentral() }} }}
rootProject.name = "{name}"
'''

GRADLE_BUILD = '''import javax.inject.Inject

plugins {{ id("com.android.application") version "9.4.1" }}

// The kiln checkout this app builds against (apps/{name} sits inside it).
val kilnRoot = file(System.getenv("KILN_ROOT") ?: "../..")

android {{
    namespace = "{package}"
    compileSdk = 37
    // kiln's NDK; KILN_NDK overrides it.
    ndkPath = System.getenv("KILN_NDK") ?: "/opt/android-ndk"
    ndkVersion = "30.0.16248370"
    defaultConfig {{
        applicationId = "{package}"
        minSdk = 31
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"
        ndk {{ abiFilters += listOf("arm64-v8a", "x86_64") }}
        // kiln's CMake builds this app's library ({name}, from CMakeLists.txt) and nothing else.
        externalNativeBuild {{ cmake {{ arguments += listOf("-DANDROID_STL=c++_static"); targets += listOf("{name}") }} }}
    }}
    // kiln needs CMake 3.24+, newer than the SDK's: the one on PATH (KILN_CMAKE_VERSION
    // overrides), which AGP finds there when the SDK has no such version.
    externalNativeBuild {{
        cmake {{
            path = kilnRoot.resolve("CMakeLists.txt")
            version = System.getenv("KILN_CMAKE_VERSION") ?: providers.exec {{ commandLine("cmake", "--version") }}
                .standardOutput.asText.get().lineSequence().first().removePrefix("cmake version ").trim()
        }}
    }}
    sourceSets {{
        getByName("main") {{
            // kiln's Java host (KilnActivity and the platform glue), the app's resources and assets.
            java.directories.add(kilnRoot.resolve("platform/android/java").path)
            res.directories.add(kilnRoot.resolve("platform/android/res").path)   // kiln's (widget layouts)
            res.directories.add("res")
            assets.directories.add("assets")
        }}
    }}
    // Release builds are signed with KILN_KEYSTORE (KILN_KEY_ALIAS, KILN_KEYSTORE_PASSWORD,
    // KILN_KEY_PASSWORD), else with kiln's debug key.
    signingConfigs {{
        create("kiln") {{
            storeFile = file(System.getenv("KILN_KEYSTORE") ?: kilnRoot.resolve("build/kiln-debug.keystore").path)
            storePassword = System.getenv("KILN_KEYSTORE_PASSWORD") ?: "android"
            keyAlias = System.getenv("KILN_KEY_ALIAS") ?: "kiln"
            keyPassword = System.getenv("KILN_KEY_PASSWORD") ?: storePassword
        }}
    }}
    buildTypes {{ getByName("release") {{ signingConfig = signingConfigs.getByName("kiln") }} }}
    packaging {{ jniLibs {{ useLegacyPackaging = false }} }}
}}

// The JVM bridge's Kotlin half (tools/gen-jvm), from the @ImplementedInCpp, @CalledFromCpp and
// @CppStruct declarations in src/main/kotlin; JVM_BRIDGE in CMakeLists.txt writes the C++ half.
abstract class KilnBridge : DefaultTask() {{
    @get:InputFiles abstract val sources: ConfigurableFileCollection
    @get:InputFile abstract val generator: RegularFileProperty
    @get:Input abstract val library: Property<String>
    @get:OutputDirectory abstract val output: DirectoryProperty
    @get:Inject abstract val exec: ExecOperations

    @TaskAction fun generate() {{
        val out = output.get().asFile
        out.deleteRecursively()
        exec.exec {{ commandLine(listOf("python3", generator.get().asFile.path, "--lib", library.get(), "--kotlin", out.path) + sources.files.map {{ it.path }}) }}
    }}
}}
val kilnBridge = tasks.register<KilnBridge>("kilnBridge") {{
    sources.from(fileTree("src/main/kotlin") {{ include("**/*.kt") }})
    generator.set(kilnRoot.resolve("tools/gen-jvm"))
    library.set("{name}")
}}
androidComponents {{ onVariants {{ v -> v.sources.kotlin?.addGeneratedSourceDirectory(kilnBridge, KilnBridge::output) }} }}

dependencies {{
    // The androidx interfaces kiln's embedded-View holder implements.
    implementation("androidx.core:core:1.9.0")
    // The JVM bridge's suspend calls.
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.11.0")
    // Maven dependencies of the app's own JVM code, for example:
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
}}
'''

GRADLE_PROPERTIES = '''org.gradle.jvmargs=-Xmx4g -Dfile.encoding=UTF-8
android.useAndroidX=true
'''

GRADLE_MANIFEST = '''<?xml version="1.0" encoding="utf-8"?>
<!-- The manifest of the Gradle form: permissions and activities go here (kiln_add_app's
     PERMISSIONS and *_XML options are tools/kiln's). -->
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <queries>
        <intent>
            <action android:name="android.intent.action.TTS_SERVICE" />
        </intent>
        <intent>
            <action android:name="android.speech.RecognitionService" />
        </intent>
    </queries>
    <application android:name=".App" android:label="{label}" android:icon="@mipmap/ic_launcher"
                 android:supportsRtl="true" android:enableOnBackInvokedCallback="true">
        <activity android:name="dev.kiln.KilnActivity" android:exported="true"
                  android:theme="@android:style/Theme.DeviceDefault.NoActionBar"
                  android:windowSoftInputMode="adjustResize"
                  android:configChanges="orientation|screenSize|smallestScreenSize|screenLayout|uiMode|density|fontScale|fontWeightAdjustment|keyboard|keyboardHidden|navigation|locale|layoutDirection|colorMode|touchscreen|grammaticalGender">
            <meta-data android:name="dev.kiln.lib_name" android:value="{name}" />
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
        <!-- kiln::work's jobs (JobScheduler). -->
        <service android:name="dev.kiln.KilnJobService" android:exported="false"
                 android:permission="android.permission.BIND_JOB_SERVICE" />
        <!-- kiln::media's sessions: playback in a media foreground service (the app declares
             FOREGROUND_SERVICE and FOREGROUND_SERVICE_MEDIA_PLAYBACK to use it). -->
        <service android:name="dev.kiln.KilnMediaService" android:exported="false"
                 android:foregroundServiceType="mediaPlayback" />
        <!-- ui::GetUriForFile: the app's files, shared by content:// URI (FileProvider). -->
        <provider android:name="dev.kiln.KilnFileProvider" android:authorities="${{applicationId}}.kiln.fileprovider"
                  android:exported="false" android:grantUriPermissions="true" />
        <!-- kiln::alarm: alarms going off, and set again after a reboot (with the app's
             RECEIVE_BOOT_COMPLETED) or when exact alarms are allowed again. -->
        <receiver android:name="dev.kiln.KilnAlarmReceiver" android:exported="false" />
        <receiver android:name="dev.kiln.KilnAlarmRescheduler" android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.BOOT_COMPLETED" />
                <action android:name="android.app.action.SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED" />
            </intent-filter>
        </receiver>
    </application>
</manifest>
'''

GRADLE_APP_KT = '''package {package}

import android.app.Application
import android.util.Log
import okhttp3.HttpUrl.Companion.toHttpUrl

/** The app's own JVM code: here an Application that uses a Maven dependency (OkHttp). */
class App : Application() {{
    override fun onCreate() {{
        super.onCreate()
        val url = "https://example.com/notes?page=2".toHttpUrl()
        Log.i("{name}", "Kotlin and OkHttp are in: ${{url.host}} page ${{url.queryParameter("page")}}")
    }}
}}
'''


def write_gradle(out, name, package, label, kiln_root):
    """The Gradle form: settings, build file, wrapper, manifest and a Kotlin class."""
    with open(os.path.join(out, "settings.gradle.kts"), "w") as f:
        f.write(GRADLE_SETTINGS.format(name=name, label=label))
    with open(os.path.join(out, "build.gradle.kts"), "w") as f:
        f.write(GRADLE_BUILD.format(name=name, package=package))
    with open(os.path.join(out, "gradle.properties"), "w") as f:
        f.write(GRADLE_PROPERTIES)
    sample = os.path.join(kiln_root, "samples", "gradle-notes")
    os.makedirs(os.path.join(out, "gradle", "wrapper"), exist_ok=True)
    for rel in ("gradlew", os.path.join("gradle", "wrapper", "gradle-wrapper.jar"), os.path.join("gradle", "wrapper", "gradle-wrapper.properties")):
        shutil.copy2(os.path.join(sample, rel), os.path.join(out, rel))
    os.makedirs(os.path.join(out, "src", "main", "kotlin", *package.split(".")), exist_ok=True)
    with open(os.path.join(out, "src", "main", "AndroidManifest.xml"), "w") as f:
        f.write(GRADLE_MANIFEST.format(name=name, label=label.replace("&", "&amp;").replace('"', "&quot;")))
    with open(os.path.join(out, "src", "main", "kotlin", *package.split("."), "App.kt"), "w") as f:
        f.write(GRADLE_APP_KT.format(name=name, package=package))
    # The launcher icon as a resource (the manifest names it).
    os.makedirs(os.path.join(out, "res", "mipmap-xxxhdpi"), exist_ok=True)
    shutil.copy2(os.path.join(out, "icon.png"), os.path.join(out, "res", "mipmap-xxxhdpi", "ic_launcher.png"))


def main():
    out, name, package, label = sys.argv[1:5]
    jvm_root = sys.argv[6] if len(sys.argv) > 6 and sys.argv[5] == "--jvm" else None
    for sub in ("src", "assets", os.path.join("res", "values")):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    cmake = CMAKE.format(name=name, package=package, label=label)
    if jvm_root:
        # The JVM bridge's C++ half, from the same Kotlin as Gradle's half.
        cmake = cmake.replace("    VERSION_NAME 1.0\n", "    VERSION_NAME 1.0\n    JVM_BRIDGE src/main/kotlin\n")
    with open(os.path.join(out, "CMakeLists.txt"), "w") as f:
        f.write(cmake)
    with open(os.path.join(out, "src", "main.cpp"), "w") as f:
        f.write(MAIN.format(label=label.replace('"', '\\"')))
    with open(os.path.join(out, "assets", ".keep"), "w"):
        pass
    with open(os.path.join(out, "icon.png"), "wb") as f:
        f.write(icon_png())
    with open(os.path.join(out, "res", "values", "strings.xml"), "w") as f:
        f.write(STRINGS.format(label=label.replace("&", "&amp;").replace("<", "&lt;").replace("'", "\\'")))
    with open(os.path.join(out, ".gitignore"), "w") as f:
        f.write("build/\n.gradle/\n.cxx/\n.kotlin/\nlocal.properties\n")
    if jvm_root:
        write_gradle(out, name, package, label, jvm_root)


if __name__ == "__main__":
    main()
