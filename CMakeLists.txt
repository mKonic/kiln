cmake_minimum_required(VERSION 3.24)   # $<LINK_LIBRARY:WHOLE_ARCHIVE,...>
project(kiln LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Hearth.cmake)
include(cmake/Shaders.cmake)

# Clipper2 (polygon booleans behind Path.op).
add_library(clipper2 STATIC vendor/Clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp)
target_include_directories(clipper2 SYSTEM PUBLIC vendor/Clipper2/CPP/Clipper2Lib/include)
# Z carries each point's source curve sample, so Path.op can give curves back.
target_compile_definitions(clipper2 PUBLIC USINGZ)

# kiln_core: platform-independent; never includes JNI or Android headers.
file(GLOB_RECURSE KILN_CORE_SOURCES CONFIGURE_DEPENDS core/src/*.cpp render/*.cpp)
file(GLOB SKIA_SHADOW_SOURCES CONFIGURE_DEPENDS vendor/skia-shadow/src/*.cpp)
add_library(skia_shadow STATIC ${SKIA_SHADOW_SOURCES})
target_include_directories(skia_shadow PUBLIC vendor/skia-shadow/src)
# Select Skia's curve subdivision, not its non-Ganesh control-polygon fallback.
target_compile_definitions(skia_shadow PRIVATE SK_GANESH)
add_library(kiln_core STATIC ${KILN_CORE_SOURCES})
target_include_directories(kiln_core PUBLIC include core/src render)
target_link_libraries(kiln_core PUBLIC hearth PRIVATE clipper2 skia_shadow)
target_compile_options(kiln_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
kiln_embed_shaders(kiln_core render/shaders)
if(NOT ANDROID)
    # net::Fetch on the desktop (Android goes through HttpURLConnection).
    find_package(CURL REQUIRED)
    target_link_libraries(kiln_core PRIVATE CURL::libcurl)
endif()

# Volition's commands (the debug endpoint): linked whole into debug apps and the tests only.
file(GLOB_RECURSE KILN_VOLITION_SOURCES CONFIGURE_DEPENDS volition/src/*.cpp)
add_library(kiln_volition STATIC ${KILN_VOLITION_SOURCES})
target_link_libraries(kiln_volition PUBLIC kiln_core)
target_compile_options(kiln_volition PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

file(GLOB_RECURSE KILN_FOUNDATION_SOURCES CONFIGURE_DEPENDS foundation/src/*.cpp)
add_library(kiln_foundation STATIC ${KILN_FOUNDATION_SOURCES})
target_include_directories(kiln_foundation PUBLIC foundation/src)
target_link_libraries(kiln_foundation PUBLIC kiln_core)
target_compile_options(kiln_foundation PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# navigation3: header-only (templates over the key type).
add_library(kiln_navigation3 INTERFACE)
target_include_directories(kiln_navigation3 INTERFACE navigation3/src)
target_link_libraries(kiln_navigation3 INTERFACE kiln_foundation)

# navigation: navigation-common, -runtime and -compose (routes, NavController, NavHost).
file(GLOB_RECURSE KILN_NAVIGATION_SOURCES CONFIGURE_DEPENDS navigation/src/*.cpp)
add_library(kiln_navigation STATIC ${KILN_NAVIGATION_SOURCES})
target_include_directories(kiln_navigation PUBLIC navigation/src)
target_link_libraries(kiln_navigation PUBLIC kiln_foundation)
target_compile_options(kiln_navigation PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# constraintlayout: constraintlayout-core's solver and widgets, and constraintlayout-compose's
# ConstraintLayout. No FMA contraction, so float results match the Java solver's.
file(GLOB_RECURSE KILN_CONSTRAINTLAYOUT_SOURCES CONFIGURE_DEPENDS constraintlayout/src/*.cpp)
add_library(kiln_constraintlayout STATIC ${KILN_CONSTRAINTLAYOUT_SOURCES})
target_include_directories(kiln_constraintlayout PUBLIC constraintlayout/src)
target_link_libraries(kiln_constraintlayout PUBLIC kiln_foundation)
target_compile_options(kiln_constraintlayout PRIVATE -Wall -Wextra -Wno-missing-field-initializers -ffp-contract=off)

# ui_test: the UI testing library (Compose ui-test): finders, matchers, actions, asserts and
# the main clock over the semantics tree, on the host and on devices.
file(GLOB_RECURSE KILN_UI_TEST_SOURCES CONFIGURE_DEPENDS testing/src/*.cpp)
add_library(kiln_ui_test STATIC ${KILN_UI_TEST_SOURCES})
target_include_directories(kiln_ui_test PUBLIC testing/src)
target_link_libraries(kiln_ui_test PUBLIC kiln_foundation)
target_compile_options(kiln_ui_test PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# sqlite: the SQLite amalgamation (vendor/sqlite; the NDK does not export the platform's) built
# with androidx sqlite-bundled's options, and kiln's wrapper (connections, statements, a
# database with migrations and observed queries). Apps name it in LIBS; sqlite3.h is public.
add_library(kiln_sqlite3 STATIC vendor/sqlite/sqlite3.c)
target_include_directories(kiln_sqlite3 SYSTEM PUBLIC vendor/sqlite)
target_compile_definitions(kiln_sqlite3 PRIVATE
    HAVE_USLEEP=1 SQLITE_DEFAULT_AUTOVACUUM=1 SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_ENABLE_COLUMN_METADATA SQLITE_ENABLE_FTS3 SQLITE_ENABLE_FTS3_PARENTHESIS SQLITE_ENABLE_FTS4
    SQLITE_ENABLE_FTS5 SQLITE_ENABLE_JSON1 SQLITE_ENABLE_MATH_FUNCTIONS SQLITE_ENABLE_NORMALIZE
    SQLITE_ENABLE_RTREE SQLITE_ENABLE_STAT4 SQLITE_HAVE_ISNAN SQLITE_OMIT_BUILTIN_TEST SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_PROGRESS_CALLBACK SQLITE_OMIT_SHARED_CACHE SQLITE_SECURE_DELETE SQLITE_TEMP_STORE=3
    SQLITE_THREADSAFE=2)
target_compile_options(kiln_sqlite3 PRIVATE -w)
file(GLOB_RECURSE KILN_SQLITE_SOURCES CONFIGURE_DEPENDS sqlite/src/*.cpp)
add_library(kiln_sqlite STATIC ${KILN_SQLITE_SOURCES})
target_include_directories(kiln_sqlite PUBLIC sqlite/src)
target_link_libraries(kiln_sqlite PUBLIC kiln_core kiln_sqlite3)
target_compile_options(kiln_sqlite PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# paging: paging-common and paging-compose (PagingSource, Pager, CachedIn, LazyPagingItems);
# header-only templates over the item type.
add_library(kiln_paging INTERFACE)
target_include_directories(kiln_paging INTERFACE paging/src)
target_link_libraries(kiln_paging INTERFACE kiln_core)

file(GLOB_RECURSE MCU_SOURCES CONFIGURE_DEPENDS vendor/material-color-utilities/cpp/*.cc)
add_library(mcu STATIC ${MCU_SOURCES})
target_include_directories(mcu PUBLIC vendor/material-color-utilities PRIVATE vendor/material-color-utilities/compat)

file(GLOB_RECURSE KILN_MATERIAL3_SOURCES CONFIGURE_DEPENDS material3/src/*.cpp)
add_library(kiln_material3 STATIC ${KILN_MATERIAL3_SOURCES})
target_include_directories(kiln_material3 PUBLIC material3/src)
target_link_libraries(kiln_material3 PUBLIC kiln_foundation kiln_navigation3 PRIVATE mcu)
target_compile_options(kiln_material3 PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# kiln_app_resources(target dir [TABLE]): the app's res/ folder as R:: names (kiln_res.h, from
# tools/gen-app-res) and, with TABLE (desktop builds), the values the generated tables serve.
set(KILN_GEN_APP_RES ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen-app-res CACHE INTERNAL "")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
function(kiln_app_resources target dir)
    cmake_parse_arguments(RES "TABLE" "" "" ${ARGN})
    set(out ${CMAKE_CURRENT_BINARY_DIR}/kiln_res/${target})
    file(GLOB_RECURSE files CONFIGURE_DEPENDS ${dir}/*)
    add_custom_command(OUTPUT ${out}/kiln_res.h ${out}/kiln_res_c.h ${out}/kiln_res_table.cpp
                       COMMAND ${Python3_EXECUTABLE} ${KILN_GEN_APP_RES} ${dir} ${out}
                       DEPENDS ${files} ${KILN_GEN_APP_RES} ${CMAKE_SOURCE_DIR}/tools/android_res.py
                       COMMENT "App resources for ${target}" VERBATIM)
    target_include_directories(${target} PRIVATE ${out})
    if(RES_TABLE)
        target_sources(${target} PRIVATE ${out}/kiln_res_table.cpp)
    else()
        target_sources(${target} PRIVATE ${out}/kiln_res.h)
    endif()
endfunction()

if(ANDROID)
    file(GLOB KILN_ANDROID_SOURCES CONFIGURE_DEPENDS platform/android/jni/*.cpp)
    add_library(kiln_android STATIC ${KILN_ANDROID_SOURCES})
    target_link_libraries(kiln_android PUBLIC kiln_foundation jnigraphics android nativewindow log mediandk aaudio camera2ndk)
    target_compile_options(kiln_android PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

    # An app is one shared library linking kiln statically; KilnActivity loads it by the
    # <meta-data android:name="dev.kiln.lib_name"> in the manifest.
    #   kiln_add_android_app(name SOURCES src... [LIBS lib...] [WHOLE lib...]
    #                        PACKAGE id [LABEL text] [ASSETS dir])
    # PACKAGE, LABEL and ASSETS go to build/android/<abi>/apps/<name>, which tools/kiln packs from.
    # kiln_android goes in whole (JNI_OnLoad and the natives it registers are reached only from
    # the VM); WHOLE adds others (the C ABI glue, reached only through kiln.h). The library
    # exports JNI_OnLoad alone, so the linker drops the code nothing reaches, and folds
    # identical functions whose address is never taken.
    # Set here, read when the function runs: from another project it still finds kiln's map.
    set(KILN_APP_MAP ${CMAKE_CURRENT_SOURCE_DIR}/platform/android/app.map CACHE INTERNAL "")
    set(KILN_GEN_JVM ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen-jvm CACHE INTERNAL "")
    function(kiln_add_android_app name)
        cmake_parse_arguments(APP "" "PACKAGE;LABEL;ASSETS;ICON;ICON_FOREGROUND;ICON_BACKGROUND;ICON_MONOCHROME;SPLASH_BACKGROUND;SPLASH_ICON;SPLASH_ICON_BACKGROUND;VERSION_CODE;VERSION_NAME;MANIFEST_XML;APPLICATION_XML;ACTIVITY_XML;ACTIVITY_ATTRIBUTES;APPLICATION_ATTRIBUTES;RES;JVM_BRIDGE;DEFAULT_LOCALE"
                              "SOURCES;LIBS;WHOLE;PERMISSIONS;WIDGETS" ${ARGN})
        if(NOT APP_PACKAGE)
            message(FATAL_ERROR "kiln_add_android_app(${name}): PACKAGE is required")
        endif()
        if(NOT APP_LABEL)
            set(APP_LABEL "${name}")
        endif()
        # <name>: the release library; <name>_debug (built on request) adds Volition's commands.
        foreach(variant "" "_debug")
            set(target ${name}${variant})
            add_library(${target} SHARED ${APP_SOURCES})
            set(whole_libs kiln_android ${APP_WHOLE})
            if(variant)
                list(APPEND whole_libs kiln_volition)
                set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL ON)
            endif()
            string(REPLACE ";" "," whole "${whole_libs}")
            target_link_libraries(${target} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${whole}>" ${APP_LIBS})
            target_link_options(${target} PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,--icf=safe
                                -Wl,--version-script=${KILN_APP_MAP})
            set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${KILN_APP_MAP})
        endforeach()
        if(APP_JVM_BRIDGE)
            # The JVM bridge's C++ half (tools/gen-jvm) from the app's annotated Kotlin; Gradle
            # writes the Kotlin half from the same files.
            if(NOT IS_ABSOLUTE "${APP_JVM_BRIDGE}")
                set(APP_JVM_BRIDGE "${CMAKE_CURRENT_SOURCE_DIR}/${APP_JVM_BRIDGE}")
            endif()
            file(GLOB_RECURSE kotlin CONFIGURE_DEPENDS ${APP_JVM_BRIDGE}/*.kt)
            set(out ${CMAKE_CURRENT_BINARY_DIR}/kiln_bridge_${name})
            add_custom_command(OUTPUT ${out}/kiln_bridge.h ${out}/kiln_bridge.cpp
                               COMMAND ${Python3_EXECUTABLE} ${KILN_GEN_JVM} --lib ${name} --cpp ${out} ${kotlin}
                               DEPENDS ${kotlin} ${KILN_GEN_JVM}
                               COMMENT "JVM bridge for ${name}" VERBATIM)
            foreach(target ${name} ${name}_debug)
                target_sources(${target} PRIVATE ${out}/kiln_bridge.cpp)
                target_include_directories(${target} PRIVATE ${out})
            endforeach()
        endif()
        string(REPLACE ";" "," permissions "${APP_PERMISSIONS}")
        string(REPLACE ";" "," widgets "${APP_WIDGETS}")
        foreach(f ASSETS ICON ICON_FOREGROUND ICON_BACKGROUND ICON_MONOCHROME SPLASH_ICON MANIFEST_XML APPLICATION_XML ACTIVITY_XML RES)   # paths relative to the app's CMakeLists.txt
            if(APP_${f} AND NOT IS_ABSOLUTE "${APP_${f}}" AND NOT "${APP_${f}}" MATCHES "^#")
                set(APP_${f} "${CMAKE_CURRENT_SOURCE_DIR}/${APP_${f}}")
            endif()
        endforeach()
        if(APP_RES)
            kiln_app_resources(${name} ${APP_RES})
            kiln_app_resources(${name}_debug ${APP_RES})
        endif()
        file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/apps/${name}
             CONTENT "res=${APP_RES}\npackage=${APP_PACKAGE}\nlabel=${APP_LABEL}\nassets=${APP_ASSETS}\nicon=${APP_ICON}\nicon_foreground=${APP_ICON_FOREGROUND}\nicon_background=${APP_ICON_BACKGROUND}\nicon_monochrome=${APP_ICON_MONOCHROME}\nsplash_background=${APP_SPLASH_BACKGROUND}\nsplash_icon=${APP_SPLASH_ICON}\nsplash_icon_background=${APP_SPLASH_ICON_BACKGROUND}\nversion_code=${APP_VERSION_CODE}\nversion_name=${APP_VERSION_NAME}\npermissions=${permissions}\nwidgets=${widgets}\nmanifest_xml=${APP_MANIFEST_XML}\napplication_xml=${APP_APPLICATION_XML}\nactivity_xml=${APP_ACTIVITY_XML}\nactivity_attributes=${APP_ACTIVITY_ATTRIBUTES}\napplication_attributes=${APP_APPLICATION_ATTRIBUTES}\ndefault_locale=${APP_DEFAULT_LOCALE}\nlib=$<TARGET_FILE:${name}>\nlib_debug=$<TARGET_FILE:${name}_debug>\n")
    endfunction()
    kiln_add_android_app(hello SOURCES samples/hello/main.cpp PACKAGE dev.kiln.hello LABEL "kiln hello")
    kiln_add_android_app(list SOURCES samples/list/main.cpp PACKAGE dev.kiln.list LABEL "kiln list")
    kiln_add_android_app(gallery SOURCES samples/gallery/main.cpp LIBS kiln_material3
                         PACKAGE dev.kiln.gallery LABEL "kiln gallery" ASSETS ${CMAKE_SOURCE_DIR}/samples/gallery/assets
                         RES ${CMAKE_SOURCE_DIR}/samples/gallery/res PERMISSIONS android.permission.CAMERA android.permission.RECORD_AUDIO android.permission.FOREGROUND_SERVICE
                             android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK android.permission.WAKE_LOCK android.permission.SCHEDULE_EXACT_ALARM
                             android.permission.RECEIVE_BOOT_COMPLETED android.permission.POST_NOTIFICATIONS android.permission.RECORD_AUDIO WIDGETS gallery)
    # Samples over the C ABI only (C, and C++ with kiln++).
    foreach(sample notes-c notes-cpp)
        file(GLOB src samples/${sample}/main.c*)
        string(REPLACE "-" "" id ${sample})
        kiln_add_android_app(${sample} SOURCES ${src} WHOLE kiln_material3 PACKAGE dev.kiln.${id} LABEL "kiln ${sample}")
        target_include_directories(${sample} PRIVATE include)
        target_include_directories(${sample}_debug PRIVATE include)
    endforeach()
    kiln_add_android_app(catalog SOURCES samples/catalog/main.cpp LIBS kiln_material3 kiln_navigation
                         PACKAGE dev.kiln.catalog LABEL "kiln catalog" ASSETS ${CMAKE_SOURCE_DIR}/samples/catalog/assets)
else()
    file(GLOB KILN_HOSTDEV_SOURCES CONFIGURE_DEPENDS platform/hostdev/*.cpp)
    find_package(glfw3 REQUIRED)
    add_library(kiln_hostdev STATIC ${KILN_HOSTDEV_SOURCES})
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HB REQUIRED IMPORTED_TARGET harfbuzz freetype2 libpng libjpeg)
    target_link_libraries(kiln_hostdev PUBLIC kiln_foundation glfw PkgConfig::HB)
    add_executable(hello samples/hello/main.cpp)
    target_link_libraries(hello PRIVATE kiln_hostdev)
    add_executable(list samples/list/main.cpp)
    target_link_libraries(list PRIVATE kiln_hostdev)
    add_executable(gallery samples/gallery/main.cpp)
    target_link_libraries(gallery PRIVATE kiln_hostdev kiln_material3)
    kiln_app_resources(gallery ${CMAKE_SOURCE_DIR}/samples/gallery/res TABLE)
    add_custom_command(TARGET gallery POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
                       ${CMAKE_SOURCE_DIR}/samples/gallery/assets $<TARGET_FILE_DIR:gallery>/assets)
    foreach(sample notes-c notes-cpp)
        file(GLOB src samples/${sample}/main.c*)
        add_executable(${sample} ${src})
        # The C ABI glue is only referenced through kiln.h: link it whole.
        target_link_libraries(${sample} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln_material3>" kiln_hostdev)
    endforeach()
    add_executable(catalog samples/catalog/main.cpp)
    target_link_libraries(catalog PRIVATE kiln_hostdev kiln_material3 kiln_navigation)
    add_custom_command(TARGET catalog POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
                       ${CMAKE_SOURCE_DIR}/samples/catalog/assets $<TARGET_FILE_DIR:catalog>/assets)
endif()
foreach(t gallery gallery_debug)   # the _debug variants exist on Android
    if(TARGET ${t})
        target_sources(${t} PRIVATE samples/gallery/MaterialComponents.cpp)
    endif()
endforeach()
# The androidx material3 catalog: samples/catalog/gen-registry writes the registry and icons.
file(GLOB KILN_CATALOG_SAMPLES CONFIGURE_DEPENDS samples/catalog/samples/*.cpp)
foreach(t catalog catalog_debug)
    if(TARGET ${t})
        target_sources(${t} PRIVATE samples/catalog/Registry.gen.cpp samples/catalog/ComponentPreviews.cpp samples/catalog/gen/Icons.cpp ${KILN_CATALOG_SAMPLES})
    endif()
endforeach()

# kiln_add_app(name SOURCES src... [LIBS lib...] [WHOLE lib...] PACKAGE id [LABEL text] [ASSETS dir]
#              [ICON png] [ICON_FOREGROUND png ICON_BACKGROUND png|#rrggbb [ICON_MONOCHROME png]]
#              [SPLASH_BACKGROUND #rrggbb] [SPLASH_ICON png] [SPLASH_ICON_BACKGROUND #rrggbb]
#              [VERSION_CODE n] [VERSION_NAME text] [PERMISSIONS perm...]
#              [MANIFEST_XML file] [APPLICATION_XML file] [ACTIVITY_XML file]
#              [ACTIVITY_ATTRIBUTES "android:launchMode=\"singleTop\" ..."]
#              [APPLICATION_ATTRIBUTES "android:networkSecurityConfig=\"@xml/network\" ..."] [RES dir]
#              [JVM_BRIDGE kotlin-dir] [WIDGETS name...] [DEFAULT_LOCALE tag])
# An app on Android (kiln_add_android_app, packed by tools/kiln) or in a desktop window. Paths
# are relative to the calling CMakeLists.txt. The XML files hold elements for the manifest's
# <manifest>, <application> or the app's <activity> (intent filters for deep links and shares);
# ACTIVITY_ATTRIBUTES go on that <activity>, APPLICATION_ATTRIBUTES on <application>. ICON is a
# 192 px launcher PNG; ICON_FOREGROUND and ICON_BACKGROUND (432 px layers, or a colour) make an
# adaptive icon, with ICON_MONOCHROME for themed icons. SPLASH_* style the launch splash screen
# (its icon defaults to the launcher icon). RES is an Android res/ folder: its names in kiln_res.h (R::), its
# values from Android's Resources on a device and from generated tables on the desktop.
# JVM_BRIDGE (Android, Gradle-built apps) generates the C++ half of the JVM bridge (tools/gen-jvm)
# from the annotated Kotlin under that folder. WIDGETS declares kiln::glance home-screen widgets,
# each with res/xml/<name>_widget.xml (its appwidget-provider info) in RES. DEFAULT_LOCALE is the
# language of RES's unqualified values (en by default); with translations the app lists its
# languages in the system's per-app language settings.
function(kiln_add_app name)
    if(ANDROID)
        kiln_add_android_app(${name} ${ARGN})
        return()
    endif()
    cmake_parse_arguments(APP "" "PACKAGE;LABEL;ASSETS;ICON;ICON_FOREGROUND;ICON_BACKGROUND;ICON_MONOCHROME;SPLASH_BACKGROUND;SPLASH_ICON;SPLASH_ICON_BACKGROUND;VERSION_CODE;VERSION_NAME;MANIFEST_XML;APPLICATION_XML;ACTIVITY_XML;ACTIVITY_ATTRIBUTES;APPLICATION_ATTRIBUTES;RES;JVM_BRIDGE;DEFAULT_LOCALE"
                          "SOURCES;LIBS;WHOLE;PERMISSIONS;WIDGETS" ${ARGN})
    add_executable(${name} ${APP_SOURCES})
    if(APP_RES)
        if(NOT IS_ABSOLUTE "${APP_RES}")
            set(APP_RES "${CMAKE_CURRENT_SOURCE_DIR}/${APP_RES}")
        endif()
        kiln_app_resources(${name} ${APP_RES} TABLE)
    endif()
    target_link_libraries(${name} PRIVATE kiln_hostdev ${APP_LIBS})
    if(APP_WHOLE)
        string(REPLACE ";" "," whole "${APP_WHOLE}")
        target_link_libraries(${name} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${whole}>")
    endif()
    if(APP_ASSETS)
        if(NOT IS_ABSOLUTE "${APP_ASSETS}")
            set(APP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/${APP_ASSETS}")
        endif()
        add_custom_command(TARGET ${name} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
                           ${APP_ASSETS} $<TARGET_FILE_DIR:${name}>/assets)
    endif()
endfunction()

# The JVM bridge's samples (Android only: Gradle builds them, with their Kotlin).
if(ANDROID)
    add_subdirectory(samples/bridge ${CMAKE_BINARY_DIR}/sample-bridge)
    add_subdirectory(samples/billing ${CMAKE_BINARY_DIR}/sample-billing)
endif()

# Apps made with `tools/kiln new` (apps/<name>/CMakeLists.txt; the folder is the app's own).
file(GLOB KILN_APP_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/apps/*)
foreach(dir ${KILN_APP_DIRS})
    if(EXISTS ${dir}/CMakeLists.txt)
        get_filename_component(app ${dir} NAME)
        add_subdirectory(${dir} ${CMAKE_BINARY_DIR}/app-${app})
    endif()
endforeach()

enable_testing()
file(GLOB KILN_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
# tests/capi: content written in C against include/kiln (the header must stay valid C).
file(GLOB KILN_CAPI_TEST_SOURCES CONFIGURE_DEPENDS tests/capi/*.c tests/capi/*.cpp)
set_source_files_properties(${KILN_CAPI_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
add_executable(kiln_tests ${KILN_TEST_SOURCES} ${KILN_CAPI_TEST_SOURCES})
# App resources as an app has them: R:: names and, everywhere (no Android Resources in the test
# process), the generated tables.
kiln_app_resources(kiln_tests ${CMAKE_SOURCE_DIR}/tests/res TABLE)
target_link_libraries(kiln_tests PRIVATE kiln_material3 kiln_navigation kiln_constraintlayout kiln_sqlite kiln_paging kiln_ui_test "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln_volition>")
if(ANDROID)
    # Run on a device with tools/device-test (pushed to /data/local/tmp), which builds it.
    set_target_properties(kiln_tests PROPERTIES EXCLUDE_FROM_ALL ON)
    target_link_options(kiln_tests PRIVATE -Wl,-z,max-page-size=16384)
else()
    add_test(NAME kiln_tests COMMAND kiln_tests)
endif()
