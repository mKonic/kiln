cmake_minimum_required(VERSION 3.22)
project(gradle_notes C CXX)

# kiln from the AAR's Prefab package.
find_package(kiln REQUIRED CONFIG)

# The notes sample in C (samples/notes-c).
add_library(notes SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../../../../notes-c/main.c)
# Whole archive: kiln's JNI natives are only reached from Java. (CMake 3.24+ can say
# "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln::kiln>" instead.)
target_link_libraries(notes PRIVATE -Wl,--whole-archive $<TARGET_FILE:kiln::kiln> -Wl,--no-whole-archive kiln::kiln)
# Debug builds answer Volition (the provider is in src/debug/AndroidManifest.xml).
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_libraries(notes PRIVATE -Wl,--whole-archive $<TARGET_FILE:kiln::kiln_volition> -Wl,--no-whole-archive)
endif()
# Export JNI_OnLoad alone (the AAR's kiln/app.map), so the linker drops what the app never
# reaches, and fold identical code.
get_target_property(KILN_INCLUDES kiln::kiln INTERFACE_INCLUDE_DIRECTORIES)
target_link_options(notes PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,--icf=safe
                    -Wl,--version-script=${KILN_INCLUDES}/kiln/app.map)
