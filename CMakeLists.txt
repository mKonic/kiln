cmake_minimum_required(VERSION 3.22)
project(kiln LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Hearth.cmake)
include(cmake/Shaders.cmake)

# kiln_core: platform-independent; never includes JNI or Android headers.
file(GLOB_RECURSE KILN_CORE_SOURCES CONFIGURE_DEPENDS core/src/*.cpp render/*.cpp)
add_library(kiln_core STATIC ${KILN_CORE_SOURCES})
target_include_directories(kiln_core PUBLIC include core/src render)
target_link_libraries(kiln_core PUBLIC hearth)
target_compile_options(kiln_core PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
kiln_embed_shaders(kiln_core render/shaders)

if(ANDROID)
    file(GLOB KILN_ANDROID_SOURCES CONFIGURE_DEPENDS platform/android/jni/*.cpp)
    add_library(kiln_android STATIC ${KILN_ANDROID_SOURCES})
    target_link_libraries(kiln_android PUBLIC kiln_core jnigraphics android log)
    target_compile_options(kiln_android PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

    # An app is one shared library linking kiln statically; KilnActivity loads it by the
    # <meta-data android:name="dev.kiln.lib_name"> in the manifest.
    add_library(hello SHARED samples/hello/main.cpp)
    # Whole archive: JNI_OnLoad and the registered natives are referenced only by the VM.
    target_link_libraries(hello PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln_android>")
    target_link_options(hello PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
else()
    file(GLOB KILN_HOSTDEV_SOURCES CONFIGURE_DEPENDS platform/hostdev/*.cpp)
    find_package(glfw3 REQUIRED)
    add_library(kiln_hostdev STATIC ${KILN_HOSTDEV_SOURCES})
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HB REQUIRED IMPORTED_TARGET harfbuzz freetype2)
    target_link_libraries(kiln_hostdev PUBLIC kiln_core glfw PkgConfig::HB)
    add_executable(hello samples/hello/main.cpp)
    target_link_libraries(hello PRIVATE kiln_hostdev)
endif()
