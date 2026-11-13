cmake_minimum_required(VERSION 3.22)
project(gradle_coordinator C CXX)
set(CMAKE_CXX_STANDARD 20)   # kiln++ (kilnpp.hpp) is C++20

# kiln from the AAR's Prefab package.
find_package(kiln REQUIRED CONFIG)

# The kiln content (main.cpp).
add_library(coordinator SHARED main.cpp)
# Whole archive: kiln's JNI natives are only reached from Java. (CMake 3.24+ can say
# "$<LINK_LIBRARY:WHOLE_ARCHIVE,kiln::kiln>" instead.)
target_link_libraries(coordinator PRIVATE -Wl,--whole-archive $<TARGET_FILE:kiln::kiln> -Wl,--no-whole-archive kiln::kiln)
# Export JNI_OnLoad alone (the AAR's kiln/app.map), so the linker drops what the app never
# reaches, and fold identical code.
get_target_property(KILN_INCLUDES kiln::kiln INTERFACE_INCLUDE_DIRECTORIES)
target_link_options(coordinator PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,--icf=safe
                    -Wl,--version-script=${KILN_INCLUDES}/kiln/app.map)
