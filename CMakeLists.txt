cmake_minimum_required(VERSION 3.22)
project(kiln LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Hearth.cmake)
include(cmake/Shaders.cmake)

# Clipper2 (polygon booleans behind Path.op).
add_library(clipper2 STATIC vendor/Clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp)
target_include_directories(clipper2 SYSTEM PUBLIC vendor/Clipper2/CPP/Clipper2Lib/include)

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

file(GLOB_RECURSE MCU_SOURCES CONFIGURE_DEPENDS vendor/material-color-utilities/cpp/*.cc)
add_library(mcu STATIC ${MCU_SOURCES})
target_include_directories(mcu PUBLIC vendor/material-color-utilities PRIVATE vendor/material-color-utilities/compat)

file(GLOB_RECURSE KILN_MATERIAL3_SOURCES CONFIGURE_DEPENDS material3/src/*.cpp)
add_library(kiln_material3 STATIC ${KILN_MATERIAL3_SOURCES})
target_include_directories(kiln_material3 PUBLIC material3/src)
target_link_libraries(kiln_material3 PUBLIC kiln_foundation kiln_navigation3 PRIVATE mcu)
target_compile_options(kiln_material3 PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

if(ANDROID)
    file(GLOB KILN_ANDROID_SOURCES CONFIGURE_DEPENDS platform/android/jni/*.cpp)
    add_library(kiln_android STATIC ${KILN_ANDROID_SOURCES})
    target_link_libraries(kiln_android PUBLIC kiln_foundation jnigraphics android log)
    target_compile_options(kiln_android PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

    # An app is one shared library linking kiln statically; KilnActivity loads it by the
    # <meta-data android:name="dev.kiln.lib_name"> in the manifest.
    add_library(hello SHARED samples/hello/main.cpp)
    # Whole archive: JNI_OnLoad and the registered natives are referenced only by the VM.
    target_link_libraries(hello PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln_android>")
    target_link_options(hello PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    add_library(list SHARED samples/list/main.cpp)
    target_link_libraries(list PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln_android>")
    target_link_options(list PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    add_library(gallery SHARED samples/gallery/main.cpp)
    target_link_libraries(gallery PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln_android>" kiln_material3)
    target_link_options(gallery PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
    add_library(catalog SHARED samples/catalog/main.cpp)
    target_link_libraries(catalog PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln_android>" kiln_material3 kiln_navigation)
    target_link_options(catalog PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
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
    add_custom_command(TARGET gallery POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
                       ${CMAKE_SOURCE_DIR}/samples/gallery/assets $<TARGET_FILE_DIR:gallery>/assets)
    add_executable(catalog samples/catalog/main.cpp)
    target_link_libraries(catalog PRIVATE kiln_hostdev kiln_material3 kiln_navigation)
    add_custom_command(TARGET catalog POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
                       ${CMAKE_SOURCE_DIR}/samples/catalog/assets $<TARGET_FILE_DIR:catalog>/assets)
endif()
target_sources(gallery PRIVATE samples/gallery/MaterialComponents.cpp)
# The androidx material3 catalog: samples/catalog/gen-registry writes the registry and icons.
file(GLOB KILN_CATALOG_SAMPLES CONFIGURE_DEPENDS samples/catalog/samples/*.cpp)
target_sources(catalog PRIVATE samples/catalog/Registry.gen.cpp samples/catalog/gen/Icons.cpp ${KILN_CATALOG_SAMPLES})

enable_testing()
file(GLOB KILN_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(kiln_tests ${KILN_TEST_SOURCES})
target_link_libraries(kiln_tests PRIVATE kiln_material3 kiln_navigation)
if(ANDROID)
    # Run on a device with tools/device-test (pushed to /data/local/tmp), which builds it.
    set_target_properties(kiln_tests PROPERTIES EXCLUDE_FROM_ALL ON)
    target_link_options(kiln_tests PRIVATE -Wl,-z,max-page-size=16384)
else()
    add_test(NAME kiln_tests COMMAND kiln_tests)
endif()
