# hearth (vendor/hearth) builds with premake upstream; kiln compiles the same sources here so the
# NDK and host builds share one CMake graph. Mirrors Hearth/premake5.lua + vendor-build/VulkanDeps.lua.
set(HEARTH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../vendor/hearth)

add_library(hearth_vulkan_deps STATIC
    ${HEARTH_ROOT}/vendor/vk-bootstrap/src/VkBootstrap.cpp
    ${HEARTH_ROOT}/vendor-build/src/vk_mem_alloc_impl.cpp)
target_include_directories(hearth_vulkan_deps SYSTEM PUBLIC
    ${HEARTH_ROOT}/vendor/Vulkan-Headers/include
    ${HEARTH_ROOT}/vendor/VulkanMemoryAllocator/include
    ${HEARTH_ROOT}/vendor/vk-bootstrap/src)
set_target_properties(hearth_vulkan_deps PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20)

file(GLOB_RECURSE HEARTH_SOURCES CONFIGURE_DEPENDS ${HEARTH_ROOT}/Hearth/src/*.cpp)
add_library(hearth STATIC ${HEARTH_SOURCES})
target_include_directories(hearth PUBLIC ${HEARTH_ROOT}/Hearth/src)
target_link_libraries(hearth PUBLIC hearth_vulkan_deps)
set_target_properties(hearth PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 23)
target_compile_options(hearth PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

if(ANDROID)
    target_compile_definitions(hearth_vulkan_deps PUBLIC VK_USE_PLATFORM_ANDROID_KHR)
    target_link_libraries(hearth PUBLIC vulkan log android)
else()
    find_package(Vulkan REQUIRED)
    target_link_libraries(hearth PUBLIC Vulkan::Vulkan)
endif()
